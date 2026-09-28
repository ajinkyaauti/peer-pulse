#!/bin/sh
set -e

# Start the C++ TCP/TLS server in the background and forward signals to it.
./build/p2p_server 8080 8081 &
TCP_PID=$!

trap 'kill -TERM "$TCP_PID" 2>/dev/null; wait "$TCP_PID" 2>/dev/null; exit 0' TERM INT

# Run the Flask app in the foreground so it stays PID-managed by the container.
python3 web_server.py &
WEB_PID=$!

wait "$WEB_PID"
kill -TERM "$TCP_PID" 2>/dev/null
wait "$TCP_PID" 2>/dev/null
