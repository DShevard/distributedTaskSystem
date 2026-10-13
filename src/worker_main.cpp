#include "common.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace dts;

double discriminant(double a, double b, double c)
{
    return b * b - 4 * a * c;
}

double determinant(double a, double b, double c, double d)
{
    return a * d - b * c;
}

int main()
{
    auto host = env_str("SCHEDULER_HOST", "localhost");
    int port = env_int("WORKER_PORT", 9000);
    auto name = env_str("WORKER_NAME", "worker");

    if (!valid_name(name))
    {
        std::cerr << "Invalid WORKER_NAME\n";
        return 1;
    }

    for (;;)
    {
        int fd = connect_tcp(host, port);

        if (fd < 0)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        std::cout << name << " connected\n";

        if (!send_all(fd, "HELLO " + name + "\n"))
        {
            close(fd);
            continue;
        }

        while (true)
        {
            auto line = recv_line(fd);
            if (!line)
            {
                break;
            }

            auto parts = split(*line);
            if (parts.size() == 1 && parts[0] == "WAIT")
            {
                continue;
            }

            if (parts.size() != 9 || parts[0] != "TASK")
            {
                break;
            }

            int id, attempt, delay;
            double a, b, c, d;

            try
            {
                id = std::stoi(parts[1]);
                attempt = std::stoi(parts[2]);
                delay = std::stoi(parts[4]);

                a = std::stod(parts[5]);
                b = std::stod(parts[6]);
                c = std::stod(parts[7]);
                d = std::stod(parts[8]);
            }

            catch (...)
            {
                break;
            }

            if ((parts[3] != "discriminant" && parts[3] != "determinant") || delay < 0 || delay > 300000)
            {
                break;
            }

            std::cout << name << " executing job=" << id << " name=" << parts[3] << "\n";
            // Demo workload: simulated CPU-independent job; heartbeat each second.
            bool connected = true;

            while (delay > 0)
            {
                int chunk = std::min(delay, 1000);
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                delay -= chunk;

                if (!send_all(fd, "PING " + std::to_string(id) + " " + std::to_string(attempt) + "\n"))
                {
                    connected = false;
                    break;
                }
            }

            if (!connected)
            {
                break;
            }

            double result;

            if (parts[3] == "discriminant")
            {
                result = discriminant(a, b, c);
            }
            else
            {
                result = determinant(a, b, c, d);
            }

            std::string message =
                "DONE " + std::to_string(id) + " " + std::to_string(attempt) + " " + std::to_string(result) + "\n";

            if (!send_all(fd, message))
            {
                break;
            }
        }

        close(fd);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
