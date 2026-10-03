# --- Stage 1: build the C++ TCP/TLS server ---
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include/ include/
COPY src/ src/

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target p2p_server

# --- Stage 2: runtime image with Python web server + compiled binary ---
FROM python:3.12-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY --from=builder /src/build/p2p_server ./build/p2p_server
COPY web_server.py ./
COPY web/ web/
COPY certs/ certs/

RUN mkdir -p uploads data \
    && useradd --create-home --uid 1000 appuser \
    && chown -R appuser:appuser /app
USER appuser

COPY --chown=appuser:appuser docker-entrypoint.sh ./
RUN chmod +x docker-entrypoint.sh

ENV P2P_WEB_HOST=0.0.0.0 \
    P2P_WEB_PORT=5000 \
    P2P_WEB_DEBUG=false \
    P2P_AUTH_DB=/app/data/p2p_auth.db

EXPOSE 5000 8080 8081

ENTRYPOINT ["./docker-entrypoint.sh"]
