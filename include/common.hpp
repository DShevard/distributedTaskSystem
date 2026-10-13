#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dts
{
struct Job
{
    int id{}, attempt{}, duration_ms{};
    std::string name;

    double a{}, b{}, c{}, d{};
};

// Protocol is LF-terminated ASCII. Names are restricted to [A-Za-z0-9_-].
std::vector<std::string> split(const std::string& line);
bool send_all(int fd, const std::string& message);
std::optional<std::string> recv_line(int fd);
int listen_tcp(int port);
int connect_tcp(const std::string& host, int port);
int env_int(const char* name, int fallback);
std::string env_str(const char* name, std::string fallback);
bool valid_name(const std::string& name);
} // namespace dts
