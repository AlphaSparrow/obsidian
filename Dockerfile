# syntax=docker/dockerfile:1.4
# =============================================================================
# Obsidian Engine: Multi-Stage Production Container Build
# DDIA-compliant, hermetic, optimized quantitative finance platform runtime
# =============================================================================

# ── Stage 1: Hermetic Build Environment ───────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ccache \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/obsidian

# Copy source tree
COPY . .

# Configure and compile with deterministic server preset
RUN cmake -B build/docker-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DOBSIDIAN_CPU_TARGET=generic \
    -DOBSIDIAN_FP_MODEL=strict \
    -DOBSIDIAN_ENABLE_TESTS=ON \
    -DOBSIDIAN_ENABLE_LTO=OFF

RUN cmake --build build/docker-release --parallel $(nproc)

# Run internal verification suite in container builder
RUN ctest --test-dir build/docker-release --output-on-failure

# ── Stage 2: Hardened Minimal Production Runtime ──────────────────────────────
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    OBSIDIAN_HOME=/opt/obsidian \
    PATH="/opt/obsidian/bin:${PATH}"

# Runtime security: non-root system user
RUN groupadd -r obsidian && useradd -r -g obsidian -d /opt/obsidian -s /sbin/nologin obsidian

WORKDIR /opt/obsidian

# Install only minimal runtime shared libraries (libgomp, libc)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy compiled binaries and licenses from builder
COPY --from=builder /workspace/obsidian/build/docker-release/tests/obsidian_ddia_tests /opt/obsidian/bin/obsidian_ddia_tests
COPY --from=builder /workspace/obsidian/build/docker-release/tests/obsidian_lakehouse_tests /opt/obsidian/bin/obsidian_lakehouse_tests
COPY --from=builder /workspace/obsidian/LICENSE /opt/obsidian/LICENSE

# Create data directory for lakehouse storage
RUN mkdir -p /var/lib/obsidian/lakehouse && chown -R obsidian:obsidian /opt/obsidian /var/lib/obsidian

USER obsidian

ENTRYPOINT ["/opt/obsidian/bin/obsidian_ddia_tests"]
CMD ["--benchmark"]
