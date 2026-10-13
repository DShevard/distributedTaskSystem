#include "common.hpp"
#include "store.hpp"
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace dts;

static void respond(int fd, int status, const std::string& body)
{
    const char* reason = status == 200   ? "OK"
                         : status == 201 ? "Created"
                         : status == 400 ? "Bad Request"
                         : status == 404 ? "Not Found"
                                         : "Internal Server Error";
    std::string hdr = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                      "\r\nConnection: close\r\n\r\n";
    send_all(fd, hdr + body);
}

static std::optional<std::string> string_field(const std::string& body, const std::string& key)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (std::regex_search(body, m, re))
        return m[1].str();
    return std::nullopt;
}

static std::optional<double> double_field(const std::string& body, const std::string& key)
{
    std::regex pattern("\"" + key + "\"\\s*:\\s*(-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))");

    std::smatch match;

    if (!std::regex_search(body, match, pattern))
    {
        return std::nullopt;
    }

    return std::stod(match[1].str());
}

static int int_field(const std::string& body, const std::string& key, int fallback)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (std::regex_search(body, m, re))
        return std::stoi(m[1].str());
    return fallback;
}

static void client(int fd, const std::string& dsn)
{
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    try
    {
        std::string request;
        char buf[4096];
        size_t end = std::string::npos;

        while ((end = request.find("\r\n\r\n")) == std::string::npos && request.size() < 16384)
        {
            auto n = recv(fd, buf, sizeof buf, 0);

            if (n <= 0)
            {
                close(fd);
                return;
            }

            request.append(buf, n);
        }

        if (end == std::string::npos)
        {
            respond(fd, 400, "{\"error\":\"invalid headers\"}");
            close(fd);
            return;
        }

        std::istringstream first(request.substr(0, request.find("\r\n")));
        std::string method, path, version;
        first >> method >> path >> version;
        std::string headers = request.substr(0, end);
        std::regex lenre("Content-Length:\\s*([0-9]+)", std::regex_constants::icase);
        std::smatch m;
        size_t length = 0;

        if (std::regex_search(headers, m, lenre))
        {
            length = std::stoul(m[1].str());
        }

        if (length > 4096)
        {
            respond(fd, 400, "{\"error\":\"body too large\"}");
            close(fd);
            return;
        }

        std::string body = request.substr(end + 4);

        while (body.size() < length)
        {
            auto n = recv(fd, buf, std::min(sizeof buf, length - body.size()), 0);

            if (n <= 0)
            {
                break;
            }

            body.append(buf, n);
        }

        if (body.size() < length)
        {
            respond(fd, 400, "{\"error\":\"incomplete body\"}");
            close(fd);
            return;
        }

        Store db(dsn);

        if (method == "GET" && path == "/health")
        {
            respond(fd, 200, "{\"status\":\"ok\"}");
        }

        else if (method == "GET" && path == "/metrics")
        {
            respond(fd, 200, db.stats_json());
        }

        else if (method == "GET" && path.rfind("/tasks/", 0) == 0)
        {
            try
            {
                auto id = std::stol(path.substr(7));
                auto job = db.job_json(id);

                if (job)
                {
                    respond(fd, 200, *job);
                }
                else
                {
                    respond(fd, 404, "{\"error\":\"not found\"}");
                }
            }

            catch (...)
            {
                respond(fd, 400, "{\"error\":\"invalid task id\"}");
            }
        }

        else if (method == "POST" && path == "/tasks")
        {
            auto name = string_field(body, "name");

            int priority = int_field(body, "priority", 0);
            int ms = int_field(body, "duration_ms", 1000);
            int max = int_field(body, "max_attempts", 3);

            auto a = double_field(body, "a");
            auto b = double_field(body, "b");
            auto c = double_field(body, "c");
            auto d = double_field(body, "d");

            bool valid = name && (*name == "discriminant" || *name == "determinant") && priority >= -100 &&
                         priority <= 100 && ms >= 0 && ms <= 300000 && max >= 1 && max <= 10 && a && b && c;

            if (name && *name == "determinant")
            {
                valid = valid && d.has_value();
            }

            if (!valid)
            {
                respond(fd, 400, "{\"error\":\"invalid task parameters\"}");
            }
            else
            {
                long id = db.submit(*name, priority, ms, max, *a, *b, *c, d.value_or(0.0));

                respond(fd, 201, "{\"id\":" + std::to_string(id) + ",\"status\":\"queued\"}");
            }
        }
        else   
        { 
            respond(fd, 404, "{\"error\":\"unknown route\"}");
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "api: " << e.what() << '\n';
        respond(fd, 500, "{\"error\":\"server error\"}");
    }

    close(fd);
}

int main()
{
    std::string dsn = env_str("DATABASE_URL", "dbname=distributed_task_system");

    try
    {
        Store check(dsn);
        int fd = listen_tcp(env_int("API_PORT", 8080));
        std::cout << "API ready\n";

        while (true)
        {
            int client_fd = accept(fd, nullptr, nullptr);
            if (client_fd >= 0)
            {
                std::thread(client, client_fd, dsn).detach();
            }
        }
    }

    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
