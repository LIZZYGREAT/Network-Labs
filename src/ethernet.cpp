#include "ethernet.h"

#include <iomanip>
#include <sstream>

bool parseEthernetFrame(const std::uint8_t* bytes, std::size_t capturedLength,
                        EthernetFields& fields, std::string& error) {
    constexpr std::size_t kRequiredLength = 15;
    if (bytes == nullptr) {
        error = "packet data pointer is null";
        return false;
    }
    if (capturedLength < kRequiredLength) {
        error = "captured frame is shorter than 15 bytes";
        return false;
    }

    for (std::size_t i = 0; i < fields.destinationMac.size(); ++i) {
        fields.destinationMac[i] = bytes[i];
        fields.sourceMac[i] = bytes[i + 6];
    }
    fields.typeOrLength = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[12]) << 8) |
        static_cast<std::uint16_t>(bytes[13]));
    fields.firstByteAfterType = bytes[14];
    error.clear();
    return true;
}

std::string formatMacAddress(const std::array<std::uint8_t, 6>& address) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < address.size(); ++i) {
        if (i != 0) {
            output << ':';
        }
        output << std::setw(2) << static_cast<unsigned int>(address[i]);
    }
    return output.str();
}
