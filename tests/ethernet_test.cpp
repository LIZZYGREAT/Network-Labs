#include "../src/ethernet.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const std::array<std::uint8_t, 15> frame = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  // Destination MAC
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Source MAC
        0x08, 0x00,                          // EtherType, network byte order
        0x45};                               // First byte after Type/Length
    EthernetFields fields;
    std::string error;
    bool passed = true;
    passed &= expect(parseEthernetFrame(frame.data(), frame.size(), fields, error),
                     "15-byte frame parses");
    passed &= expect(fields.destinationMac ==
                         std::array<std::uint8_t, 6>{0x00, 0x11, 0x22, 0x33, 0x44, 0x55},
                     "destination MAC uses offsets 0-5");
    passed &= expect(fields.sourceMac ==
                         std::array<std::uint8_t, 6>{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
                     "source MAC uses offsets 6-11");
    passed &= expect(fields.typeOrLength == 0x0800,
                     "Type/Length is combined in network byte order");
    passed &= expect(fields.firstByteAfterType == 0x45,
                     "byte at offset 14 is read");
    passed &= expect(formatMacAddress(fields.sourceMac) == "AA:BB:CC:DD:EE:FF",
                     "MAC address formatting is uppercase and colon-separated");

    passed &= expect(!parseEthernetFrame(frame.data(), 0, fields, error),
                     "zero-byte frame is rejected");
    passed &= expect(!parseEthernetFrame(frame.data(), 14, fields, error),
                     "14-byte frame is rejected");
    passed &= expect(parseEthernetFrame(frame.data(), 15, fields, error),
                     "15-byte frame is accepted");
    passed &= expect(!parseEthernetFrame(nullptr, 15, fields, error),
                     "null data pointer is rejected");

    if (!passed) {
        return 1;
    }
    std::cout << "Ethernet parser checks passed.\n";
    return 0;
}
