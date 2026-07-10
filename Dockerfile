# Stage 1: Build environment
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CMake configuration and source code
COPY CMakeLists.txt .
COPY src/ ./src/

# Build DrutaDB in Release mode
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Stage 2: Runtime environment
FROM ubuntu:24.04

# Create a non-root user for security
RUN useradd -m druta

WORKDIR /app

# Copy the compiled binary from the builder stage
COPY --from=builder /app/build/drutadb /app/drutadb

# Ensure data directory exists and set permissions
RUN mkdir -p /app/data && chown -R druta:druta /app

USER druta

# Expose the default Redis port
EXPOSE 6379

ENV DRUTA_BIND_IP=0.0.0.0

# Run DrutaDB
CMD ["./drutadb"]
