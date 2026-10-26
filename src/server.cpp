#include "protocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <ctime>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

volatile sig_atomic_t stop_requested = 0;

void handle_signal(int) { stop_requested = 1; }

struct ClientSession {
    explicit ClientSession(int socket_fd) : fd(socket_fd) {}

    int fd;
    std::string username;
    std::mutex send_mutex;
    bool active = true;  // guarded by send_mutex
};

struct ServerState {
    std::mutex users_mutex;
    std::mutex broadcast_mutex;
    std::mutex sessions_mutex;
    std::atomic<bool> stopping{false};
    std::unordered_map<std::string, std::shared_ptr<ClientSession>> users;
    std::vector<std::shared_ptr<ClientSession>> all_sessions;
};

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

std::string current_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (::localtime_r(&now, &local) == nullptr) {
        return "1970-01-01 00:00:00";
    }
    char buffer[20]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) {
        return "1970-01-01 00:00:00";
    }
    return buffer;
}

bool send_to_session(const std::shared_ptr<ClientSession>& session,
                     const chat::Frame& frame) {
    std::lock_guard<std::mutex> lock(session->send_mutex);
    if (!session->active || session->fd < 0) {
        return false;
    }

    int error_number = 0;
    if (chat::send_frame(session->fd, frame, &error_number)) {
        return true;
    }

    session->active = false;
    ::shutdown(session->fd, SHUT_RDWR);
    std::cerr << "send to " << session->username << " failed: "
              << error_message(error_number) << '\n';
    return false;
}

void send_error(const std::shared_ptr<ClientSession>& session,
                const std::string& message) {
    std::lock_guard<std::mutex> lock(session->send_mutex);
    if (!session->active || session->fd < 0) {
        return;
    }
    int error_number = 0;
    const bool sent = chat::send_frame(
        session->fd, {chat::MessageType::ERROR, message}, &error_number);
    session->active = false;
    ::shutdown(session->fd, sent ? SHUT_WR : SHUT_RDWR);
    if (!sent) {
        std::cerr << "send error response failed: "
                  << error_message(error_number) << '\n';
    }
}

void cleanup_session(ServerState& state,
                     const std::shared_ptr<ClientSession>& session,
                     bool registered) {
    if (registered) {
        std::lock_guard<std::mutex> users_lock(state.users_mutex);
        const auto found = state.users.find(session->username);
        if (found != state.users.end() && found->second == session) {
            state.users.erase(found);
        }
    }

    {
        std::lock_guard<std::mutex> send_lock(session->send_mutex);
        if (session->fd >= 0) {
            session->active = false;
            ::shutdown(session->fd, SHUT_RDWR);
            ::close(session->fd);
            session->fd = -1;
        }
    }

    if (registered) {
        std::cout << "user disconnected: " << session->username << '\n';
    }
}

void broadcast_chat(ServerState& state, const std::string& username,
                    const std::string& content) {
    const chat::Frame frame{
        chat::MessageType::BROADCAST,
        chat::make_broadcast_data(current_timestamp(), username, content)};
    if (frame.data.size() + 1 > chat::MAX_BODY_BYTES) {
        return;
    }

    std::lock_guard<std::mutex> broadcast_lock(state.broadcast_mutex);
    std::vector<std::shared_ptr<ClientSession>> recipients;
    {
        std::lock_guard<std::mutex> users_lock(state.users_mutex);
        recipients.reserve(state.users.size());
        for (const auto& entry : state.users) {
            recipients.push_back(entry.second);
        }
    }

    for (const auto& recipient : recipients) {
        if (state.stopping.load()) {
            break;
        }
        send_to_session(recipient, frame);
    }
}

void handle_client(ServerState& state,
                   const std::shared_ptr<ClientSession>& session) {
    bool registered = false;
    chat::RecvResult first = chat::recv_frame(session->fd);
    if (first.status != chat::RecvStatus::Ok) {
        if (first.status == chat::RecvStatus::InvalidLength) {
            std::cerr << "rejected frame with invalid body length\n";
        }
        cleanup_session(state, session, registered);
        return;
    }

    if (first.frame.type != chat::MessageType::LOGIN) {
        send_error(session, "LOGIN required");
        cleanup_session(state, session, registered);
        return;
    }
    if (!chat::valid_username(first.frame.data)) {
        send_error(session, "invalid username");
        cleanup_session(state, session, registered);
        return;
    }

    session->username = first.frame.data;
    std::unique_lock<std::mutex> send_lock(session->send_mutex);
    bool duplicate = false;
    {
        std::lock_guard<std::mutex> users_lock(state.users_mutex);
        duplicate = state.users.find(session->username) != state.users.end();
        if (!duplicate && session->active) {
            state.users.emplace(session->username, session);
            registered = true;
        }
    }

    if (duplicate) {
        int error_number = 0;
        if (!chat::send_frame(session->fd,
                              {chat::MessageType::ERROR, "username already in use"},
                              &error_number)) {
            session->active = false;
            ::shutdown(session->fd, SHUT_RDWR);
        } else {
            session->active = false;
            ::shutdown(session->fd, SHUT_WR);
        }
        send_lock.unlock();
        cleanup_session(state, session, registered);
        return;
    }

    int error_number = 0;
    if (!registered ||
        !chat::send_frame(session->fd, {chat::MessageType::LOGIN_OK, {}},
                          &error_number)) {
        session->active = false;
        if (session->fd >= 0) {
            ::shutdown(session->fd, SHUT_RDWR);
        }
        send_lock.unlock();
        cleanup_session(state, session, registered);
        return;
    }
    send_lock.unlock();

    std::cout << "user connected: " << session->username << '\n';
    while (!state.stopping.load()) {
        chat::RecvResult result = chat::recv_frame(session->fd);
        if (result.status != chat::RecvStatus::Ok) {
            if (result.status == chat::RecvStatus::InvalidLength) {
                std::cerr << "invalid frame length from " << session->username << '\n';
            } else if (result.status == chat::RecvStatus::Truncated) {
                std::cerr << "incomplete frame from " << session->username << '\n';
            } else if (result.status == chat::RecvStatus::IoError &&
                       result.error_number != 0 &&
                       result.error_number != ECONNRESET &&
                       result.error_number != ESHUTDOWN) {
                std::cerr << "receive from " << session->username << " failed: "
                          << error_message(result.error_number) << '\n';
            }
            break;
        }

        if (result.frame.type == chat::MessageType::QUIT && result.frame.data.empty()) {
            break;
        }
        if (result.frame.type != chat::MessageType::CHAT) {
            send_error(session, "unexpected message type");
            break;
        }
        if (!chat::valid_chat(result.frame.data)) {
            send_error(session, "invalid message");
            break;
        }
        broadcast_chat(state, session->username, result.frame.data);
    }

    cleanup_session(state, session, registered);
}

int create_listen_socket(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket: " << error_message(errno) << '\n';
        return -1;
    }

    int reuse_address = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                     sizeof(reuse_address)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR): " << error_message(errno) << '\n';
        ::close(fd);
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "bind: " << error_message(errno) << '\n';
        ::close(fd);
        return -1;
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        std::cerr << "listen: " << error_message(errno) << '\n';
        ::close(fd);
        return -1;
    }
    return fd;
}

void set_send_timeout(int fd) {
    const timeval timeout{3, 0};
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cerr << "setsockopt(SO_SNDTIMEO): " << error_message(errno) << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port 1-65535>\n";
        return 2;
    }

    std::uint16_t port = 0;
    if (!parse_port(argv[1], port)) {
        std::cerr << "Invalid port: expected an integer from 1 to 65535\n";
        return 2;
    }

    struct sigaction action{};
    action.sa_handler = handle_signal;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (::sigaction(SIGINT, &action, nullptr) < 0 ||
        ::sigaction(SIGTERM, &action, nullptr) < 0) {
        std::cerr << "sigaction: " << error_message(errno) << '\n';
        return 1;
    }

    const int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        return 1;
    }

    std::cout << "server listening on 0.0.0.0:" << port
              << " (Ctrl+C to stop)\n";
    ServerState state;
    std::vector<std::thread> workers;

    while (stop_requested == 0) {
        pollfd descriptor{listen_fd, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 200);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll: " << error_message(errno) << '\n';
            break;
        }
        if (ready == 0 || (descriptor.revents & POLLIN) == 0) {
            continue;
        }

        sockaddr_in peer{};
        socklen_t peer_length = sizeof(peer);
        const int client_fd = ::accept(
            listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            std::cerr << "accept: " << error_message(errno) << '\n';
            continue;
        }

        set_send_timeout(client_fd);
        char peer_ip[INET_ADDRSTRLEN]{};
        if (::inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip)) == nullptr) {
            std::strncpy(peer_ip, "unknown", sizeof(peer_ip) - 1);
        }
        std::cout << "accepted connection from " << peer_ip << ':'
                  << ntohs(peer.sin_port) << '\n';

        auto session = std::make_shared<ClientSession>(client_fd);
        {
            std::lock_guard<std::mutex> lock(state.sessions_mutex);
            state.all_sessions.push_back(session);
        }
        try {
            workers.emplace_back(handle_client, std::ref(state), session);
        } catch (const std::system_error& error) {
            std::cerr << "could not start client thread: " << error.what() << '\n';
            std::lock_guard<std::mutex> lock(session->send_mutex);
            session->active = false;
            ::close(session->fd);
            session->fd = -1;
        }
    }

    std::cout << "stopping server...\n";
    state.stopping.store(true);
    ::close(listen_fd);
    std::vector<std::shared_ptr<ClientSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(state.sessions_mutex);
        sessions = state.all_sessions;
    }
    for (const auto& session : sessions) {
        std::lock_guard<std::mutex> lock(session->send_mutex);
        if (session->fd >= 0) {
            session->active = false;
            ::shutdown(session->fd, SHUT_RDWR);
        }
    }
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    std::cout << "server stopped\n";
    return 0;
}
