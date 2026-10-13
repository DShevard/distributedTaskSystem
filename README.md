# DistributedTaskSystem

A distributed task processing system built with C++20, PostgreSQL, TCP sockets, Docker, and GitHub Actions.

The system accepts computational tasks through an HTTP API, distributes them across independent worker processes, executes mathematical operations, tracks execution progress, and automatically retries failed tasks.

## Tech Stack

- C++20
- PostgreSQL
- TCP/IP
- HTTP REST API
- Docker & Docker Compose
- CMake
- Python 3 (integration testing)
- GitHub Actions (CI)

## Features

- HTTP API for task submission and status retrieval
- Mathematical task execution
- Priority-based task scheduling
- Multiple independent worker processes
- Concurrent execution across workers
- TCP communication between Scheduler and Workers
- Worker heartbeats
- Task leases and automatic recovery
- Configurable retry limits
- PostgreSQL persistence
- Execution history
- Protection against outdated execution results
- Docker-based deployment
- Automated builds and integration testing

## Architecture

```text
                  HTTP Client
                       |
                       | HTTP
                       v
                  +----------+
                  |   API    |
                  |  :8080   |
                  +----------+
                       |
                       v
                 +------------+
                 | PostgreSQL |
                 +------------+
                       ^
                       |
                 +------------+
                 | Scheduler  |
                 | :9000 TCP  |
                 +------------+
                       |
             +---------+---------+
             |         |         |
             v         v         v
          Worker 1  Worker 2  Worker 3
             |         |         |
             v         v         v
         Execute    Execute    Execute
           tasks      tasks      tasks
```

### Components

**API**

Accepts HTTP requests, validates task types and coefficients, stores tasks in PostgreSQL, and provides task status and system statistics.

**Scheduler**

Retrieves queued tasks from PostgreSQL, prioritizes them, assigns them to available Workers, and manages execution attempts and leases.

**Workers**

Independent C++ processes that connect to Scheduler through TCP, receive task parameters, execute mathematical operations, send heartbeats, and report results.

**PostgreSQL**

Stores task parameters, execution statuses, retry counters, lease information, computed results, and execution history.

### Communication

- HTTP clients communicate with the API using JSON.
- The API and Scheduler access PostgreSQL independently.
- Scheduler and Workers communicate through a custom line-based TCP protocol.
- Workers do not access PostgreSQL directly.

## Supported Tasks

The system currently supports two mathematical operations.

### 1. Discriminant

Computes the discriminant of a quadratic expression:

```text
D = b² - 4ac
```

Required parameters: `a`, `b`, `c`.

Example:

```text
a = 2, b = 5, c = 3
D = 25 - 24 = 1
```

### 2. Matrix Determinant (2×2)

Computes the determinant of a 2×2 matrix:

```text
| a  b |
| c  d |

D = ad - bc
```

Required parameters: `a`, `b`, `c`, `d`.

Example:

```text
a = 1, b = 2, c = 3, d = 4
D = 4 - 6 = -2
```

Both operations are implemented in C++ and executed by Worker processes.

## Project Structure

```text
distributedTaskSystem/
├── .github/
│   └── workflows/
│       └── ci.yml
├── include/
│   ├── common.hpp
│   └── store.hpp
├── src/
│   ├── api_main.cpp
│   ├── common.cpp
│   ├── scheduler_main.cpp
│   ├── store.cpp
│   └── worker_main.cpp
├── sql/
│   └── schema.sql
├── tests/
│   ├── integration.py
│   └── unit.cpp
├── .dockerignore
├── .gitignore
├── CMakeLists.txt
├── Dockerfile
├── compose.yaml
└── README.md
```

## Requirements

- Docker
- Docker Compose
- Python 3 (to run the integration test locally)

The application runs in Linux containers and can be launched on macOS, Linux, or Windows with Docker installed.

## Quick Start

### 1. Clone the repository

```bash
git clone https://github.com/DShevard/distributedTaskSystem.git
cd distributedTaskSystem
```

### 2. Build and start the system

```bash
docker compose up -d --build --scale worker=3
```

Docker Compose starts:

- PostgreSQL
- HTTP API
- Scheduler
- Three Worker instances

### 3. Check running containers

```bash
docker compose ps
```

### 4. Verify API availability

```bash
curl http://localhost:8080/health
```

Expected response:

```json
{"status":"ok"}
```

## Usage

### Create a Discriminant Task

```bash
curl -X POST http://localhost:8080/tasks \
  -H "Content-Type: application/json" \
  -d '{"name":"discriminant","a":2,"b":5,"c":3,"priority":10,"duration_ms":1000,"max_attempts":3}'
```

Example response:

```json
{
  "id": 1,
  "status": "queued"
}
```

### Create a Determinant Task

```bash
curl -X POST http://localhost:8080/tasks \
  -H "Content-Type: application/json" \
  -d '{"name":"determinant","a":1,"b":2,"c":3,"d":4}'
```

Expected mathematical result:

```text
-2
```

### Task Parameters

| Parameter | Description |
|---|---|
| `name` | Task type: `discriminant` or `determinant` |
| `a`, `b`, `c` | Required numerical coefficients |
| `d` | Required for `determinant` |
| `priority` | Scheduling priority, from -100 to 100 (default: 0) |
| `duration_ms` | Simulated processing delay before calculation, from 0 to 300000 (default: 1000) |
| `max_attempts` | Maximum execution attempts, from 1 to 10 (default: 3) |

The API rejects unsupported task types and requests missing required coefficients.

### Check Task Status

```bash
curl http://localhost:8080/tasks/1
```

Example completed discriminant task:

```json
{
  "id": 1,
  "name": "discriminant",
  "priority": 10,
  "duration_ms": 1000,
  "a": 2,
  "b": 5,
  "c": 3,
  "d": 0,
  "status": "completed",
  "attempt": 1,
  "max_attempts": 3,
  "worker_name": "worker",
  "result": "1.000000"
}
```

The actual response also includes timestamps and other task information. The worker name depends on the service configuration.

### System Metrics

```bash
curl http://localhost:8080/metrics
```

Example response:

```json
{
  "queued": 0,
  "running": 0,
  "completed": 6,
  "failed": 0
}
```

The metrics cover all tasks currently stored in PostgreSQL.

## Task Lifecycle

```text
HTTP POST /tasks
       |
       v
Task stored in PostgreSQL
       |
       v
Status: QUEUED
       |
       v
Scheduler claims task
       |
       v
Status: RUNNING
       |
       v
Scheduler sends TASK over TCP
       |
       v
Worker receives coefficients
       |
       +---- Simulated delay and PING
       |
       v
Worker performs calculation
       |
       v
Worker sends DONE with result
       |
       v
Scheduler stores result
       |
       v
Status: COMPLETED
```

Tasks are initially stored with status `queued`.

The Scheduler claims tasks using PostgreSQL row-level locking and `FOR UPDATE SKIP LOCKED`, helping avoid concurrent claims of the same task.

Workers receive task identifiers, attempt numbers, task types, durations, and coefficients.

After execution, results are stored in PostgreSQL and can be retrieved through the HTTP API.

## Fault Tolerance

The system implements task leases, heartbeats, and retries.

Workers periodically send heartbeat messages during the configured simulated processing delay.

The Scheduler extends active leases when valid heartbeats are received.

If a Worker disconnects or its task lease expires, the system can requeue the task for another execution attempt.

### Failure Recovery Example

```text
Task assigned to Worker 1
           |
           v
      Worker crashes
           |
           v
   Scheduler detects failure
           |
           v
       Task requeued
           |
           v
   Task assigned to Worker 2
           |
           v
      Task completed
```

Execution attempts are identified by task ID and attempt number.

This prevents results from outdated execution attempts from overwriting the result of a newer attempt.

The system follows an at-least-once execution model. Tasks may execute more than once after failures.

The mathematical operations are short. Supporting long-running computational workloads would require heartbeats to continue independently during calculation.

## Testing

### Unit Tests

The project includes unit tests for common utility functions, including:

- Protocol message splitting
- Task and worker name validation

Unit tests are executed through CTest during Docker image building.

### Integration Tests

Start the system:

```bash
docker compose up -d --build --scale worker=3
```

Run:

```bash
python3 tests/integration.py
```

The integration test:

1. Checks API availability.
2. Submits six mathematical tasks.
3. Waits for all tasks to complete.
4. Validates calculated results against expected values.
5. Retrieves system metrics.

Example successful output:

```text
Integration OK: [1, 2, 3, 4, 5, 6]
Metrics: {'queued': 0, 'running': 0, 'completed': 6, 'failed': 0}
```

### Continuous Integration

GitHub Actions automatically builds and tests the project on push and pull request events.

The workflow is located at:

```text
.github/workflows/ci.yml
```

The CI workflow:

1. Checks out the repository.
2. Builds Docker images and runs CTest.
3. Starts PostgreSQL, API, Scheduler, and three Workers.
4. Waits for the HTTP API to become available.
5. Submits a discriminant task.
6. Verifies successful completion and the calculated result.
7. Stops the services.

## Docker Commands

Start the system:

```bash
docker compose up -d --build --scale worker=3
```

View running containers:

```bash
docker compose ps
```

View logs:

```bash
docker compose logs -f
```

Stop the system:

```bash
docker compose down
```

Stop the system and delete PostgreSQL data:

```bash
docker compose down -v
```

**Warning:** The `-v` flag removes the database volume and its stored data.

## Limitations

This project is intended for educational use.

- Only two predefined mathematical operations are supported.
- Task coefficients are stored in fixed database columns rather than a generic payload format.
- Workers still use a configurable simulated delay before performing calculations.
- Heartbeats are not independent of computation.
- The HTTP API uses simplified regular-expression-based JSON parsing.
- TLS and authentication are not implemented.
- Worker names are not guaranteed to be unique.
- Logging and monitoring are basic.
- Public deployment is not recommended without additional security measures.

## Purpose

The project demonstrates practical concepts in:

- Distributed systems
- Concurrent programming
- TCP socket programming
- Custom application protocols
- Task scheduling
- PostgreSQL transactions and row locking
- Fault tolerance
- Docker containerization
- Automated testing and continuous integration

It provides a practical foundation for understanding how independent services coordinate computational work through a database-backed task queue and a scheduler.