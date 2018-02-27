#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

// value used as delimiter / for marking the end of a message
const std::string kDelimiter = "#";

/**
 * Struct used to track each client's request.
 */
struct ClientRequestInfo {
  // filename requested
  std::string filename;

  // bytes transferred
  uint64_t bytesTransferred = 0;

  // total bytes to send
  uint64_t bytesToTransfer = 0;
};

/**
 * Struct used to track each client's connection.
 */
struct ClientConnection {
  ClientConnection(
      const int clientId, boost::asio::ip::tcp::socket clientSocket)
      : clientId(clientId),
        socket(std::move(clientSocket)) {}

  // client ID
  const int clientId;

  // client request information
  ClientRequestInfo clientRequestInfo;

  // hold this mutex when accessing clientRequestInfo
  std::mutex clientRequestInfoMutex;

  // client socket
  boost::asio::ip::tcp::socket socket;

  // hold this mutex when calling shutdown or close on a socket
  std::mutex socketStateMutex;
};

/**
 * Server class manages socket and client connections.
 */
class Server {
 public:
  Server();

  /**
   * Starts a server process listening on the given endpoint.
   *
   * This is a blocking call.
   */
  void run(const boost::asio::ip::tcp::endpoint& endpoint);
  // void run(const int32_t port);

  /**
   * Stops the server server process.
   *
   * Closes the acceptor and all outstanding threads.
   */
  void stop();

  /**
   * Return client IDs for all connected clients.
   */
  std::vector<int> getConnectedClients();

  /**
   * Return map of client ID -> ClientRequestInfo for all connected clients.
   *
   * The ClientRequestInfo contains information about the client's request at
   * the time that the structure was captured.
   *
   * Ordered by client ID integer (via std::map).
   */
  std::map<int, ClientRequestInfo> getConnectedClientsWithInfo();

  /**
   * Disconnect the client with the specified ID.
   *
   * Returns whether the client was successfully disconnected.
   * (Fails if no client exists for the client ID).
   */
  bool disconnectClient(const int clientId);

 private:
  /**
   * Handle a client connection.
   *
   * This function will block until the client disconnects. It should be called
   * from within its own thread if multiple clients need to be handled in
   * parallel.
   */
  void handleClient(std::shared_ptr<ClientConnection> clientConn);

  /**
   * Return the next client ID, incrementing the client ID in parallel.
   */
  int getNextClientID();

  // atomic integer holding next client ID
  std::atomic<int> nextClientID_;

  // all client threads
  std::vector<std::thread> clientThreads_;

  // all client connections
  std::unordered_map<int, std::shared_ptr<ClientConnection>> clientConnections_;

  // mutex used to protect client connections map
  std::mutex clientConnectionsMutex_;

  // io_service and acceptor objects
  // we store the acceptor at the class level so we can call close
  boost::asio::io_service ioService_;
  boost::asio::ip::tcp::acceptor acceptor_;
};
