#include "capture.h"

#include "ethernet.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <ctime>

namespace {

struct PcapCloser {
    // 释放捕获句柄，让 unique_ptr 在正常结束和错误返回时统一关闭设备。
    void operator()(pcap_t* handle) const {
        if (handle != nullptr) {
            pcap_close(handle);
        }
    }
};

// 将 Npcap 提供的秒和微秒时间戳转换为本地时分秒及毫秒文本。
std::string formatTimestamp(const timeval& timestamp) {
    const std::time_t seconds = static_cast<std::time_t>(timestamp.tv_sec);
    const std::tm* local = std::localtime(&seconds);
    std::ostringstream output;
    if (local != nullptr) {
        output << std::put_time(local, "%H:%M:%S");
    } else {
        output << timestamp.tv_sec;
    }
    output << '.' << std::setfill('0') << std::setw(3)
           << (timestamp.tv_usec / 1000);
    return output.str();
}

// 输出一条已解析帧的编号、时间、四项实验字段和捕获长度。
void printFrame(std::size_t frameNumber, const pcap_pkthdr& header,
                const EthernetFields& fields) {
    std::cout << '#' << frameNumber << "  " << formatTimestamp(header.ts)
              << "  Source MAC: " << formatMacAddress(fields.sourceMac)
              << "  Destination MAC: " << formatMacAddress(fields.destinationMac)
              << "  Type/Length: 0x" << std::uppercase << std::hex
              << std::setw(4) << std::setfill('0') << fields.typeOrLength
              << "  First byte after Type/Length: 0x" << std::setw(2)
              << static_cast<unsigned int>(fields.firstByteAfterType)
              << std::dec << std::setfill(' ') << "  Captured length: "
              << header.caplen << " bytes\n";
}

}  // namespace

// 用 pcap_open_live 打开设备，再通过 pcap_datalink 确保收到的是 Ethernet 帧。
// 捕获循环把超时视为继续等待、错误视为失败、数据源结束视为正常退出；短于 15 字节的帧跳过。
// count 为空时持续捕获；有值时，累计显示到指定数量后正常退出，RAII 负责关闭句柄。
int captureDevice(const pcap_if_t* device, std::optional<std::size_t> count) {
    if (device == nullptr || device->name == nullptr) {
        std::cerr << "Invalid capture device.\n";
        return 2;
    }

    char errorBuffer[PCAP_ERRBUF_SIZE]{};
    std::unique_ptr<pcap_t, PcapCloser> capture(
        pcap_open_live(device->name, 65535, 0, 1000, errorBuffer));
    if (!capture) {
        std::cerr << "Could not open capture device: " << errorBuffer << "\n";
        return 1;
    }

    const int dataLinkType = pcap_datalink(capture.get());
    if (dataLinkType != DLT_EN10MB) {
        std::cerr << "Unsupported data-link type " << dataLinkType
                  << "; this parser requires DLT_EN10MB (Ethernet). Choose another device.\n";
        return 1;
    }

    std::cout << "Capturing on: "
              << (device->description != nullptr ? device->description : device->name)
              << "\n"
              << "Link type: DLT_EN10MB (Ethernet); promiscuous mode: off\n"
              << "Stop with Ctrl+C";
    if (count.has_value()) {
        std::cout << "; displaying " << *count << " valid frames";
    }
    std::cout << ".\n";

    std::size_t displayedFrames = 0;
    std::size_t receivedFrames = 0;
    std::size_t skippedShortFrames = 0;
    while (!count.has_value() || displayedFrames < *count) {
        pcap_pkthdr* header = nullptr;
        const u_char* packet = nullptr;
        const int result = pcap_next_ex(capture.get(), &header, &packet);
        if (result == 0) {
            continue;  // Read timeout, not an error.
        }
        if (result == -1) {
            std::cerr << "Capture error: " << pcap_geterr(capture.get()) << "\n";
            return 1;
        }
        if (result == -2) {
            std::cout << "Capture ended by the source.\n";
            break;
        }
        if (result != 1 || header == nullptr || packet == nullptr) {
            std::cerr << "Capture API returned an unexpected packet result.\n";
            return 1;
        }

        ++receivedFrames;
        EthernetFields fields;
        std::string parseError;
        if (!parseEthernetFrame(packet, header->caplen, fields, parseError)) {
            ++skippedShortFrames;
            std::cerr << "Skipping captured frame #" << receivedFrames << " ("
                      << header->caplen << " bytes): " << parseError << "\n";
            continue;
        }

        ++displayedFrames;
        printFrame(displayedFrames, *header, fields);
    }

    std::cout << "Capture finished: " << displayedFrames << " displayed frame(s), "
              << skippedShortFrames << " short frame(s) skipped.\n";
    return 0;
}
