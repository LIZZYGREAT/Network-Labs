#include "capture.h"
#include "device.h"

#include <charconv>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct DeviceListDeleter {
    // 作为 unique_ptr 的自定义删除器，确保所有退出路径都会释放 Npcap 设备链表。
    void operator()(pcap_if_t* devices) const {
        if (devices != nullptr) {
            pcap_freealldevs(devices);
        }
    }
};

struct Options {
    bool helpRequested = false;
    bool listOnly = false;
    std::optional<std::size_t> deviceIndex;
    std::optional<std::size_t> count;
};

// 打印支持的命令行参数，供帮助信息和参数错误时复用。
void printUsage(const char* program) {
    std::cout << "Usage: " << program
              << " [--list] [--device-index N] [--count N]\n"
              << "  --list             List available capture devices and exit\n"
              << "  --device-index N   Select the 1-based index from this run's device list\n"
              << "  --count N          Stop after displaying N valid frames\n";
}

// 将十进制字符串严格转换为正整数；拒绝空值、零、尾随字符和溢出。
bool parsePositiveSize(std::string_view value, std::size_t& result) {
    if (value.empty()) {
        return false;
    }
    unsigned long long parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto conversion = std::from_chars(begin, end, parsed, 10);
    if (conversion.ec != std::errc{} || conversion.ptr != end || parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    result = static_cast<std::size_t>(parsed);
    return true;
}

// 逐项读取命令行参数并写入 Options；遇到缺值、非法值或冲突组合时返回失败。
bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--help" || argument == "-h") {
            options.helpRequested = true;
            return true;
        }
        if (argument == "--list") {
            options.listOnly = true;
            continue;
        }
        if (argument == "--device-index" || argument == "--count") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << argument << ".\n";
                return false;
            }
            std::size_t value = 0;
            if (!parsePositiveSize(argv[++i], value)) {
                std::cerr << "Expected a positive integer after " << argument << ".\n";
                return false;
            }
            if (argument == "--device-index") {
                options.deviceIndex = value;
            } else {
                options.count = value;
            }
            continue;
        }
        std::cerr << "Unknown argument: " << argument << "\n";
        return false;
    }

    if (options.listOnly && (options.deviceIndex.has_value() || options.count.has_value())) {
        std::cerr << "--list cannot be combined with --device-index or --count.\n";
        return false;
    }
    return true;
}

// 在交互模式读取设备编号，并反复校验输入直到选择有效设备或用户退出。
bool readDeviceIndex(std::size_t deviceCount, std::size_t& selectedIndex) {
    std::string input;
    while (true) {
        std::cout << "Select device number (1-" << deviceCount << ", or q to quit): ";
        if (!std::getline(std::cin, input)) {
            std::cerr << "Could not read a device selection.\n";
            return false;
        }
        if (input == "q" || input == "Q") {
            return false;
        }
        if (parsePositiveSize(input, selectedIndex) && selectedIndex <= deviceCount) {
            return true;
        }
        std::cerr << "Invalid selection. Enter a listed device number.\n";
    }
}

}  // namespace

// 程序入口：解析参数、枚举设备、确定本次运行中的设备编号并启动捕获。
int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage(argv[0]);
        return 2;
    }
    if (options.helpRequested) {
        printUsage(argv[0]);
        return 0;
    }

    pcap_if_t* rawDevices = nullptr;
    std::string enumerationError;
    if (!enumerateDevices(rawDevices, enumerationError)) {
        std::cerr << "Could not enumerate capture devices: " << enumerationError << "\n";
        return 1;
    }
    std::unique_ptr<pcap_if_t, DeviceListDeleter> devices(rawDevices);

    const std::size_t deviceCount = countDevices(devices.get());
    std::cout << "Npcap: " << pcap_lib_version() << "\n";
    printDevices(devices.get());
    if (options.listOnly) {
        return 0;
    }
    if (deviceCount == 0) {
        std::cerr << "No capture device is available to open.\n";
        return 1;
    }

    std::size_t selectedIndex = 0;
    if (options.deviceIndex.has_value()) {
        selectedIndex = *options.deviceIndex;
        if (selectedIndex > deviceCount) {
            std::cerr << "Device index " << selectedIndex
                      << " is outside the current list (1-" << deviceCount << ").\n";
            return 2;
        }
    } else if (!readDeviceIndex(deviceCount, selectedIndex)) {
        return 0;
    }

    const pcap_if_t* selectedDevice = deviceAt(devices.get(), selectedIndex);
    if (selectedDevice == nullptr) {
        std::cerr << "Selected device is no longer available.\n";
        return 1;
    }

    // The device list remains alive until captureDevice has opened and finished
    // using the selected entry, then its owning unique_ptr releases it.
    return captureDevice(selectedDevice, options.count);  //执行具体逻辑
}
