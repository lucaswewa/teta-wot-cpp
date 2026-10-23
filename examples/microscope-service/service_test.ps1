# Installs the simulated microscope as a Windows service, starts it, uses it,
# stops it during a long move and removes it, checking that it stops
# gracefully.
#
# Needs an administrator (GitHub's Windows runners are). From the repository
# root:
#
#     cmake --build --preset windows-msvc --target example-microscope-service
#     powershell -ExecutionPolicy Bypass -File examples\microscope-service\service_test.ps1
#
# Exits with a non-zero status if a step fails. The port of teta-wot's
# script.

param(
    [string]$Exe = "build\windows-msvc\examples\microscope-service\example-microscope-service.exe",
    [int]$Port = 5123
)

$ErrorActionPreference = "Stop"
$name = "teta-wot-microscope"
$exe = (Resolve-Path $Exe).Path
$log = Join-Path (Split-Path $exe) "microscope-service.log"
$base = "http://127.0.0.1:$Port"
Remove-Item $log -ErrorAction SilentlyContinue

& $exe install --port $Port
if ($LASTEXITCODE -ne 0) { throw "install failed" }
try {
    Start-Service $name
    (Get-Service $name).WaitForStatus("Running", "00:00:30")
    Write-Output "the service is running"

    $ready = $false
    for ($i = 0; $i -lt 120 -and -not $ready; $i++) {
        try { Invoke-RestMethod "$base/stage/" | Out-Null; $ready = $true }
        catch { Start-Sleep -Milliseconds 250 }
    }
    if (-not $ready) { throw "the service doesn't answer on $base" }
    $td = Invoke-RestMethod "$base/camera/"
    Write-Output "GET /camera/: $($td.title)"

    # A long move, which stopping must cancel rather than wait for.
    $invocation = Invoke-RestMethod -Method Post -ContentType "application/json" `
        -Body '{"x": 100000, "y": 0, "z": 0}' "$base/stage/move_to"
    Write-Output "started a long move: $($invocation.status)"

    $watch = [Diagnostics.Stopwatch]::StartNew()
    Stop-Service $name
    (Get-Service $name).WaitForStatus("Stopped", "00:00:30")
    Write-Output ("the service stopped in {0:N1} s" -f $watch.Elapsed.TotalSeconds)

    $exit = (Get-CimInstance Win32_Service -Filter "Name='$name'").ExitCode
    if ($exit -ne 0) { throw "the service's exit code is $exit" }
    $text = Get-Content $log -Raw
    foreach ($expected in @("asked the service to stop", "shutting down", "was cancelled", "stopped")) {
        if ($text -notmatch [regex]::Escape($expected)) { throw "the log has no '$expected':`n$text" }
    }
    Write-Output "the log shows a graceful stop"
}
catch {
    # What the service did, for the failure's diagnosis.
    Write-Output "the service: $((Get-Service $name -ErrorAction SilentlyContinue).Status)"
    Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue | Format-Table Id, StartTime, Responding
    Get-NetTCPConnection -LocalPort $Port -ErrorAction SilentlyContinue | Format-Table LocalAddress, State, OwningProcess
    Get-WinEvent -FilterHashtable @{LogName = "System"; ProviderName = "Service Control Manager"} -MaxEvents 8 `
        -ErrorAction SilentlyContinue | Format-List TimeCreated, Message
    if (Test-Path $log) {
        Write-Output "the service's log:"
        Get-Content $log -Tail 80
    } else {
        Write-Output "no log at $log"
    }
    throw
}
finally {
    & $exe uninstall
}
