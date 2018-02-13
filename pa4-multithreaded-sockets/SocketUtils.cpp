#include "SocketUtils.h"

#include <glog/logging.h>

void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message,
    boost::system::error_code& error) {
  boost::asio::write(
      socket, boost::asio::buffer(message), boost::asio::transfer_all(), error);
  if (error) {
    // error during read, return
    // the caller should check error before acting on the return value
    return;
  }
}


void sendBytes(
    boost::asio::ip::tcp::socket& socket,
    const std::string& message) {
  boost::system::error_code error;
  sendBytes(socket, message, error);
  if (error) {
    LOG(FATAL)
        << "Send error: "
        << boost::system::system_error(error).what();
  }
}


std::string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const std::string& delimiter,
    boost::system::error_code& error) {
  // blocking read on the socket until the delimiter
  const auto bytesTransferred =
      boost::asio::read_until(socket, rcvBuffer, delimiter, error);
  if (error) {
    // error during read, return empty string
    // the caller should check error before acting on the return value
    return "";
  }

  // read_until may read more data into the buffer (past our delimiter)
  // so we need to extract based on the bytesTransferred value
  const std::string readString(
      boost::asio::buffers_begin(rcvBuffer.data()),
      boost::asio::buffers_begin(rcvBuffer.data()) +
      bytesTransferred - delimiter.length());

  // consume all of the data (including the delimiter) from our read buffer
  // there may be additional data left in the buffer, see readBytes comments
  rcvBuffer.consume(bytesTransferred);
  return readString;
}


std::string readUntilDelimiter(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const std::string& delimiter) {
  boost::system::error_code error;
  const auto rcvStr = readUntilDelimiter(socket, rcvBuffer, delimiter, error);
  if (error) {
    LOG(FATAL)
        << "Read error: "
        << boost::system::system_error(error).what();
  }
  return rcvStr;
}


std::string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const int numBytesToRead,
    boost::system::error_code& error) {
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
  const int numBytesInBuffer = rcvBuffer.size();
  const int numBytesToReadFromSocket = numBytesToRead - numBytesInBuffer;

  // we may not even need to read if the buffer already contains enough data
  if (numBytesToReadFromSocket > 0) {
    // blocking read on the socket until the delimiter
    boost::asio::read(
        socket, rcvBuffer,
        boost::asio::transfer_exactly(numBytesToReadFromSocket), error);
    if (error) {
      // error during read, return empty string
      // the caller should check error before acting on the return value
      return "";
    }
  }

  // extract numBytesToRead from buffer into string and remove it from buffer
  // buffer could contain more data -- depends on previous read operations
  //
  // consume(x) will remove x bytes from the buffer
  const std::string readBytes(
      boost::asio::buffers_begin(rcvBuffer.data()),
      boost::asio::buffers_begin(rcvBuffer.data()) +
      numBytesToRead);
  rcvBuffer.consume(numBytesToRead);
  return readBytes;
}


std::string readBytes(
    boost::asio::ip::tcp::socket& socket,
    boost::asio::streambuf& rcvBuffer,
    const int numBytesToRead) {
  boost::system::error_code error;
  const auto rcvStr = readBytes(socket, rcvBuffer, numBytesToRead, error);
  if (error) {
    LOG(FATAL)
        << "Read error: "
        << boost::system::system_error(error).what();
  }
  return rcvStr;
}
