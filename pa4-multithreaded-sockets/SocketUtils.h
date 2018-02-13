#include <string>
#include <boost/asio.hpp>

/**
 * Send bytes onto the socket.
 */
void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message,
    boost::system::error_code& error);

/**
 * Send bytes onto the socket.
 *
 * Same as sendBytes(3), but calls LOG(FATAL) on an error.
 */
void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message);

/**
 * Read from a socket up until a delimiter.
 *
 * Note that this function may actually overread from the socket. Left over
 * bytes will be kept in the streambuf object that is passed in.
 */
std::string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const std::string& delimiter,
    boost::system::error_code& error);

/**
 * Read from a socket up until a delimiter.
 *
 * Same as readUntilDelimiter(4), but calls LOG(FATAL) on an error.
 */
std::string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const std::string& delimiter);

/**
 * Read the specified number of bytes from socket or buffer.
 *
 * The buffer will be drained first before any new bytes are pulled from the
 * socket. This ensures that we cleanup / recoup any existing bytes from the
 * buffer. See note on read_until below.
 */
std::string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const int numBytesToRead,
    boost::system::error_code& error);

/**
 * Read the specified number of bytes from socket or buffer.
 *
 * Same as readBytes(4), but calls LOG(FATAL) on an error.
 */
std::string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const int numBytesToRead);
