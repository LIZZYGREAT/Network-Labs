CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
LDFLAGS ?= -pthread

COMMON_SOURCES := src/protocol.cpp

.PHONY: all server client clean

all: server client

server: src/server.cpp $(COMMON_SOURCES) src/protocol.h
	$(CXX) $(CXXFLAGS) src/server.cpp $(COMMON_SOURCES) $(LDFLAGS) -o $@

client: src/client.cpp $(COMMON_SOURCES) src/protocol.h
	$(CXX) $(CXXFLAGS) src/client.cpp $(COMMON_SOURCES) $(LDFLAGS) -o $@

clean:
	rm -f server client
