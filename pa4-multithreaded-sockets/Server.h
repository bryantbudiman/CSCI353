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
 * Struct used to track each client's connection.
 */
struct ClientConnection {
  ClientConnection(
      const int clientId, boost::asio::ip::tcp::socket clientSocket)
      : clientId(clientId),
        socket(std::move(clientSocket)) {}

  // client ID
  const int clientId;

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
   * Starts a server process.
   *
   * This is a blocking call.
   */
  void run(int32_t port);

  /**
   * Return client IDs for all connected clients.
   */
  std::vector<int> getConnectedClients();

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
};
