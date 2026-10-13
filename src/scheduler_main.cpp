#include "common.hpp"
#include "store.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace dts;

static void handle_worker(int fd, const std::string& dsn)
{
    try
    {
        Store db(dsn);
        timeval timeout{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        auto hello = recv_line(fd);

        if (!hello)
        {
            close(fd);
            return;
        }

        auto fields = split(*hello);

        if (fields.size() != 2 || fields[0] != "HELLO" || !valid_name(fields[1]))
        {
            close(fd);
            return;
        }

        auto worker = fields[1];
        std::cout << "worker connected: " << worker << std::endl;
        constexpr int lease = 10;

        while (true)
        {
            auto job = db.claim(worker, lease);
            if (!job)
            {
                if (!send_all(fd, "WAIT\n"))
                {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                continue;
            }

            std::cout << "dispatch job=" << job->id << " attempt=" << job->attempt << " worker=" << worker << std::endl;

            std::string message = "TASK " + std::to_string(job->id) + " " + std::to_string(job->attempt) + " " +
                                  job->name + " " + std::to_string(job->duration_ms) + " " + std::to_string(job->a) +
                                  " " + std::to_string(job->b) + " " + std::to_string(job->c) + " " +
                                  std::to_string(job->d) + "\n";

            if (!send_all(fd, message))
            {
                db.fail(job->id, job->attempt, "send_failed");
                break;
            }

            bool done = false;
            while (!done)
            {
                auto line = recv_line(fd);
                if (!line)
                {
                    db.fail(job->id, job->attempt, "worker_disconnected_or_timeout");
                    close(fd);
                    return;
                }

                auto msg = split(*line);
                if (msg.size() < 3)
                {
                    db.fail(job->id, job->attempt, "invalid_response");
                    close(fd);
                    return;
                }

                int id = 0, attempt = 0;
                try
                {
                    id = std::stoi(msg[1]);
                    attempt = std::stoi(msg[2]);
                }
                catch (...)
                {
                    close(fd);
                    return;
                }

                if (id != job->id || attempt != job->attempt)
                {
                    close(fd);
                    return;
                }

                if (msg[0] == "PING" && msg.size() == 3)
                {
                    if (!db.renew(id, attempt, lease))
                    {
                        close(fd);
                        return;
                    }
                }
                else if (msg[0] == "DONE" && msg.size() == 4)
                {
                    bool accepted = db.finish(id, attempt, msg[3]);
                    std::cout << "job=" << id << " complete=" << accepted << std::endl;
                    done = true;
                }
                else if (msg[0] == "FAIL" && msg.size() == 4)
                {
                    db.fail(id, attempt, msg[3]);
                    done = true;
                }
                else
                {
                    db.fail(id, attempt, "invalid_response");
                    close(fd);
                    return;
                }
            }
        }
    }

    catch (const std::exception& e)
    {
        std::cerr << "worker handler: " << e.what() << '\n';
    }

    close(fd);
}

int main()
{
    const std::string dsn = env_str("DATABASE_URL", "dbname=distributed_task_system");
    const int port = env_int("WORKER_PORT", 9000);

    try
    {
        Store db(dsn); // fail fast on invalid database settings
        std::thread([dsn] {
            while (true)
            {
                try
                {
                    Store s(dsn);
                    s.recover_expired();
                }
                catch (const std::exception& e)
                {
                    std::cerr << "recovery: " << e.what() << '\n';
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }).detach();

        int server = listen_tcp(port);
        std::cout << "scheduler listening on " << port << std::endl;

        while (true)
        {
            int fd = accept(server, nullptr, nullptr);
            if (fd >= 0)
            {
                std::thread(handle_worker, fd, dsn).detach();
            }
        }
    }

    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
