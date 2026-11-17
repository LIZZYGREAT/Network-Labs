#ifndef LAB2_CAPTURE_H
#define LAB2_CAPTURE_H

#include <pcap.h>

#include <cstddef>
#include <optional>

// 启动指定设备的捕获流程；count 为空时持续捕获，有值时限制成功解析并显示的帧数。
int captureDevice(const pcap_if_t* device, std::optional<std::size_t> count);

#endif  // LAB2_CAPTURE_H
