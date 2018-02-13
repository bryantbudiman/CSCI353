#include "Server.h"

#include <iomanip>
#include <iostream>
#include <fstream>
#include <glog/logging.h>
#include <boost/scope_exit.hpp>

#include "SocketUtils.h"

Server::Server()
  : nextClientID_(1) {}

void Server::run(int32_t port) {
  // start an IO service, accept connections on all addresses
  boost::asio::io_service ioService;
  boost::asio::ip::tcp::acceptor acceptor(
      ioService,
      boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));

  // keep processing connections forever
  for (;;) {
    // setup a socket and wait on a client connection
    // we'll block on acceptor.accept(socket) until a new client arrives...
    boost::asio::ip::tcp::socket socket(ioService);
    LOG(INFO) << "Waiting for client to connect";
    acceptor.accept(socket);

    // a client has connected
    LOG(INFO) << "Processing new client connection";

    // determine the client's ID and create a ClientConnection object
    //
    // we use a shared_ptr so that both the client's thread and the main server
    // thread can have access to the ClientConnection object and its socket
    const auto clientId = getNextClientID();
    const auto clientConn =
        std::make_shared<ClientConnection>(clientId, std::move(socket));

    // add it to our map of clientId -> ClientConnection object
    {
      std::lock_guard<std::mutex> guard(clientConnectionsMutex_);
      clientConnections_.emplace(clientId, clientConn);
    }

    // create a std::thread object that executes Server::handleClient
    // we pass in `this` because we need to pass in the instance of the Server
    // class to call a class function, and the clientConn structure
    clientThreads_.emplace_back(
        std::thread(&Server::handleClient, this, clientConn));
  }
}

std::vector<int> Server::getConnectedClients() {
  std::vector<int> clientIds;
  {
    std::lock_guard<std::mutex> guard(clientConnectionsMutex_);
    for (const auto& kv : clientConnections_) {
      const auto& clientId = kv.first;
      clientIds.push_back(clientId);
    }
  }
  return clientIds;
}

bool Server::disconnectClient(const int clientId) {
  // try to find a ClientConnection for the given clientId
  std::shared_ptr<ClientConnection> clientConn = nullptr;
  {
    std::lock_guard<std::mutex> guard(clientConnectionsMutex_);
    const auto& kv = clientConnections_.find(clientId);
    if (kv != clientConnections_.end()) {
      clientConn = kv->second;
    }
  }

  // we cannot find a ClientConnection for the given clientId
  if (not clientConn) {
      return false;
  }

  // found a ClientConnection, call shutdown
  {
    std::lock_guard<std::mutex> guard(clientConn->socketStateMutex);
    // it's possible the socket has already been closed, so check first
    if (clientConn->socket.is_open()) {
      clientConn->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    }
  }

  return true;
}

void Server::handleClient(std::shared_ptr<ClientConnection> clientConn) {
  // capture the client ID and create a string for logging
  const auto& clientId = clientConn->clientId;
  std::string clientIdStr = "CID=" + std::to_string(clientId) + "|";

  // we want to make sure we clean up the connection, no matter where we return
  // in this function (from an error, or at the end after sending the file)
  //
  // to accomplish this, we use BOOST_SCOPE_EXIT
  // the logic in the SCOPE exit function will be executed when this function
  // returns, ensuring that we cleanup
  BOOST_SCOPE_EXIT(&clientConn, &clientIdStr, &clientId, this_) {
    // start to disconnect the client
    LOG(INFO) << clientIdStr << "Cleaning up for client ID " << clientId;

    // close the socket
    // scope for lock on mutex for socket state
    {
      std::lock_guard<std::mutex> guard(clientConn->socketStateMutex);
      clientConn->socket.close();
    }

    // remove ourselves from the list of connections
    // add it to our map of clientId -> ClientConnection object
    {
      std::lock_guard<std::mutex> guard(this_->clientConnectionsMutex_);
      this_->clientConnections_.erase(clientId);
    }

    // we're done
    LOG(INFO) << clientIdStr << "Exiting handler for client ID " << clientId;
  } BOOST_SCOPE_EXIT_END

  // log the address of the remote client
  const auto remoteEndpoint = clientConn->socket.remote_endpoint();
  LOG(INFO)
      << clientIdStr
      << "Connected to client ID "
      << clientId
      << " ("
      << remoteEndpoint.address() << ":" << remoteEndpoint.port() << ")";

  // we'll reuse the same error_code object and keep checking for errors
  boost::system::error_code error;

  // wait for a message from the client
  LOG(INFO) << clientIdStr << "Waiting for message from client";
  boost::asio::streambuf rcvBuffer;
  const auto filename =
      readUntilDelimiter(clientConn->socket, rcvBuffer, kDelimiter, error);
  if (error) {
    LOG(ERROR)
        << clientIdStr
        << "Read error: "
        << boost::system::system_error(error).what();
    return;
  }
  LOG(INFO)
      << clientIdStr
      << "Message received from client (should be a filename) = "
      << filename.empty() ? ("(empty)") : filename;

  // the message should be a filename for us to read from
  std::string inputFileBuf;
  std::ifstream inputFile(filename, std::ios::in | std::ios::binary);
  if (inputFile.is_open()) {
    LOG(INFO) << clientIdStr << "Opened file \"" << filename << "\"";
    inputFileBuf = std::string(
        std::istreambuf_iterator<char>(inputFile),
        std::istreambuf_iterator<char>());
  } else {
    LOG(INFO) << clientIdStr << "Unable to open file \"" << filename << "\"";
  }
  inputFile.close();

  // first send a message with the number of bytes in the file and a delimiter
  // then send the actual bytes in the file (no delimiter)
  // if we weren't able to read the file, inputFileBuf will be empty
  //
  // TODO(PA4): Add logic to rate limit based on token bucket
  sendBytes(
      clientConn->socket,
      std::to_string(inputFileBuf.size()) + kDelimiter, error);
  if (error) {
    LOG(ERROR)
        << clientIdStr
        << "Write error: "
        << boost::system::system_error(error).what();
    return;
  }
  sendBytes(clientConn->socket, inputFileBuf, error);
  if (error) {
    LOG(ERROR)
        << clientIdStr
        << "Write error: "
        << boost::system::system_error(error).what();
    return;
  }
  LOG(INFO)
      << clientIdStr
      << "Sent header + " << inputFileBuf.size()
      << " bytes of data to client";

  // our BOOST_SCOPE_EXIT will clean up
}

int Server::getNextClientID() {
  // atomically increment value and return previous value
  return nextClientID_.fetch_add(1);
}
