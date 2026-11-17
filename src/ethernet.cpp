#include "ethernet.h"

#include <iomanip>
#include <sstream>

// 按 Ethernet II 固定偏移提取目的 MAC(0-5)、源 MAC(6-11)、Type/Length(12-13) 和偏移 14 字节。
// Type/Length 按网络字节序组合为 16 位值。
// 先验证指针和捕获长度，任何校验失败都返回原因且不读取帧内容。
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

// 将 6 字节 MAC 地址格式化为大写、冒号分隔的十六进制字符串。
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
