#!/usr/bin/env bash
# Every route of the HTTP binding, with curl. Start the server first:
#
#     cargo run -p http-walkthrough -- --serve
#
# then run this script (it needs curl and, for the invocation IDs, jq).
set -euo pipefail
BASE="${BASE:-http://127.0.0.1:5000}"
show() { echo; echo "\$ curl $*"; curl -s -i "$@"; echo; }

echo "## Discovery: the Thing list and the Thing Description"
show "$BASE/things/"
show "$BASE/lamp/"

echo "## Properties: read, write (with lax coercion), invalid values, reset"
show "$BASE/lamp/brightness"
show -X PUT -H 'Content-Type: application/json' -d '"80"' "$BASE/lamp/brightness"
show -X PUT -H 'Content-Type: application/json' -d '150' "$BASE/lamp/brightness"
show -X PUT -H 'Content-Type: application/json' -d 'true' "$BASE/lamp/is_on"
show -X POST "$BASE/lamp/brightness/reset"

echo "## Actions: invoke, poll, output, list"
id=$(curl -s -X POST "$BASE/lamp/toggle" | jq -r .id)
sleep 0.1
show "$BASE/action_invocations/$id"
show "$BASE/action_invocations/$id/output"
show "$BASE/lamp/toggle"
show -X POST -H 'Content-Type: application/json' -d '{"to": "high"}' "$BASE/lamp/fade"

echo "## Cancelling a long invocation"
id=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"to": 100}' "$BASE/lamp/fade" | jq -r .id)
sleep 0.3
show -X DELETE "$BASE/action_invocations/$id"
sleep 0.1
show "$BASE/action_invocations/$id"
show -X DELETE "$BASE/action_invocations/$id"

echo "## Errors and routing: 404, 405, 307, 422"
show "$BASE/action_invocations/00000000-0000-0000-0000-000000000000"
show "$BASE/action_invocations/not-a-uuid"
show -X DELETE "$BASE/lamp/brightness"
show "$BASE/lamp"

echo "## CORS: a preflight, and a request with an Origin"
show -X OPTIONS -H 'Origin: http://example.com' -H 'Access-Control-Request-Method: PUT' "$BASE/lamp/brightness"
show -H 'Origin: http://example.com' "$BASE/lamp/is_on"
