# Socket Programming Example with File Transfer for PA2/PA3

Example client / server in C++ using `Boost::Asio`.

Includes transfer of binary files.

## Overview

This is a slight variant of the programming example in `pa2-sockets`. In this
example, clients connect and request a file from a server. The server tries to
find the file, and then sends back a header (containing the amount of data the
server is going to send) and the file data itself.

If the server cannot find the file, the header will indicate 0 bytes are going
to be sent. When the client receives the data, it saves it into a file called
`{filename}.{timestamp}`, where `{timestamp}` is the number of seconds since
epoch.

This program has been tested with linux.jpg to ensure that transfer of binary
files works as expected.

## Difference between this and PA2/PA3

In this example, the "header" is extremely simple:
  - for client requests, the header is just a filename followed by a delimiter
  - for server responses, the header is just an integer followed by a delimiter

For the HTTP server / client in PA2/PA3, you need to handle more complex
headers, and also need to be able to handle offsets, MIME types, and other
information in the URL. This sample also does not properly prevent the
client from escaping from the server root / other features.

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
./pa2 -port {PORT_NUMBER} -filename={FILENAME}
```

## Example output

Server side:
```
bschlinker@cs353:~/cs353/pa2-filetransfer$ ./pa2 --port 5000 --server
I0205 22:32:31.740988 16183 main.cpp:47] Started pa2 - file transfer edition
I0205 22:32:31.741109 16183 main.cpp:58] Running in server mode
I0205 22:32:31.741212 16183 main.cpp:75] Server is running at 0.0.0.0:5000
I0205 22:32:31.741248 16183 main.cpp:84] Waiting for client to connect
I0205 22:32:34.881948 16183 main.cpp:89] Connected to client (127.0.0.1:36058)
I0205 22:32:34.882009 16183 main.cpp:94] Waiting for message from client
I0205 22:32:34.882078 16183 main.cpp:97] Message received from client (should be a filename) = 0
I0205 22:32:34.882143 16183 main.cpp:105] Opened file "linux.jpg"
I0205 22:32:34.886565 16183 main.cpp:119] Sent header + 37097 bytes of data to client
I0205 22:32:34.886585 16183 main.cpp:124] Disconnected client
I0205 22:32:34.886636 16183 main.cpp:84] Waiting for client to connect
```

Client side:
```
bschlinker@cs353:~/cs353/pa2-filetransfer$ ./pa2 --port 5000 --filename=linux.jpg
I0205 22:32:34.881494 16188 main.cpp:47] Started pa2 - file transfer edition
I0205 22:32:34.881626 16188 main.cpp:61] Running in client mode
I0205 22:32:34.881688 16188 main.cpp:144] Connecting to 127.0.0.1:5000
I0205 22:32:34.881886 16188 main.cpp:153] Connected to remote endpoint
I0205 22:32:34.881901 16188 main.cpp:156] Requesting file linux.jpg
I0205 22:32:34.886713 16188 main.cpp:175] Server is responding with 37097 bytes
I0205 22:32:34.889382 16188 main.cpp:189] Saving to file "linux.jpg.1517869954"
I0205 22:32:34.889595 16188 main.cpp:192] Wrote 37097 bytes to file "linux.jpg.1517869954"
```

Confirmation the two files are equal:
```
bschlinker@cs353:~/cs353/pa2-filetransfer$ cmp -b linux.jpg linux.jpg.1517869954
bschlinker@cs353:~/cs353/pa2-filetransfer$ echo $?
0
```
