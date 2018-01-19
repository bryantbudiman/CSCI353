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

// value used as delimiter / for marking the end of a message
const std::string kDelimiter = "#";

void runServer();
void runClient();
string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& readBuffer);
void sendWithDelimiter(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message);

int main(int argc, char *argv[]) {
  // setup Google logging and flags
  // set logtostderr to true by default, but it can still be overridden by flags
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  LOG(INFO) << "Started pa2";

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
  ip::tcp::acceptor acceptor(
      ioService, ip::tcp::endpoint(ip::tcp::v4(), FLAGS_port));
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
    const auto message = readUntilDelimiter(socket, rcvBuffer);

    // reverse the message and send it back
    const auto responseMessage = string(message.rbegin(), message.rend());
    sendWithDelimiter(socket, responseMessage);
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

  // send the message
  sendWithDelimiter(socket, message);
  LOG(INFO) << "Sent message \"" << message << "\"";

  // wait for a reply
  LOG(INFO) << "Waiting for response from server";
  boost::asio::streambuf rcvBuffer;
  const auto responseMessage = readUntilDelimiter(socket, rcvBuffer);
  LOG(INFO) << "Received message \"" << responseMessage << "\"";
}

void sendWithDelimiter(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message) {
  boost::system::error_code error;
  const std::string messageWithDelimiter = message + kDelimiter;
  write(socket, buffer(messageWithDelimiter), transfer_all(), error);
  if (error) {
    LOG(FATAL)
        << "Send error: "
        << boost::system::system_error(error).what();
  }
}

string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& readBuffer) {
  // blocking read on the socket until the delimiter
  boost::system::error_code error;
  const auto bytesTransferred = read_until(
      socket, readBuffer, kDelimiter, error);
  if (error) {
    LOG(FATAL)
        << "Read error: "
        << boost::system::system_error(error).what();
  }

  // read_until may read more data into the buffer (past our delimiter)
  // so we need to extract based on the bytesTransferred value
  const string readString(
      buffers_begin(readBuffer.data()),
      buffers_begin(readBuffer.data()) +
      bytesTransferred - kDelimiter.length());
  // consume all of the data (including the delimiter) from our read buffer
  readBuffer.consume(bytesTransferred);
  return readString;
}
