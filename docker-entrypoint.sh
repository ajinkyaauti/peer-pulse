#!/bin/bash
./build/p2p_server 8080 8081 &
TCP_PID=$!

python3 web_server.py &
WEB_PID=$!

trap 'kill -TERM $TCP_PID $WEB_PID 2>/dev/null; wait; exit 0' TERM INT

# Block until EITHER process exits
wait -n
echo "A process exited; stopping container so Render restarts it" >&2
kill -TERM $TCP_PID $WEB_PID 2>/dev/null
wait
exit 1