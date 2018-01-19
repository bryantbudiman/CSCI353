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
