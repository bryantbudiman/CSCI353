#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>

#include <boost/asio.hpp>
#include <glog/logging.h>

using namespace std;
using namespace boost::asio;

DEFINE_int32(
    port, 0,
    "Port number for server / where server is running");
DEFINE_string(
    ip_address, "127.0.0.1",
    "IP address for server / where server is running");
DEFINE_bool(
    server, false,
    "Whether to operate in server or client mode (true = server)");
DEFINE_string(
    filename, "",
    "Filename to capture from remote server");

// value used as delimiter / for marking the end of a message
const string kDelimiter = "#";

void runServer();
void runClient();
void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const string& message);
string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer);
string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const int numBytesToRead);

int main(int argc, char *argv[]) {
  // setup Google logging and flags
  // set logtostderr to true by default, but it can still be overridden by flags
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  LOG(INFO) << "Started pa2 - file transfer edition";

  // verify port number set
  if (FLAGS_port == 0) {
    LOG(FATAL) << "Port number must be set";
  } else if (FLAGS_port > 65535 || FLAGS_port < 0) {
    LOG(FATAL) << "Invalid port number";
  }

  // run in server or client mode, depending on the flag
  if (FLAGS_server) {
    LOG(INFO) << "Running in server mode";
    runServer();
  } else {
    LOG(INFO) << "Running in client mode";
    runClient();
  }

  return 0;
}

void runServer() {
  io_service ioService;

  // accept connections on the first resolved address
  ip::tcp::acceptor acceptor(
      ioService, ip::tcp::endpoint(ip::tcp::v4(), FLAGS_port));
  const auto localEndpoint = acceptor.local_endpoint();
  LOG(INFO)
      << "Server is running at "
      << localEndpoint.address() << ":" << localEndpoint.port();

  // keep processing connections forever
  for (;;) {
    // setup a socket and wait on a client connection
    ip::tcp::socket socket(ioService);

    LOG(INFO) << "Waiting for client to connect";
    acceptor.accept(socket);

    // log the address of the remote client
    const auto remoteEndpoint = socket.remote_endpoint();
    LOG(INFO)
        << "Connected to client ("
        << remoteEndpoint.address() << ":" << remoteEndpoint.port() << ")";

    // wait for a message from the client
    LOG(INFO) << "Waiting for message from client";
    boost::asio::streambuf rcvBuffer;
    const auto filename = readUntilDelimiter(socket, rcvBuffer);
    LOG(INFO)
        << "Message received from client (should be a filename) = "
        << filename.empty() ? ("(empty)") : filename;

    // the message should be a filename for us to read from
    string inputFileBuf;
    ifstream inputFile(filename, ios::in | ios::binary);
    if (inputFile.is_open()) {
      LOG(INFO) << "Opened file \"" << filename << "\"";
      inputFileBuf = string(
          istreambuf_iterator<char>(inputFile),
          istreambuf_iterator<char>());
    } else {
      LOG(INFO) << "Unable to open file \"" << filename << "\"";
    }
    inputFile.close();

    // first send a message with the # of bytes in the file and a delimiter
    // then send the actual bytes in the file (no delimiter)
    // if we weren't able to read the file, inputFileBuf will be empty
    sendBytes(socket, to_string(inputFileBuf.size()) + "#");
    sendBytes(socket, inputFileBuf);
    LOG(INFO)
        << "Sent header + " << inputFileBuf.size()
        << " bytes of data to client";

    // we're done
    LOG(INFO) << "Disconnected client";
  }
}

void runClient() {
  // create local variables for readability
  const auto filename = FLAGS_filename;
  const auto remoteIp = FLAGS_ip_address;
  const auto remotePort = FLAGS_port;

  // verify that the filename is not empty
  if (filename.empty()) {
    LOG(FATAL) << "Filename that client is requesting must be set";
  }

  // create an ASIO instance and a socket
  io_service ioService;
  ip::tcp::socket socket(ioService);

  // connect the socket to the remote system
  LOG(INFO) << "Connecting to " << remoteIp << ":" << remotePort;
  ip::tcp::endpoint endpoint(ip::address::from_string(remoteIp), remotePort);
  boost::system::error_code error;
  socket.connect(endpoint, error);
  if (error) {
    LOG(FATAL)
        << "Connection error: "
        << boost::system::system_error(error).what();
  }
  LOG(INFO) << "Connected to remote endpoint";

  // send the filename that we want to receive
  LOG(INFO) << "Requesting file " << filename;
  sendBytes(socket, filename + "#");

  // listen for a message containing the # of bytes the server is sending
  boost::asio::streambuf rcvBuffer;
  const auto header = readUntilDelimiter(socket, rcvBuffer);
  int numBytes = 0;
  try {
    numBytes = stoi(header);
  } catch (const invalid_argument& ia) {
     LOG(FATAL)
        << "Invalid header (" << header << "), cannot convert to numBytes";
  }
  if (numBytes < 0) {
    LOG(FATAL) << "Invalid header, numBytes = " << numBytes;
  }
  if (numBytes == 0) {
    LOG(FATAL) << "Server is returning 0 bytes (maybe could not find file)";
  }
  LOG(INFO) << "Server is responding with " << numBytes << " bytes";

  // receive the specified # of bytes
  const auto outputFileBuf = readBytes(socket, rcvBuffer, numBytes);

  // write the bytes to a file, appended with current timestamp
  const auto now = chrono::system_clock::now();
  const auto timeSinceEpoch =
      chrono::duration_cast<chrono::seconds>(now.time_since_epoch());
  const string localFilename =
      filename + "." + to_string(timeSinceEpoch.count());
  ofstream outputFile(
      localFilename, ios::out | ios::binary | ios::trunc);
  if (outputFile.is_open()) {
    LOG(INFO) << "Saving to file \"" << localFilename << "\"";
    outputFile << outputFileBuf;
    outputFile.close();
    LOG(INFO)
        << "Wrote " << outputFileBuf.size()
        << " bytes to file \"" << localFilename << "\"";
  } else {
    LOG(INFO) << "Unable to save to file \"" << filename << "\"";
  }
}

/**
 * Send bytes onto the socket.
 */
void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const string& message) {
  boost::system::error_code error;
  write(socket, buffer(message), transfer_all(), error);
  if (error) {
    LOG(FATAL)
        << "Send error: "
        << boost::system::system_error(error).what();
  }
}

/**
 * Read from a socket up until a delimiter.
 *
 * Note that this function may actually overread from the socket. Left over
 * bytes will be kept in the streambuf object that is passed in.
 */
string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer) {
  // blocking read on the socket until the delimiter
  boost::system::error_code error;
  const auto bytesTransferred = read_until(
      socket, rcvBuffer, kDelimiter, error);
  if (error) {
    LOG(FATAL)
        << "Read error: "
        << boost::system::system_error(error).what();
  }

  // read_until may read more data into the buffer (past our delimiter)
  // so we need to extract based on the bytesTransferred value
  const string readString(
      buffers_begin(rcvBuffer.data()),
      buffers_begin(rcvBuffer.data()) +
      bytesTransferred - kDelimiter.length());
  // consume all of the data (including the delimiter) from our read buffer
  rcvBuffer.consume(bytesTransferred);
  return readString;
}

/**
 * Read the specified number of bytes from socket or buffer.
 *
 * The buffer will be drained first before any new bytes are pulled from the
 * socket. This ensures that we cleanup / recoup any existing bytes from the
 * buffer. See note on read_until below.
 */
string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& readBuffer,
    const int numBytesToRead) {
  // When using read_until with a delimiter, more bytes than necessary may be
  // read into the streambuf. For instance, if we want to read until a "#", and
  // the stream is "abcde#fghij", the buffer may actually end up containing
  // "abcde#fg" (two more bytes than what we needed to capture to get the
  // delimiter). read_until automatically takes care of this when called again
  // (it checks if the streambuf already has an instance of the delimiter).
  //
  // However, this "overreading" becomes a problem if we subsequently call
  // "read" with a "transfer_exactly" parameter. For instance, we may expect
  // 64 bytes (due to a variable in a header, such as CONTENT LENGTH). If the
  // buffer already contains two bytes (due to the previous overread), then
  // we'll end up waiting forever (as the server will only send 62 more bytes),
  // or we'll end up with a corrupt message that contains two bytes of the next
  // message.
  //
  // To handle this, we first check how many bytes the buffer contains, and
  // subtract this from the number that we need to wait for. It's possible that
  // the buffer already contains everything, in which case we don't need to call
  // read() at all.

  // check how many bytes the buffer already contains
  const int numBytesInBuffer = readBuffer.size();
  const int numBytesToReadFromSocket = numBytesToRead - numBytesInBuffer;

  // we may not even need to read if the buffer already contains enough data
  if (numBytesToReadFromSocket > 0) {
    // blocking read on the socket until the delimiter
    boost::system::error_code error;
    read(
        socket, readBuffer,
        boost::asio::transfer_exactly(numBytesToReadFromSocket), error);
    if (error) {
      LOG(FATAL)
          << "Read error: "
          << boost::system::system_error(error).what();
    }
  }

  // extract numBytesToRead from buffer into string and remove it from buffer
  // buffer could contain more data -- depends on previous read operations
  const string readBytes(
      buffers_begin(readBuffer.data()),
      buffers_begin(readBuffer.data()) +
      numBytesToRead);
  readBuffer.consume(numBytesToRead);
  return readBytes;
}
