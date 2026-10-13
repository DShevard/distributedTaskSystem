FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make libpq-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . ./
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 2 && ctest --test-dir build --output-on-failure

FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends libpq5 ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/scheduler /usr/local/bin/scheduler
COPY --from=build /src/build/api /usr/local/bin/api
COPY --from=build /src/build/worker /usr/local/bin/worker
USER 65534:65534
CMD ["scheduler"]
