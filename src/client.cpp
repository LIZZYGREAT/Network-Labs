#include "protocol.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace {

bool parse_port(const char* raw, std::uint16_t& port) {
    if (raw == nullptr || *raw == '\0') {
        return false;
    }
    unsigned int parsed = 0;
    const char* end = raw + std::strlen(raw);
    const auto result = std::from_chars(raw, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 1 || parsed > 65535) {
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

std::string error_message(int error_number) {
    return std::system_category().message(error_number);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <server-ipv4> <port 1-65535> <username>\n";
        return 2;
    }

    std::uint16_t port = 0;
    if (!parse_port(argv[2], port)) {
        std::cerr << "Invalid port: expected an integer from 1 to 65535\n";
        return 2;
    }
    const std::string username(argv[3]);
    if (!chat::valid_username(username)) {
        std::cerr << "Invalid username: use 1-32 UTF-8 bytes without control characters\n";
        return 2;
    }

    in_addr server_address{};
    if (::inet_pton(AF_INET, argv[1], &server_address) != 1) {
        std::cerr << "Invalid IPv4 address: " << argv[1] << '\n';
        return 2;
    }

    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "socket: " << error_message(errno) << '\n';
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr = server_address;
    if (::connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "connect: " << error_message(errno) << '\n';
        ::close(socket_fd);
        return 1;
    }

    int error_number = 0;
    if (!chat::send_frame(socket_fd, {chat::MessageType::LOGIN, username},
                          &error_number)) {
        std::cerr << "send LOGIN: " << error_message(error_number) << '\n';
        ::close(socket_fd);
        return 1;
    }

    const chat::RecvResult login_result = chat::recv_frame(socket_fd);
    if (login_result.status != chat::RecvStatus::Ok) {
        std::cerr << "server closed the connection before login completed\n";
        ::close(socket_fd);
        return 1;
    }
    if (login_result.frame.type == chat::MessageType::ERROR) {
        std::cerr << "login rejected: " << login_result.frame.data << '\n';
        ::close(socket_fd);
        return 1;
    }
    if (login_result.frame.type != chat::MessageType::LOGIN_OK ||
        !login_result.frame.data.empty()) {
        std::cerr << "unexpected response during login\n";
        ::close(socket_fd);
        return 1;
    }

    std::cout << "Connected as " << username
              << ". Enter a message, /quit to leave.\n";
    std::atomic<bool> running{true};
    std::thread receiver;
    try {
        receiver = std::thread([&]() {
            while (running.load()) {
                chat::RecvResult result = chat::recv_frame(socket_fd);
                if (result.status != chat::RecvStatus::Ok) {
                    if (running.load()) {
                        std::cout << "\n[connection closed]" << std::endl;
                    }
                    running.store(false);
                    return;
                }
                if (result.frame.type == chat::MessageType::BROADCAST) {
                    chat::BroadcastMessage message;
                    if (!chat::parse_broadcast_data(result.frame.data, message)) {
                        std::cout << "\n[invalid BROADCAST received]" << std::endl;
                        running.store(false);
                        return;
                    }
                    std::cout << "\n[" << message.timestamp << "] "
                              << message.username << ": " << message.content
                              << std::endl;
                } else if (result.frame.type == chat::MessageType::ERROR) {
                    std::cout << "\n[server error] " << result.frame.data << std::endl;
                    running.store(false);
                    return;
                } else {
                    std::cout << "\n[unexpected server message]" << std::endl;
                    running.store(false);
                    return;
                }
            }
        });
    } catch (const std::system_error& error) {
        std::cerr << "could not start receiver thread: " << error.what() << '\n';
        ::shutdown(socket_fd, SHUT_RDWR);
        ::close(socket_fd);
        return 1;
    }

    bool send_quit = false;
    while (running.load()) {
        std::cout << username << "> " << std::flush;
        bool input_ready = false;
        while (running.load() && !input_ready) {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const int ready = ::poll(&input, 1, 200);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "poll stdin: " << error_message(errno) << '\n';
                running.store(false);
                break;
            }
            if (ready == 0) {
                continue;
            }
            input_ready = (input.revents & (POLLIN | POLLHUP)) != 0;
        }
        if (!running.load()) {
            break;
        }

        std::string line;
        if (!std::getline(std::cin, line)) {
            send_quit = true;
            break;
        }
        if (line == "/quit") {
            send_quit = true;
            break;
        }
        if (line.empty()) {
            continue;
        }
        if (!chat::valid_chat(line)) {
            std::cerr << "Message must be valid UTF-8 and contain 1-4000 bytes\n";
            continue;
        }
        if (!chat::send_frame(socket_fd, {chat::MessageType::CHAT, line},
                              &error_number)) {
            std::cerr << "send CHAT: " << error_message(error_number) << '\n';
            running.store(false);
            break;
        }
    }

    if (send_quit && running.load()) {
        chat::send_frame(socket_fd, {chat::MessageType::QUIT, {}}, &error_number);
    }
    running.store(false);
    ::shutdown(socket_fd, SHUT_RDWR);
    if (receiver.joinable()) {
        receiver.join();
    }
    ::close(socket_fd);
    return 0;
}
