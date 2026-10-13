#include "common.hpp"
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <netdb.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace dts
{
std::vector<std::string> split(const std::string& line)
{
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string token;

    while (in >> token)
    {
        out.push_back(token);
    }

    return out;
}

bool send_all(int fd, const std::string& msg)
{
    size_t pos = 0;

    while (pos < msg.size())
    {
        ssize_t n = send(fd, msg.data() + pos, msg.size() - pos, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }

        if (n <= 0)
        {
            return false;
        }

        pos += static_cast<size_t>(n);
    }

    return true;
}

std::optional<std::string> recv_line(int fd)
{
    std::string out;
    char c;

    while (out.size() < 4096)
    {
        ssize_t n = recv(fd, &c, 1, 0);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }

        if (n <= 0)
        {
            return std::nullopt;
        }

        if (c == '\n')
        {
            return out;
        }

        if (c != '\r')
        {
            out += c;
        }
    }

    return std::nullopt;
}

int listen_tcp(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        throw std::runtime_error("socket failed");
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(fd, 64) < 0)
    {
        close(fd);
        throw std::runtime_error("bind/listen failed");
    }

    return fd;
}

int connect_tcp(const std::string& host, int port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    std::string service = std::to_string(port);

    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &results) != 0)
    {
        return -1;
    }

    int fd = -1;

    for (auto p = results; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
        {
            continue;
        }

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
        {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    return fd;
}

int env_int(const char* name, int fallback)
{
    if (const char* v = std::getenv(name))
    {
        try
        {
            return std::stoi(v);
        }

        catch (...)
        {
        }
    }

    return fallback;
}

std::string env_str(const char* name, std::string fallback)
{
    const char* v = std::getenv(name);
    return v ? v : fallback;
}

bool valid_name(const std::string& s)
{
    if (s.empty() || s.size() > 80)
    {
        return false;
    }

    for (unsigned char c : s)
    {
        if (!std::isalnum(c) && c != '_' && c != '-')
        {
            return false;
        }
    }
    
    return true;
}
} // namespace dts
