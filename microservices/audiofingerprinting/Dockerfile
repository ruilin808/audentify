# Multi-stage build for C++ application with auto-download dr_libs
FROM ubuntu:22.04 AS build

# Avoid interactive prompts during package installation
ARG DEBIAN_FRONTEND=noninteractive

# Install build dependencies including wget for downloading headers
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
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code (excluding build directory)
COPY CMakeLists.txt .
COPY src/ src/

# Copy lib directory if it exists, otherwise create empty one
COPY lib* lib/

# Download dr_libs headers if not present
RUN echo "Checking for dr_libs headers..." && \
    if [ ! -f "lib/dr_libs/dr_wav.h" ] || [ ! -f "lib/dr_libs/dr_mp3.h" ] || [ ! -f "lib/dr_libs/dr_flac.h" ]; then \
        echo "❌ dr_libs headers not found, downloading from official repository..." && \
        mkdir -p lib/dr_libs && \
        echo "📥 Downloading dr_wav.h..." && \
        wget -q --timeout=30 --tries=3 -O lib/dr_libs/dr_wav.h \
            "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h" && \
        echo "📥 Downloading dr_mp3.h..." && \
        wget -q --timeout=30 --tries=3 -O lib/dr_libs/dr_mp3.h \
            "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h" && \
        echo "📥 Downloading dr_flac.h..." && \
        wget -q --timeout=30 --tries=3 -O lib/dr_libs/dr_flac.h \
            "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_flac.h" && \
        echo "✅ Successfully downloaded all dr_libs headers" && \
        ls -la lib/dr_libs/; \
    else \
        echo "✅ dr_libs headers found, using existing files" && \
        ls -la lib/dr_libs/; \
    fi

# Verify all required headers are present
RUN echo "🔍 Verifying dr_libs headers..." && \
    for header in dr_wav.h dr_mp3.h dr_flac.h; do \
        if [ ! -f "lib/dr_libs/$header" ]; then \
            echo "❌ Missing required header: $header" && exit 1; \
        else \
            echo "✅ Found: $header ($(wc -l < lib/dr_libs/$header) lines)"; \
        fi; \
    done

# Copy sample songs for database initialization (optional)
COPY sample_songs* sample_songs/

# Clean any existing build artifacts and create fresh build directory
RUN echo "🏗️ Building C++ application..." && \
    rm -rf build && \
    mkdir -p build && \
    cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    echo "✅ Build completed successfully"

# Initialize database with sample songs if they exist
RUN mkdir -p /app/data && \
    if [ -d "/app/sample_songs" ] && [ "$(find /app/sample_songs -name '*.mp3' -o -name '*.wav' -o -name '*.flac' -o -name '*.m4a' | wc -l)" -gt 0 ]; then \
        echo "🎵 Initializing database with sample songs..." && \
        song_count=$(find /app/sample_songs -name '*.mp3' -o -name '*.wav' -o -name '*.flac' -o -name '*.m4a' | wc -l) && \
        echo "Found $song_count audio files for registration" && \
        /app/build/audioFingerprintingCLI register /app/sample_songs --db /app/data/fingerprints.db && \
        echo "✅ Database initialized with sample songs"; \
    else \
        echo "ℹ️ No sample songs found, creating empty database..." && \
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