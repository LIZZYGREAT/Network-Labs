#include "protocol.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace chat {
namespace {

struct ExactResult {
    RecvStatus status;
    int error_number;
};

ExactResult recv_exact(int fd, void* destination, std::size_t length) {
    auto* bytes = static_cast<unsigned char*>(destination);
    std::size_t offset = 0;

    while (offset < length) {
        const ssize_t count = ::recv(fd, bytes + offset, length - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0) {
            return {offset == 0 ? RecvStatus::Eof : RecvStatus::Truncated, 0};
        }
        if (errno == EINTR) {
            continue;
        }
        return {RecvStatus::IoError, errno};
    }

    return {RecvStatus::Ok, 0};
}

bool decode_utf8(const std::string& text, std::size_t& offset,
                 std::uint32_t& codepoint) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t size = text.size();
    const unsigned char first = bytes[offset];

    if (first <= 0x7f) {
        codepoint = first;
        ++offset;
        return true;
    }

    std::size_t count = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        count = 2;
        value = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 3;
        value = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        return false;
    }

    if (offset + count > size) {
        return false;
    }
    for (std::size_t i = 1; i < count; ++i) {
        const unsigned char next = bytes[offset + i];
        if ((next & 0xc0) != 0x80) {
            return false;
        }
        value = (value << 6) | (next & 0x3f);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return false;
    }

    codepoint = value;
    offset += count;
    return true;
}

bool timestamp_has_expected_shape(const std::string& value) {
    if (value.size() != 19) {
        return false;
    }
    constexpr std::size_t separators[] = {4, 7, 10, 13, 16};
    constexpr char expected[] = {'-', '-', ' ', ':', ':'};
    for (std::size_t i = 0; i < 19; ++i) {
        bool is_separator = false;
        for (std::size_t j = 0; j < 5; ++j) {
            if (i == separators[j]) {
                is_separator = true;
                if (value[i] != expected[j]) {
                    return false;
                }
                break;
            }
        }
        if (!is_separator && (value[i] < '0' || value[i] > '9')) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool send_all(int fd, const void* bytes, std::size_t length, int* error_number) {
    const auto* data = static_cast<const unsigned char*>(bytes);
    std::size_t offset = 0;
    if (error_number != nullptr) {
        *error_number = 0;
    }

    while (offset < length) {
        const ssize_t count = ::send(fd, data + offset, length - offset,
                                    MSG_NOSIGNAL);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (error_number != nullptr) {
            *error_number = count == 0 ? EPIPE : errno;
        }
        return false;
    }

    return true;
}

bool send_frame(int fd, const Frame& frame, int* error_number) {
    const std::size_t body_size = 1 + frame.data.size();
    if (body_size < 1 || body_size > MAX_BODY_BYTES) {
        if (error_number != nullptr) {
            *error_number = EMSGSIZE;
        }
        return false;
    }

    const std::uint32_t network_size = htonl(static_cast<std::uint32_t>(body_size));
    std::string encoded(sizeof(network_size) + body_size, '\0');
    std::memcpy(encoded.data(), &network_size, sizeof(network_size));
    encoded[sizeof(network_size)] = static_cast<char>(frame.type);
    if (!frame.data.empty()) {
        std::memcpy(encoded.data() + sizeof(network_size) + 1,
                    frame.data.data(), frame.data.size());
    }
    return send_all(fd, encoded.data(), encoded.size(), error_number);
}

RecvResult recv_frame(int fd) {
    std::uint32_t network_size = 0;
    const ExactResult header = recv_exact(fd, &network_size, sizeof(network_size));
    if (header.status != RecvStatus::Ok) {
        return {header.status, {MessageType::ERROR, {}}, header.error_number};
    }

    const std::uint32_t body_size = ntohl(network_size);
    if (body_size < 1 || body_size > MAX_BODY_BYTES) {
        return {RecvStatus::InvalidLength, {MessageType::ERROR, {}}, 0};
    }

    std::string body(body_size, '\0');
    const ExactResult payload = recv_exact(fd, body.data(), body.size());
    if (payload.status != RecvStatus::Ok) {
        return {payload.status, {MessageType::ERROR, {}}, payload.error_number};
    }

    Frame frame{static_cast<MessageType>(static_cast<unsigned char>(body[0])),
                body.substr(1)};
    return {RecvStatus::Ok, std::move(frame), 0};
}

bool valid_utf8(const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        std::uint32_t codepoint = 0;
        if (!decode_utf8(text, offset, codepoint)) {
            return false;
        }
    }
    return true;
}

bool valid_username(const std::string& username) {
    if (username.empty() || username.size() > MAX_USERNAME_BYTES) {
        return false;
    }
    std::size_t offset = 0;
    while (offset < username.size()) {
        std::uint32_t codepoint = 0;
        if (!decode_utf8(username, offset, codepoint) || codepoint <= 0x1f ||
            (codepoint >= 0x7f && codepoint <= 0x9f)) {
            return false;
        }
    }
    return true;
}

bool valid_chat(const std::string& content) {
    if (content.empty() || content.size() > MAX_CHAT_BYTES ||
        !valid_utf8(content)) {
        return false;
    }
    return content.find('\0') == std::string::npos;
}

bool valid_error_text(const std::string& text) {
    return !text.empty() && text.size() <= MAX_BODY_BYTES - 1 &&
           valid_utf8(text) && text.find('\0') == std::string::npos;
}

std::string make_broadcast_data(const std::string& timestamp,
                                const std::string& username,
                                const std::string& content) {
    return timestamp + '\n' + username + '\n' + content;
}

bool parse_broadcast_data(const std::string& data, BroadcastMessage& out) {
    if (data.size() < 21 || data[19] != '\n') {
        return false;
    }
    const std::size_t username_end = data.find('\n', 20);
    if (username_end == std::string::npos) {
        return false;
    }

    BroadcastMessage parsed;
    parsed.timestamp = data.substr(0, 19);
    parsed.username = data.substr(20, username_end - 20);
    parsed.content = data.substr(username_end + 1);
    if (!timestamp_has_expected_shape(parsed.timestamp) ||
        !valid_username(parsed.username) || !valid_chat(parsed.content)) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

}  // namespace chat
