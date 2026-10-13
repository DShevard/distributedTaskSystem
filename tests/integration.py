#!/usr/bin/env python3
"""Run after: docker compose up -d --build --scale worker=3"""
import json
import time
import urllib.request

BASE = "http://127.0.0.1:8080"

def request(method, path, data=None):
    content = None if data is None else json.dumps(data).encode()

    req = urllib.request.Request(
        BASE + path,
        data=content,
        method=method,
        headers={"Content-Type": "application/json"},
    )

    with urllib.request.urlopen(req, timeout=5) as result:
        return json.load(result)

def main():
    assert request("GET", "/health")["status"] == "ok"

    cases = [
        ({"name": "discriminant", "a": 2, "b": 5, "c": 3}, 1),
        ({"name": "discriminant", "a": 1, "b": 2, "c": 1}, 0),
        ({"name": "discriminant", "a": 1, "b": 2, "c": 3}, -8),
        ({"name": "determinant", "a": 1, "b": 2, "c": 3, "d": 4}, -2),
        ({"name": "determinant", "a": 2, "b": 0, "c": 0, "d": 3}, 6),
        ({"name": "determinant", "a": 1, "b": 0, "c": 0, "d": 1}, 1),
    ]

    expected = {}

    for data, result in cases:
        data["duration_ms"] = 500
        response = request("POST", "/tasks", data)
        expected[response["id"]] = result

    ids = list(expected)
    deadline = time.monotonic() + 35

    while time.monotonic() < deadline:
        jobs = [request("GET", f"/tasks/{i}") for i in ids]

        if all(job["status"] == "completed" for job in jobs):
            for job in jobs:
                actual = float(job["result"])
                assert abs(actual - expected[job["id"]]) < 1e-6, (f"Wrong result for task {job['id']}: {actual}")

            print("Integration OK:", ids)
            print("Metrics:", request("GET", "/metrics"))
            return
        
        time.sleep(0.5)
        
    raise AssertionError(f"Tasks not completed: {jobs}")

if __name__ == "__main__":
    main()
