#include "device.h"

#include <iostream>

bool enumerateDevices(pcap_if_t*& devices, std::string& error) {
    char errorBuffer[PCAP_ERRBUF_SIZE]{};
    devices = nullptr;
    if (pcap_findalldevs(&devices, errorBuffer) == -1) {
        error = errorBuffer;
        if (devices != nullptr) {
            pcap_freealldevs(devices);
            devices = nullptr;
        }
        return false;
    }
    error.clear();
    return true;
}

std::size_t countDevices(const pcap_if_t* devices) {
    std::size_t count = 0;
    for (const pcap_if_t* device = devices; device != nullptr; device = device->next) {
        ++count;
    }
    return count;
}

void printDevices(const pcap_if_t* devices) {
    if (devices == nullptr) {
        std::cout << "No capture devices were found.\n";
        return;
    }

    std::size_t index = 1;
    for (const pcap_if_t* device = devices; device != nullptr;
         device = device->next, ++index) {
        const char* description =
            device->description != nullptr && device->description[0] != '\0'
                ? device->description
                : "(no description)";
        std::cout << index << ". " << description << "\n"
                  << "   Name: " << (device->name != nullptr ? device->name : "(unknown)")
                  << "\n";
    }
}

const pcap_if_t* deviceAt(const pcap_if_t* devices, std::size_t oneBasedIndex) {
    if (oneBasedIndex == 0) {
        return nullptr;
    }
    const pcap_if_t* device = devices;
    for (std::size_t index = 1; device != nullptr && index < oneBasedIndex; ++index) {
        device = device->next;
    }
    return device;
}
