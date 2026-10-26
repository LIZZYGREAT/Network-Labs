#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat {

enum class MessageType : std::uint8_t {
    LOGIN = 0x01,
    LOGIN_OK = 0x02,
    CHAT = 0x03,
    BROADCAST = 0x04,
    QUIT = 0x05,
    ERROR = 0x06,
};

constexpr std::uint32_t MAX_BODY_BYTES = 4096;
constexpr std::size_t MAX_USERNAME_BYTES = 32;
constexpr std::size_t MAX_CHAT_BYTES = 4000;

struct Frame {
    MessageType type;
    std::string data;
};

enum class RecvStatus {
    Ok,
    Eof,
    Truncated,
    IoError,
    InvalidLength,
};

struct RecvResult {
    RecvStatus status = RecvStatus::IoError;
    Frame frame{MessageType::ERROR, {}};
    int error_number = 0;
};

struct BroadcastMessage {
    std::string timestamp;
    std::string username;
    std::string content;
};

bool send_all(int fd, const void* bytes, std::size_t length,
              int* error_number = nullptr);
bool send_frame(int fd, const Frame& frame, int* error_number = nullptr);
RecvResult recv_frame(int fd);

bool valid_utf8(const std::string& text);
bool valid_username(const std::string& username);
bool valid_chat(const std::string& content);
bool valid_error_text(const std::string& text);

std::string make_broadcast_data(const std::string& timestamp,
                                const std::string& username,
                                const std::string& content);
bool parse_broadcast_data(const std::string& data, BroadcastMessage& out);

}  // namespace chat
