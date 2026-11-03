#ifndef LAB2_CAPTURE_H
#define LAB2_CAPTURE_H

#include <pcap.h>

#include <cstddef>
#include <optional>

// A missing count means capture until pcap reports an error/end or the user
// interrupts the process. A count limits successfully parsed and displayed frames.
int captureDevice(const pcap_if_t* device, std::optional<std::size_t> count);

#endif  // LAB2_CAPTURE_H
