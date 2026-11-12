#ifndef LAB2_DEVICE_H
#define LAB2_DEVICE_H

#include <pcap.h>

#include <cstddef>
#include <string>

bool enumerateDevices(pcap_if_t*& devices, std::string& error);
std::size_t countDevices(const pcap_if_t* devices);
void printDevices(const pcap_if_t* devices);
const pcap_if_t* deviceAt(const pcap_if_t* devices, std::size_t oneBasedIndex);

#endif  // LAB2_DEVICE_H
