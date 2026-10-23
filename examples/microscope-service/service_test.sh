#!/usr/bin/env bash
# Installs the simulated microscope as a systemd service, starts it, uses it,
# stops it during a long move and removes it, checking that it stops
# gracefully: the Linux counterpart of service_test.ps1.
#
# Needs systemd and sudo (GitHub's Ubuntu runners have both). From the
# repository root:
#
#     cmake --build --preset linux-gcc --target example-microscope-service
#     examples/microscope-service/service_test.sh
#
# Exits with a non-zero status if a step fails.

set -euo pipefail

exe=$(realpath "${1:-build/linux-gcc/examples/microscope-service/example-microscope-service}")
port=${2:-5123}
name=teta-wot-microscope
base=http://127.0.0.1:$port

sudo "$exe" install --port "$port" --user "$(id -un)"
trap 'sudo "$exe" uninstall' EXIT
since=$(date '+%Y-%m-%d %H:%M:%S')

# Type=notify: `start` returns once the server has said READY=1, so it
# answers at once.
sudo systemctl start "$name"
echo "the service is $(systemctl is-active "$name")"
title=$(curl -fsS "$base/camera/" | python3 -c 'import json, sys; print(json.load(sys.stdin)["title"])')
echo "GET /camera/: $title"

# A long move, which stopping must cancel rather than wait for.
status=$(curl -fsS -X POST -H 'Content-Type: application/json' -d '{"x": 100000, "y": 0, "z": 0}' \
    "$base/stage/move_to" | python3 -c 'import json, sys; print(json.load(sys.stdin)["status"])')
echo "started a long move: $status"

start=$(date +%s.%N)
sudo systemctl stop "$name"
echo "the service stopped in $(python3 -c "print(f'{$(date +%s.%N) - $start:.1f}')") s"

result=$(systemctl show -p Result --value "$name")
code=$(systemctl show -p ExecMainStatus --value "$name")
if [ "$result" != success ] || [ "$code" != 0 ]; then
    echo "the service stopped with result $result and exit code $code" >&2
    exit 1
fi
log=$(sudo journalctl -u "$name" --since "$since" --no-pager -o cat)
for expected in "serving 3 Things" "shutting down" "was cancelled" "stopped"; do
    if ! grep -qF "$expected" <<<"$log"; then
        printf "the journal has no '%s':\n%s\n" "$expected" "$log" >&2
        exit 1
    fi
done
echo "the journal shows a graceful stop"
