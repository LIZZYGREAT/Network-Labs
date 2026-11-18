#ifndef LAB2_ETHERNET_H
#define LAB2_ETHERNET_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

//定义要求的数据结构 6目的Mac+6源Mac+2Type+首字节
struct EthernetFields {
    std::array<std::uint8_t, 6> destinationMac{};
    std::array<std::uint8_t, 6> sourceMac{};
    std::uint16_t typeOrLength = 0;
    std::uint8_t firstByteAfterType = 0;
};

// 按实验要求的固定偏移解析帧字段；数据为空或捕获长度不足 15 字节时返回 false。
bool parseEthernetFrame(const std::uint8_t* bytes, std::size_t capturedLength,
                        EthernetFields& fields, std::string& error);

std::string formatMacAddress(const std::array<std::uint8_t, 6>& address);

#endif  // LAB2_ETHERNET_H
