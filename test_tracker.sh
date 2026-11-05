#!/bin/bash
# test_tracker.sh - manual protocol tests for the tracker using netcat.
# Usage: start the tracker first (cd TrackerFolder && ./tracker), then run
#        ./test_tracker.sh [host] [port]
HOST=${1:-localhost}
PORT=${2:-3490}

# send one message, print the reply (the tracker closes the connection after replying, so no nc flags are needed; works with macOS and Linux nc)
send() {
    echo ">>> $1"
    printf '%s\n' "$1" | nc "$HOST" "$PORT"
    echo
}

echo "===== createtracker ====="
send "<createtracker movie1.avi 109283519 Ghost_and_the_Darkness,_DVD_Rip c68c2ee8bfca4e898b396e7a935a1d92 202.141.60.10 2097>"
send "<createtracker movie1.avi 109283519 Ghost_and_the_Darkness,_DVD_Rip c68c2ee8bfca4e898b396e7a935a1d92 202.141.60.10 2097>"   # ferr
send "createtracker small.txt 2048 a_small_file 0123456789abcdef0123456789abcdef 10.0.0.1 5000"                                  # manual format
send "createtracker bad.txt notanumber desc 0123456789abcdef0123456789abcdef 10.0.0.1 5000"                                       # fail

echo "===== updatetracker ====="
send "<updatetracker movie1.avi 0 1024 10.126.155.17 20182>"     # new peer
send "<updatetracker movie1.avi 0 4096 10.126.155.17 20182>"     # same peer, new range
send "<updatetracker nothere.avi 0 1024 10.126.155.17 20182>"    # ferr
send "<updatetracker movie1.avi 5 1 10.126.155.17 20182>"        # fail: start > end

echo "===== LIST ====="
send "<REQ LIST>"

echo "===== GET ====="
send "<GET movie1.avi.track >"
send "GET nothere.avi.track"