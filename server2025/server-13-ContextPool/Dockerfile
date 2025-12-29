FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libsqlite3-dev \
    curl \
    python3 \
    jq \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source
COPY . /app

# Build
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Expose port
EXPOSE 8080

# Environment variables
ENV MODEL_PATH=/app/models/smollm-360m-q4.gguf
ENV PORT=8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
  CMD curl -f http://localhost:8080/ || exit 1

# Start
CMD ["./build/ai_infra_server"]