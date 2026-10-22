# Every route of the HTTP binding, with curl.exe (shipped with Windows 10 and
# later). Start the server first:
#
#     cargo run -p http-walkthrough -- --serve
#
# then run this script in Windows PowerShell or PowerShell 7.
$ErrorActionPreference = "Stop"
$Base = if ($env:BASE) { $env:BASE } else { "http://127.0.0.1:5000" }
function Show { Write-Output ""; Write-Output "`$ curl $args"; curl.exe -s -i @args; Write-Output "" }
# Writes a JSON body to a temporary file: quoting JSON for curl.exe on the
# Windows command line is fragile.
$BodyFile = Join-Path ([System.IO.Path]::GetTempPath()) "wot-walkthrough-body.json"
function Body([string]$Json) { Set-Content -Path $BodyFile -Value $Json -NoNewline; "@$BodyFile" }

Write-Output "## Discovery: the Thing list and the Thing Description"
Show "$Base/things/"
Show "$Base/lamp/"

Write-Output "## Properties: read, write (with lax coercion), invalid values, reset"
Show "$Base/lamp/brightness"
Show -X PUT -H "Content-Type: application/json" --data-binary (Body '"80"') "$Base/lamp/brightness"
Show -X PUT -H "Content-Type: application/json" --data-binary (Body '150') "$Base/lamp/brightness"
Show -X PUT -H "Content-Type: application/json" --data-binary (Body 'true') "$Base/lamp/is_on"
Show -X POST "$Base/lamp/brightness/reset"

Write-Output "## Actions: invoke, poll, output, list"
$id = (Invoke-RestMethod -Method Post "$Base/lamp/toggle").id
Start-Sleep -Milliseconds 100
Show "$Base/action_invocations/$id"
Show "$Base/action_invocations/$id/output"
Show "$Base/lamp/toggle"
Show -X POST -H "Content-Type: application/json" --data-binary (Body '{"to": "high"}') "$Base/lamp/fade"

Write-Output "## Cancelling a long invocation"
$id = (Invoke-RestMethod -Method Post -ContentType "application/json" -Body '{"to": 100}' "$Base/lamp/fade").id
Start-Sleep -Milliseconds 300
Show -X DELETE "$Base/action_invocations/$id"
Start-Sleep -Milliseconds 100
Show "$Base/action_invocations/$id"
Show -X DELETE "$Base/action_invocations/$id"

Write-Output "## Errors and routing: 404, 405, 307, 422"
Show "$Base/action_invocations/00000000-0000-0000-0000-000000000000"
Show "$Base/action_invocations/not-a-uuid"
Show -X DELETE "$Base/lamp/brightness"
Show "$Base/lamp"

Write-Output "## CORS: a preflight, and a request with an Origin"
Show -X OPTIONS -H "Origin: http://example.com" -H "Access-Control-Request-Method: PUT" "$Base/lamp/brightness"
Show -H "Origin: http://example.com" "$Base/lamp/is_on"
