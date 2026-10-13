#include "store.hpp"
#include <memory>
#include <stdexcept>

namespace dts
{
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
static std::string field(PGresult* r, int row, int col)
{
    return PQgetisnull(r, row, col) ? "" : PQgetvalue(r, row, col);
}

Store::Store(const std::string& dsn)
    : conn_(PQconnectdb(dsn.c_str()))
{
    if (PQstatus(conn_) != CONNECTION_OK)
    {
        throw std::runtime_error(PQerrorMessage(conn_));
    }
}

Store::~Store()
{
    PQfinish(conn_);
}

PGresult* Store::query(const std::string& sql, const std::vector<std::string>& args)
{
    std::vector<const char*> values;
    for (auto& a : args)
    {
        values.push_back(a.c_str());
    }

    PGresult* r =
        PQexecParams(conn_, sql.c_str(), static_cast<int>(args.size()), nullptr, values.data(), nullptr, nullptr, 0);
    if (!r || (PQresultStatus(r) != PGRES_TUPLES_OK && PQresultStatus(r) != PGRES_COMMAND_OK))
    {
        std::string err = PQerrorMessage(conn_);
        if (r)
        {
            PQclear(r);
        }

        throw std::runtime_error(err);
    }

    return r;
}

void Store::exec(const std::string& sql)
{
    Result r(query(sql, {}), PQclear);
}

long Store::submit(const std::string& name, int priority, int duration_ms, int max_attempts, double a, double b,
                   double c, double d)
{
    std::lock_guard lock(mutex_);
    Result r(query("INSERT INTO jobs(name,priority,duration_ms,max_attempts,a,b,c,d) VALUES($1,$2,$3,$4,$5,$6,$7,$8) "
                   "RETURNING id",
                   {name, std::to_string(priority), std::to_string(duration_ms), std::to_string(max_attempts), std::to_string(a), std::to_string(b), std::to_string(c), std::to_string(d)}),
             PQclear);

    return std::stol(field(r.get(), 0, 0));
}
std::optional<Job> Store::claim(const std::string& worker, int lease_seconds)
{
    std::lock_guard lock(mutex_);
    // Atomic claim: skip rows locked by other schedulers, then increment attempt.
    Result r(query(R"SQL(
      WITH candidate AS (
        SELECT id FROM jobs WHERE status='queued' ORDER BY priority DESC,id ASC FOR UPDATE SKIP LOCKED LIMIT 1
      ), claimed AS (
        UPDATE jobs j SET status='running', attempt=j.attempt+1,worker_name=$1,
        lease_until=NOW()+($2::int*INTERVAL '1 second'),updated_at=NOW()
        FROM candidate WHERE j.id=candidate.id
        RETURNING j.id,j.name,j.duration_ms,j.attempt,j.a,j.b,j.c,j.d
      ), history AS (
        INSERT INTO execution_history(job_id,attempt,worker_name,status)
        SELECT id,attempt,$1,'running' FROM claimed
      ) SELECT id,name,duration_ms,attempt,a,b,c,d FROM claimed
    )SQL",
                   {worker, std::to_string(lease_seconds)}),
             PQclear);

    if (PQntuples(r.get()) == 0)
    {
        return std::nullopt;
    }

    return Job{std::stoi(field(r.get(), 0, 0)), std::stoi(field(r.get(), 0, 3)), std::stoi(field(r.get(), 0, 2)),
               field(r.get(), 0, 1), std::stod(field(r.get(), 0, 4)), std::stod(field(r.get(), 0, 5)), std::stod(field(r.get(), 0, 6)), std::stod(field(r.get(), 0, 7)) };
}

bool Store::renew(int id, int attempt, int lease_seconds)
{
    std::lock_guard lock(mutex_);
    Result r(query("UPDATE jobs SET lease_until=NOW()+($3::int*INTERVAL '1 second'),updated_at=NOW() WHERE id=$1 AND "
                   "attempt=$2 AND status='running' RETURNING id",
                   {std::to_string(id), std::to_string(attempt), std::to_string(lease_seconds)}),
             PQclear);

    return PQntuples(r.get()) != 0;
}

bool Store::finish(int id, int attempt, const std::string& value)
{
    std::lock_guard lock(mutex_);
    // A stale worker cannot finish a job claimed by a later attempt.
    Result r(query(R"SQL(
       WITH finished AS (
         UPDATE jobs SET status='completed',result=$3,lease_until=NULL,updated_at=NOW()
         WHERE id=$1 AND attempt=$2 AND status='running' AND lease_until>NOW()
         RETURNING id
       ), history AS (
         UPDATE execution_history SET status='completed',finished_at=NOW()
         WHERE job_id IN (SELECT id FROM finished) AND attempt=$2
       ) SELECT id FROM finished
    )SQL",
                   {std::to_string(id), std::to_string(attempt), value}),
             PQclear);

    return PQntuples(r.get()) != 0;
}

bool Store::fail(int id, int attempt, const std::string& reason)
{
    std::lock_guard lock(mutex_);
    Result r(query(R"SQL(
      WITH changed AS (
        UPDATE jobs SET status=CASE WHEN attempt<max_attempts THEN 'queued' ELSE 'failed' END,
        result=$3, lease_until=NULL,worker_name=NULL,updated_at=NOW()
        WHERE id=$1 AND attempt=$2 AND status='running'
        RETURNING id
      ), history AS (
        UPDATE execution_history SET status='failed',finished_at=NOW()
        WHERE job_id IN (SELECT id FROM changed) AND attempt=$2
      ) SELECT id FROM changed
    )SQL",
                   {std::to_string(id), std::to_string(attempt), reason}),
             PQclear);

    return PQntuples(r.get()) != 0;
}

int Store::recover_expired()
{
    std::lock_guard lock(mutex_);
    Result r(query(R"SQL(
      WITH expired AS (
        UPDATE jobs SET status=CASE WHEN attempt<max_attempts THEN 'queued' ELSE 'failed' END,
          result='lease_expired',lease_until=NULL,worker_name=NULL,updated_at=NOW()
        WHERE status='running' AND lease_until<NOW()
        RETURNING id,attempt
      ), history AS (
        UPDATE execution_history h SET status='timed_out',finished_at=NOW()
        FROM expired e WHERE h.job_id=e.id AND h.attempt=e.attempt
      ) SELECT count(*) FROM expired
    )SQL",
                   {}),
             PQclear);

    return std::stoi(field(r.get(), 0, 0));
}

std::optional<std::string> Store::job_json(long id)
{
    std::lock_guard lock(mutex_);
    Result r(query(R"SQL(SELECT row_to_json(t)::text FROM
      (SELECT id,name,priority,duration_ms,a,b,c,d,status,attempt,max_attempts,worker_name,result,created_at,updated_at
       FROM jobs WHERE id=$1) t)SQL",
                   {std::to_string(id)}),
             PQclear);

    if (PQntuples(r.get()) == 0)
    {
        return std::nullopt;
    }

    return field(r.get(), 0, 0);
}

std::string Store::stats_json()
{
    std::lock_guard lock(mutex_);
    Result r(query(R"SQL(SELECT json_build_object(
        'queued',count(*) FILTER (WHERE status='queued'),
        'running',count(*) FILTER (WHERE status='running'),
        'completed',count(*) FILTER (WHERE status='completed'),
        'failed',count(*) FILTER (WHERE status='failed'))::text FROM jobs)SQL",
                   {}),
             PQclear);
    return field(r.get(), 0, 0);
}
} // namespace dts
