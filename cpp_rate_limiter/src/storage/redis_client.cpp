#include "redis_client.hpp"
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
typedef int SOCKET;
#endif

RedisClient::RedisClient(const std::string& host, int port)
    : host_(host), port_(port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

RedisClient::~RedisClient() {
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RedisClient::connect() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (connected_) return true;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        connected_ = false;
        return false;
    }

    // Set 1-second receive & send timeouts
#ifdef _WIN32
    DWORD timeout = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);

    if (::connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(sock);
        connected_ = false;
        return false;
    }

    socket_handle_ = static_cast<uintptr_t>(sock);
    connected_ = true;
    return true;
}

void RedisClient::disconnect() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (connected_) {
        SOCKET sock = static_cast<SOCKET>(socket_handle_);
        closesocket(sock);
        socket_handle_ = static_cast<uintptr_t>(~0);
        connected_ = false;
    }
}

bool RedisClient::is_connected() const {
    return connected_;
}

bool RedisClient::send_raw(const std::string& data) {
    if (!connected_) return false;
    SOCKET sock = static_cast<SOCKET>(socket_handle_);
    size_t total = 0;
    while (total < data.size()) {
        int sent = send(sock, data.data() + total, static_cast<int>(data.size() - total), 0);
        if (sent <= 0) {
            connected_ = false;
            return false;
        }
        total += sent;
    }
    return true;
}

std::string RedisClient::read_line() {
    std::string line;
    SOCKET sock = static_cast<SOCKET>(socket_handle_);
    char c;
    while (true) {
        int r = recv(sock, &c, 1, 0);
        if (r <= 0) {
            connected_ = false;
            return "";
        }
        if (c == '\r') {
            // Read next '\n'
            recv(sock, &c, 1, 0);
            break;
        }
        line += c;
    }
    return line;
}

std::string RedisClient::read_bytes(size_t n) {
    std::string buffer(n, '\0');
    SOCKET sock = static_cast<SOCKET>(socket_handle_);
    size_t total = 0;
    while (total < n) {
        int r = recv(sock, &buffer[total], static_cast<int>(n - total), 0);
        if (r <= 0) {
            connected_ = false;
            return "";
        }
        total += r;
    }
    // Read trailing \r\n
    char crlf[2];
    recv(sock, crlf, 2, 0);
    return buffer;
}

RedisReply RedisClient::parse_reply() {
    RedisReply reply;
    std::string line = read_line();
    if (line.empty() && !connected_) {
        reply.type = RedisReply::Type::Error;
        reply.string_val = "Connection closed";
        return reply;
    }

    char prefix = line[0];
    std::string content = line.substr(1);

    if (prefix == '+') { // Simple string
        reply.type = RedisReply::Type::String;
        reply.string_val = content;
    } else if (prefix == '-') { // Error
        reply.type = RedisReply::Type::Error;
        reply.string_val = content;
    } else if (prefix == ':') { // Integer
        reply.type = RedisReply::Type::Integer;
        try {
            reply.integer_val = std::stoll(content);
        } catch (...) {
            reply.integer_val = 0;
        }
    } else if (prefix == '$') { // Bulk string
        long long len = 0;
        try {
            len = std::stoll(content);
        } catch (...) {
            len = -1;
        }
        if (len < 0) {
            reply.type = RedisReply::Type::Nil;
        } else {
            reply.type = RedisReply::Type::String;
            reply.string_val = read_bytes(static_cast<size_t>(len));
        }
    } else if (prefix == '*') { // Array
        long long count = 0;
        try {
            count = std::stoll(content);
        } catch (...) {
            count = -1;
        }
        if (count < 0) {
            reply.type = RedisReply::Type::Nil;
        } else {
            reply.type = RedisReply::Type::Array;
            for (long long i = 0; i < count; ++i) {
                reply.elements.push_back(parse_reply());
            }
        }
    }
    return reply;
}

RedisReply RedisClient::execute(const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!connected_) {
        if (!connect()) {
            RedisReply reply;
            reply.type = RedisReply::Type::Error;
            reply.string_val = "Not connected";
            return reply;
        }
    }

    // Format RESP command: *<count>\r\n$<len>\r\n<arg>\r\n...
    std::ostringstream oss;
    oss << "*" << args.size() << "\r\n";
    for (const auto& arg : args) {
        oss << "$" << arg.size() << "\r\n" << arg << "\r\n";
    }

    if (!send_raw(oss.str())) {
        RedisReply reply;
        reply.type = RedisReply::Type::Error;
        reply.string_val = "Failed to send command";
        return reply;
    }

    return parse_reply();
}

RedisReply RedisClient::eval(const std::string& script, int num_keys, const std::vector<std::string>& keys, const std::vector<std::string>& args) {
    std::vector<std::string> cmd;
    cmd.push_back("EVAL");
    cmd.push_back(script);
    cmd.push_back(std::to_string(num_keys));
    for (const auto& k : keys) cmd.push_back(k);
    for (const auto& a : args) cmd.push_back(a);
    return execute(cmd);
}
