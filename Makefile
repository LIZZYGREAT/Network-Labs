CXX = x86_64-w64-mingw32-g++
HOST_CXX ?= g++
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SDK_DIR ?= $(abspath $(MAKEFILE_DIR)/../tools/npcap-sdk-1.17)
BUILD_DIR ?= build/lab2
TARGET := $(BUILD_DIR)/npcap_capture.exe
PARSER_TEST := $(BUILD_DIR)/ethernet_test
SOURCES := src/main.cpp src/device.cpp src/capture.cpp src/ethernet.cpp
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
CPPFLAGS := -I$(SDK_DIR)/Include -Isrc
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
WINDOWS_FLAGS := -static -static-libgcc -static-libstdc++
LDLIBS := $(SDK_DIR)/Lib/x64/wpcap.lib -lws2_32

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(WINDOWS_FLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

$(PARSER_TEST): tests/ethernet_test.cpp src/ethernet.cpp src/ethernet.h | $(BUILD_DIR)
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Wpedantic -O2 -Isrc -o $@ tests/ethernet_test.cpp src/ethernet.cpp

test: $(PARSER_TEST)
	./$(PARSER_TEST)

clean:
	rm -f $(OBJECTS) $(TARGET) $(PARSER_TEST)
