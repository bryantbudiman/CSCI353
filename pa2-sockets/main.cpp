#include <chrono>
#include <iomanip>
#include <iostream>

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
    message, "",
    "Message for client to send to remote server");
DEFINE_bool(
    header_mode, false,
    "Whether to use a header with the number of bytes as the first message");

// value used as delimiter / for marking the end of a message
const std::string kDelimiter = "#";

void runServer();
void runClient();
void sendMessage(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message);
void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message);
string readMessage(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer);
string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer);
string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const int numBytesToRead);
string getTimestamp();

int main(int argc, char *argv[]) {
  // setup Google logging and flags
  // set logtostderr to true by default, but it can still be overridden by flags
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  LOG(INFO) << "Started pa2";
  LOG(INFO) << "Timestamp: " << getTimestamp();

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

  // resolve my hostname into an IP address
  //
  // flag explanation:
  //   - address_configured (AI_ADDRCONFIG): ignore loopback addrs (127.0.0.1)
  //     Only return IPv4 addresses if a non-loopback IPv4 address is
  //     configured for the system. Only return IPv6 addresses if a non-loopback
  //     IPv6 address is configured for the system.
  //   - numeric_service (AI_NUMERICSERV): service is a interger port #
  //     Service name should be treated as a numeric string defining a port
  //     number and no name resolution should be attempted.
  const auto queryFlags = (
      ip::resolver_query_base::address_configured |
      ip::resolver_query_base::numeric_service);
  const auto hostname = ip::host_name();
  ip::tcp::resolver resolver(ioService);
  ip::tcp::resolver::query query(hostname, to_string(FLAGS_port), queryFlags);
  ip::tcp::resolver::iterator queryIt = resolver.resolve(query);
  ip::tcp::resolver::iterator endIt;
  if (queryIt == endIt) {
    LOG(FATAL) << "Could not resolve local hostname to an IP address";
  }

  // accept connections on the first resolved address
  ip::tcp::acceptor acceptor(ioService, *queryIt);
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
    const auto message = readMessage(socket, rcvBuffer);

    // reverse the message and send it back
    const auto responseMessage = string(message.rbegin(), message.rend());
    sendMessage(socket, responseMessage);
    LOG(INFO) << "Sent message \"" << responseMessage << "\"";

    // we're done
    LOG(INFO) << "Disconnected client";
  }
}

void runClient() {
  // create local variables for readability
  const auto message = FLAGS_message;
  const auto remoteIp = FLAGS_ip_address;
  const auto remotePort = FLAGS_port;

  // verify that the message is not empty
  if (message.empty()) {
    LOG(FATAL) << "Non-empty message must be sent for client mode";
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

  // send a message to the server
  sendMessage(socket, message);
  LOG(INFO) << "Sent message \"" << message << "\"";

  // wait for a reply
  LOG(INFO) << "Waiting for response from server";
  boost::asio::streambuf rcvBuffer;
  const auto responseMessage = readMessage(socket, rcvBuffer);
  LOG(INFO) << "Received message \"" << responseMessage << "\"";
}

/**
 * Send a message, handling header mode or non-header mode.
 */
void sendMessage(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message) {
  // if we're in header_mode, we need to send the following:
  //   - a header (delimiter at the end) with number of bytes in the message
  //   - the actual message to the client (no delimiter at the end)
  //
  // if we're not in header_mode, then we just send a message with a delimiter
  if (FLAGS_header_mode) {
    sendBytes(socket, std::to_string(message.size()) + kDelimiter);
    sendBytes(socket, message);
  } else {
    sendBytes(socket, message + kDelimiter);
  }
}

/**
 * Send bytes onto the socket.
 */
void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message) {
  boost::system::error_code error;
  write(socket, buffer(message), transfer_all(), error);
  if (error) {
    LOG(FATAL)
        << "Send error: "
        << boost::system::system_error(error).what();
  }
}

/**
 * Read a message, handling header-mode or non-header mode.
 */
string readMessage(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer) {
  // if we're in header_mode, we get the following:
  //   - a header (delimiter at the end) with number of bytes in the message
  //   - the actual message from the client (no delimiter at the end)
  //
  // if we're not in header_mode, then we just get a message with a delimiter
  if (not FLAGS_header_mode) {
    return readUntilDelimiter(socket, rcvBuffer);
  }

  // get the header
  const std::string messageHeader = readUntilDelimiter(socket, rcvBuffer);

  // the header message tells us the number of bytes in the message
  int numBytes;
  try {
    numBytes = std::stoi(messageHeader);
  } catch (const std::invalid_argument& ia) {
     LOG(FATAL) << "Invalid header, cannot convert to numBytes";
  }
  if (numBytes <= 0) {
    LOG(FATAL) << "Invalid header, numBytes = " << numBytes;
  }
  LOG(INFO) << "Received message header, message is " << numBytes << " bytes";

  // read in the specified number of bytes
  return readBytes(socket, rcvBuffer, numBytes);
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

/**
 * Return a string containing the current time with millisecond resolution.
 */
string getTimestamp() {
  // get a precise timestamp as a string
  const auto now = std::chrono::system_clock::now();
  const auto nowAsTimeT = std::chrono::system_clock::to_time_t(now);
  const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  std::stringstream nowSs;
  nowSs
      << std::put_time(std::localtime(&nowAsTimeT), "%a %b %d %Y %T")
      << '.' << std::setfill('0') << std::setw(3) << nowMs.count();
  return nowSs.str();
}
