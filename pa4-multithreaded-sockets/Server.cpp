#include "Server.h"

#include <iomanip>
#include <iostream>
#include <fstream>
#include <glog/logging.h>
#include <boost/scope_exit.hpp>

#include "SocketUtils.h"

// The following flag allows us to artificially slow down the transfer
DEFINE_int32(
    send_delay_ms, 10,
    "Number of milliseconds to wait between sending each byte of the file");

Server::Server()
  : nextClientID_(1),
    acceptor_(ioService_) {}

void Server::run(const boost::asio::ip::tcp::endpoint& endpoint) {
  // open the acceptor and bind it to our endpoint (All V4 addresses on system)
  acceptor_.open(endpoint.protocol());
  acceptor_.bind(endpoint);
  acceptor_.listen();

  // keep processing connections until the acceptor is shutdown
  LOG(INFO) << "Entering connection acceptance loop";
  while (acceptor_.is_open()) {
    // Setup a socket and wait on a client connection
    //
    // We'll use `acceptor_.async_accept()` to setup a lambda function that will
    // be called when a new connection is ready to be accepted, or when the
    // acceptor has been closed. This lambda function is called our "handler"
    // function (since it is called to handle new client connections). Normally
    // we would put more of the logic below into the handler function, but we
    // don't do that here primarily for readability and to avoid changing this
    // code too much from previous examples.
    //
    // When we call `ioService_.run()`, the ``io_service` object processes
    // serviceable event handlers, returning once there are no more handlers
    // waiting. This ensures that `run()` will not return until all registered
    // handlers (such as the function we registered for async_accept) have been
    // called.
    //
    // We call `reset` on the `io_service` object because we need to reset its
    // state to be able to call `run()` on it multiple times.
    //
    // Note that the approach shown here is much different than what you would
    // find in Boost::Asio documentation. We choose this method to avoid use of
    // the more sophisticated ASIO functionality and instead have things handled
    // directly by threads.
    boost::asio::ip::tcp::socket socket(ioService_);
    boost::system::error_code acceptorError;
    acceptor_.async_accept(
        socket, [&acceptorError](const boost::system::error_code& error) {
          acceptorError = error;
        }
    );
    LOG(INFO) << "Waiting for client to connect";
    ioService_.reset();
    ioService_.run();

    // our handler function for async_accept must have been called, but it's
    // possible that it was called because the acceptor was closed by
    // Server::stop() or due to some other error
    //
    // check if the async_accept was cancelled or if the acceptor is closed
    if (acceptorError == boost::asio::error::operation_aborted or
        not acceptor_.is_open()) {
      // looks like we need to shutdown -- break out of the loop
      LOG(INFO) << "cancel() called or acceptor closed, breaking out of loop";
      break;
    }

    // otherwise, if there's some other error, print it and also shutdown
    if (acceptorError) {
      LOG(ERROR)
          << "Accept error: "
          << boost::system::system_error(acceptorError).what()
          << ", breaking out of loop";
      break;
    }

    // a client has connected
    // determine the client's ID and create a ClientConnection object
    //
    // we use a shared_ptr so that both the client's thread and the main server
    // thread can have access to the ClientConnection object and its socket
    const auto clientId = getNextClientID();
    const auto clientConn =
        std::make_shared<ClientConnection>(clientId, std::move(socket));
    LOG(INFO) << "Processing new client connection, client ID = " << clientId;

    // add it to our map of clientId -> ClientConnection object
    {
      std::lock_guard<std::mutex> guard(clientConnectionsMutex_);
      clientConnections_.emplace(clientId, clientConn);
    }

    // create a std::thread object that executes Server::handleClient
    // we pass in `this` because we need to pass in the instance of the Server
    // class to call a class function, and the ClientConnection structure
    //
    // TODO: Periodic cleanup of thread objects in clientThreads_...
    clientThreads_.emplace_back(
        std::thread(&Server::handleClient, this, clientConn));
  }
  LOG(INFO) << "Exited connection acceptance loop";

  // disconnect remaining client connections
  //
  // we know that we're not going to accept any more clients, so just call
  // getConnectedClients() to get all clientIds and then call disconnectClient()
  // for each client ID
  LOG(INFO) << "Cleaning up client connections";
  const auto connectedClientIds = getConnectedClients();
  for (const auto& clientId : connectedClientIds) {
    // call disconnect
    LOG(INFO) << "Disconnecting client " << clientId;
    disconnectClient(clientId);
  }

  // wait for all client threads to cleanup by calling join() on each
  LOG(INFO) << "Waiting for client threads to exit";
  for (auto& clientThread : clientThreads_) {
    clientThread.join();
  }

  // we're done -- all client connections were closed and threads joined
  LOG(INFO) << "Finished shutting down server";
}

void Server::stop() {
  // close the acceptor, which will cancel async_accept calls waiting on it
  LOG(INFO) << "Closing acceptor, canceling all pending accept operations";
  acceptor_.close();
  LOG(INFO) << "Acceptor closed";
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

std::map<int, ClientRequestInfo> Server::getConnectedClientsWithInfo() {
  std::map<int, ClientRequestInfo> clientIdToRequestInfo;
  {
    std::lock_guard<std::mutex> guard(clientConnectionsMutex_);
    for (const auto& kv : clientConnections_) {
      const auto& clientId = kv.first;
      const auto& clientConn = kv.second;

      // lock and copy the ClientRequestInfo structure
      {
        std::lock_guard<std::mutex> guard(clientConn->clientRequestInfoMutex);
        clientIdToRequestInfo[clientId] = clientConn->clientRequestInfo;
      }
    }
  }
  return clientIdToRequestInfo;
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

  // update the ClientRequestInfo structure
  {
    std::lock_guard<std::mutex> guard(clientConn->clientRequestInfoMutex);
    clientConn->clientRequestInfo.filename = filename;
    clientConn->clientRequestInfo.bytesToTransfer = inputFileBuf.size();
  }

  // first send a message with the number of bytes in the file and a delimiter
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

  // then send the actual bytes in the file (no delimiter)
  // if we weren't able to read the file, inputFileBuf will be empty
  //
  // we send one byte at a time to slow down the send rate...
  //
  // TODO(PA4): Add logic to rate limit based on token bucket
  for (unsigned int i = 0; i < inputFileBuf.size(); i++) {
    // send a single byte
    sendBytes(clientConn->socket, inputFileBuf.substr(i, 1), error);

    // check for errors -- socket might have been closed while we were writing
    if (error) {
      LOG(ERROR)
          << clientIdStr
          << "Write error: "
          << boost::system::system_error(error).what();
      return;
    }

    // update the ClientRequestInfo structure
    {
      std::lock_guard<std::mutex> guard(clientConn->clientRequestInfoMutex);
      clientConn->clientRequestInfo.bytesTransferred++;
    }

    // wait before sending the next byte
    std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_send_delay_ms));
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
