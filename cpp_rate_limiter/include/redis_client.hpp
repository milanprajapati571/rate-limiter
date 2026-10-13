#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <variant>

struct RedisReply {
    enum class Type { Nil, Integer, String, Array, Error };
    Type type{Type::Nil};
    long long integer_val{0};
    std::string string_val;
    std::vector<RedisReply> elements;
};

class RedisClient {
public:
    RedisClient(const std::string& host = "127.0.0.1", int port = 6379);
    ~RedisClient();

    bool connect();
    void disconnect();
    bool is_connected() const;

    // Send a command with arguments: e.g. execute({"PING"})
    RedisReply execute(const std::vector<std::string>& args);

    // Helper to evaluate Lua script
    RedisReply eval(const std::string& script, int num_keys, const std::vector<std::string>& keys, const std::vector<std::string>& args);

private:
    std::string host_;
    int port_;
    bool connected_{false};
    uintptr_t socket_handle_{static_cast<uintptr_t>(~0)}; // SOCKET on Windows
    std::mutex mtx_;

    bool send_raw(const std::string& data);
    std::string read_line();
    std::string read_bytes(size_t n);
    RedisReply parse_reply();
};
