#ifndef LAB2_ETHERNET_H
#define LAB2_ETHERNET_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

struct EthernetFields {
    std::array<std::uint8_t, 6> destinationMac{};
    std::array<std::uint8_t, 6> sourceMac{};
    std::uint16_t typeOrLength = 0;
    std::uint8_t firstByteAfterType = 0;
};

// Parses the fixed offsets required by the experiment. Returns false when the
// captured buffer is null or shorter than 15 bytes.
bool parseEthernetFrame(const std::uint8_t* bytes, std::size_t capturedLength,
                        EthernetFields& fields, std::string& error);

std::string formatMacAddress(const std::array<std::uint8_t, 6>& address);

#endif  // LAB2_ETHERNET_H
