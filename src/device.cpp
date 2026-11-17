#include "device.h"

#include <iostream>

// 调用 Npcap 枚举设备；失败时复制错误文本并清理可能已分配的链表。
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

// 遍历 Npcap 设备链表并返回设备数量，用于校验用户选择的编号范围。
std::size_t countDevices(const pcap_if_t* devices) {
    std::size_t count = 0;
    for (const pcap_if_t* device = devices; device != nullptr; device = device->next) {
        ++count;
    }
    return count;
}

// 按本次枚举顺序显示设备编号、描述和传给 pcap_open_live 的内部名称。
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

// 使用从 1 开始的编号定位设备；编号为 0 或超出链表时返回空指针。
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
