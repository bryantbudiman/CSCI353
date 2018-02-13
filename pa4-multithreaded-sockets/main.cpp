#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "Server.h"
#include "SocketUtils.h"

// Flags used for client and server
DEFINE_int32(
    port, 0,
    "Port number for server / where server is running");
DEFINE_bool(
    server, false,
    "Whether to operate in server or client mode (true = server)");

// Flags only used for client
DEFINE_string(
    ip_address, "127.0.0.1",
    "IP address for server / where server is running");
DEFINE_string(
    filename, "",
    "Filename to capture from remote server");

void runServer();
void runServerTerminal(Server& server);
void runClient();

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
  // create a server object
  Server server;

  // create a thread that calls Server::run
  std::thread serverThread([&server](){
      server.run(FLAGS_port);
  });

  // create a thread with simple logic that operates on the server
  // we need to pass the server by reference, so we use std::ref
  std::thread terminalThread(runServerTerminal, std::ref(server));

  // wait on the terminal thread to exit
  terminalThread.join();

  // TODO: Shutdown the server connections

  // wait on the server thread to exit
  serverThread.join();
}

void runServerTerminal(Server& server) {
  // loop forever
  while (true) {
    std::cout << "#> " << std::flush;
    std::string command;
    std::getline(std::cin, command);

    // tokenize the command line, breaking around whitespace
    std::vector<std::string> commandFields;
    boost::split(commandFields, command, boost::is_any_of(" "));

    // handle empty line
    if (commandFields.empty()) {
      continue;
    }

    // handle "list" command
    if (commandFields[0] == "list") {
      auto clientIds = server.getConnectedClients();

      // sort the client IDs
      std::sort(clientIds.begin(), clientIds.end());

      // print as "Connected clients = 10, 11, 12"
      std::cout << "Connected clients = ";
      bool firstClient = true;
      for (const auto& clientId : clientIds) {
        if (not firstClient) {
          // add separator
          std::cout << ", ";
        }
        std::cout << clientId;
      }
      std::cout << std::endl;
    }

    // handle "list" command
    if (commandFields[0] == "disconnect") {
      // make sure there's a client ID specified
      if (commandFields.size() != 2) {
        std::cout << "Invalid arguments for `disconnect` command" << std::endl;
        continue;
      }

      // convert the client ID to an integer
      const auto clientIdStr = commandFields[1];
      int clientId = 0;
      try {
        clientId = std::stoi(clientIdStr);
      } catch (const std::invalid_argument& ia) {
        std::cout << "Invalid arguments for `disconnect` command" << std::endl;
        continue;
      }

      // call disconnect
      const auto result = server.disconnectClient(clientId);
      if (result) {
        std::cout << "Disconnected client ID " << clientId << std::endl;
      } else {
        std::cout << "Unable to disconnect client ID " << clientId << std::endl;
      }
    }
  }
}

void runClient() {
  const auto remoteIp = FLAGS_ip_address;
  const auto remotePort = FLAGS_port;

  // create an ASIO instance and a socket
  boost::asio::io_service ioService;
  boost::asio::ip::tcp::socket socket(ioService);

  // connect the socket to the remote system
  LOG(INFO) << "Connecting to " << remoteIp << ":" << remotePort;
  boost::asio::ip::tcp::endpoint endpoint(
      boost::asio::ip::address::from_string(remoteIp), remotePort);
  boost::system::error_code error;
  socket.connect(endpoint, error);
  if (error) {
    LOG(FATAL)
        << "Connection error: "
        << boost::system::system_error(error).what();
  }
  LOG(INFO) << "Connected to remote endpoint";

  // let the user specity what file they want
  std::string filename;
  std::cout << "Enter filename to retrieve: " << std::flush;
  std::getline(std::cin, filename);

  // send the filename that we want to receive
  LOG(INFO) << "Requesting file " << filename;
  sendBytes(socket, filename + "#");

  // listen for a message containing the # of bytes the server is sending
  boost::asio::streambuf rcvBuffer;
  const auto header = readUntilDelimiter(socket, rcvBuffer, kDelimiter);
  int numBytes = 0;
  try {
    numBytes = std::stoi(header);
  } catch (const std::invalid_argument& ia) {
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
  const auto now = std::chrono::system_clock::now();
  const auto timeSinceEpoch =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
  const std::string localFilename =
      filename + "." + std::to_string(timeSinceEpoch.count());
  std::ofstream outputFile(
      localFilename, std::ios::out | std::ios::binary | std::ios::trunc);
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

  // done
  LOG(INFO) << "Client exiting";
}
