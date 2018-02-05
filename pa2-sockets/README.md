# Socket Programming Example for PA2/PA3

Example client / server in C++ using `Boost::Asio`.

## Instructions

Install dependencies:
```
sudo apt-get install build-essential git libboost-all-dev libgoogle-glog-dev libgflags-dev
```

Build:
```
make
```

Run server:
```
./pa2 -server -port {PORT_NUMBER}
```

Run client:
```
./pa2 -port {PORT_NUMBER} -message={MESSAGE}
```

Header mode:

You can run the client and server in //header mode// by including
`--header_mode` in commands (shown below, by default header mode is disabled).
Both the client and server must be running in the same mode!

In header mode, each message has a "header" that specifies the number of bytes
in the body of the message. The header ends with a delimiter ("#"). After the
header is sent, the actual body / data of the message is sent.

When sending a message:
  - Determine the number of bytes in the string (e.g., string length) and then
    send a message with "<length>#"
  - Send the actual message

When receiving a message in header mode:
  - Wait for a message that ends with a delimiter ("#")
  - Convert the message to an integer. This is the number of bytes in the
    message body.
  - Wait for the number of bytes from the socket.

Running in header mode:
```
# server
./pa2 -server -port {PORT_NUMBER} -header_mode

# client
./pa2 -port {PORT_NUMBER} -header_mode -message={MESSAGE}
```
