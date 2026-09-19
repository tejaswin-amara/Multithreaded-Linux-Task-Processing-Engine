# Builder Stage
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source code
COPY . .

# Compile
RUN make clean && make all

# Runtime Stage
FROM ubuntu:24.04

# Install runtime dependencies (libc6 and libpthread are in the base image)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6 \
    && rm -rf /var/lib/apt/lists/*

# Create unprivileged user and group
RUN groupadd -r taskengine && useradd -r -g taskengine taskengine

WORKDIR /app

# Copy compiled binary from builder
COPY --from=builder /build/bin/task_engine /usr/local/bin/task_engine

# Copy web assets
COPY --from=builder /build/web /app/web

# Expose default HTTP port
EXPOSE 8080

# Switch to unprivileged user
USER taskengine

# Set entrypoint
ENTRYPOINT ["/usr/local/bin/task_engine"]
