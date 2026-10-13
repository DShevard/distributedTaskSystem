#pragma once
#include "common.hpp"
#include <libpq-fe.h>
#include <mutex>
#include <optional>
#include <string>

namespace dts
{
// Each Store owns a single connection; all queries are guarded by a mutex.
class Store
{
    PGconn* conn_{};
    std::mutex mutex_;
    void exec(const std::string& sql);
    PGresult* query(const std::string& sql, const std::vector<std::string>& args);

  public:
    explicit Store(const std::string& dsn);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    long submit(const std::string& name, int priority, int duration_ms, int max_attempts, double a, double b, double c, double d);
    std::optional<Job> claim(const std::string& worker, int lease_seconds);
    bool renew(int id, int attempt, int lease_seconds);
    bool finish(int id, int attempt, const std::string& result);
    bool fail(int id, int attempt, const std::string& reason);
    int recover_expired();
    std::optional<std::string> job_json(long id);
    std::string stats_json();
};
} // namespace dts
