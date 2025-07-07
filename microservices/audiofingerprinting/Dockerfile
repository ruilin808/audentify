# Multi-stage build for C++ application with database initialization
FROM ubuntu:22.04 AS build

# Avoid interactive prompts during package installation
ARG DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libfftw3-dev \
    libcurl4-openssl-dev \
    libtag1-dev \
    libsqlite3-dev \
    git \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code (excluding build directory)
COPY CMakeLists.txt .
COPY src/ src/
COPY lib/ lib/

# Copy sample songs for database initialization (optional)
COPY sample_songs/ sample_songs/

# Clean any existing build artifacts and create fresh build directory
RUN rm -rf build && \
    mkdir -p build && \
    cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Initialize database with sample songs if they exist
RUN mkdir -p /app/data && \
    if [ -d "/app/sample_songs" ] && [ "$(ls -A /app/sample_songs)" ]; then \
        echo "Initializing database with sample songs..." && \
        /app/build/audioFingerprintingCLI register /app/sample_songs --db /app/data/fingerprints.db; \
    else \
        echo "No sample songs found, creating empty database..." && \
        touch /app/data/fingerprints.db; \
    fi

# Runtime stage
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libfftw3-3 \
    libcurl4 \
    libtag1v5 \
    libsqlite3-0 \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create app user
RUN groupadd -g 1001 appgroup && \
    useradd -u 1001 -g appgroup -m appuser

# Set working directory
WORKDIR /app

# Copy built executables from build stage
COPY --from=build /app/build/audioFingerprintingServer /app/
COPY --from=build /app/build/audioFingerprintingCLI /app/

# Copy initialized database from build stage
COPY --from=build /app/data/ /app/data/

# Create necessary directories and set permissions
RUN mkdir -p /app/temp && \
    chown -R appuser:appgroup /app

# Switch to app user
USER appuser

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Expose port
EXPOSE 8080

# Run the server
CMD ["./audioFingerprintingServer", "--port", "8080", "--db", "/app/data/fingerprints.db"]