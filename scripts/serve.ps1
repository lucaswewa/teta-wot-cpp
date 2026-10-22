<#
.SYNOPSIS
Serves Things for manual testing, until Ctrl-C.

.DESCRIPTION
Serves one of:

- an example (-Example), as `example-<name> --serve`;
- the C++ twin server with a scenario's configuration (-Scenario), a
  configuration file (-Config), or chosen twins (-Things), by default all of
  them;
- the same, from the Rust reference server (-Reference), to compare by hand:
  serve both on different ports and send them the same requests.

Generated configurations keep the reference's settings files in a temporary
folder. Ctrl-C stops the server gracefully. scripts\probe.ps1 explores what
is served.

.PARAMETER Example
quickstart-counter, light, counter-server, http-walkthrough or
drogon-integration. They serve on port 5000 (counter-server on -Port).

.PARAMETER Scenario
A scenario of conformance/scenarios/http (default, locked, prefixed, wot,
counter, validation), whose configuration is served.

.PARAMETER Config
A configuration file, as teta-wot reads it.

.PARAMETER Things
Twins to serve, by name: mything, broken, counter, types, http_probe,
settings_probe, ws_probe. All of them by default.

.PARAMETER Prefix
An API prefix for -Things, such as /api/v1.

.PARAMETER WireProfile
tetathing (the default) or wot, for -Things.

.PARAMETER GlobalLock
Enable the global lock, for -Things.

.PARAMETER Reference
Serve with the Rust reference server instead of the C++ twin server.

.PARAMETER Port
The port (5000 by default).

.EXAMPLE
scripts\serve.ps1 -Example light

.EXAMPLE
scripts\serve.ps1 -Things counter,http_probe -GlobalLock

.EXAMPLE
scripts\serve.ps1 -Scenario default -Port 5001 -Reference
#>
param(
    [ValidateSet('quickstart-counter', 'light', 'counter-server', 'http-walkthrough', 'drogon-integration')]
    [string]$Example,
    [string]$Scenario,
    [string]$Config,
    [string[]]$Things,
    [string]$Prefix,
    [ValidateSet('tetathing', 'wot')]
    [string]$WireProfile,
    [switch]$GlobalLock,
    [switch]$Reference,
    [int]$Port = 5000,
    [string]$Address = '127.0.0.1',
    [string]$Preset
)

. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $Preset) { $Preset = Get-DefaultPreset }
if ($Things) { $Things = Split-List $Things }

$sources = @(@($Example, $Scenario, $Config) | Where-Object { $_ })
if ($sources.Count -gt 1 -or ($sources.Count -eq 1 -and $Things)) {
    throw 'Choose one of -Example, -Scenario, -Config and -Things'
}

if ($Example) {
    if ($Reference) { throw "-Reference serves twins; serve the Rust example from teta-wot's repository" }
    $executable = Get-BuiltExecutable $Preset "examples\$Example\example-$Example"
    $arguments = @('--serve')
    if ($Example -eq 'counter-server') {
        $arguments += @('--port', $Port)
    } elseif ($Port -ne 5000) {
        Write-Warning "$Example always serves on port 5000"
    }
    if ($Example -ne 'counter-server') { $Port = 5000 }
    Write-Host "Serving the $Example example on http://127.0.0.1:$Port/ (Ctrl-C stops it)" -ForegroundColor Green
    & $executable @arguments
    exit $LASTEXITCODE
}

# A configuration for the twin server or the reference.
$folder = Join-Path ([System.IO.Path]::GetTempPath()) "teta-wot-serve-$PID"
New-Item -ItemType Directory -Force -Path $folder | Out-Null
if ($Config) {
    $configPath = (Resolve-Path $Config).Path
    $what = $configPath
} else {
    if ($Scenario) {
        $file = Join-Path $RepoRoot "conformance\scenarios\http\$Scenario.json"
        if (-not (Test-Path $file)) {
            $known = (Get-ChildItem (Join-Path $RepoRoot 'conformance\scenarios\http') -Filter *.json |
                    ForEach-Object { $_.BaseName }) -join ', '
            throw "No scenario named $Scenario (there are: $known)"
        }
        $configuration = (Get-Content -Raw -Encoding UTF8 -Path $file | ConvertFrom-Json).config
        $what = "the $Scenario scenario's Things"
    } else {
        if (-not $Things) { $Things = @($TwinClasses.Keys) }
        $map = [ordered]@{}
        foreach ($name in $Things) {
            if (-not $TwinClasses.Contains($name)) {
                throw "No twin named $name (there are: $($TwinClasses.Keys -join ', '))"
            }
            $map[$name] = $TwinClasses[$name]
        }
        $configuration = [pscustomobject]@{ things = $map; server_id = 'manual' }
        if ($Prefix) { $configuration | Add-Member api_prefix $Prefix }
        if ($WireProfile) { $configuration | Add-Member wire_profile $WireProfile }
        if ($GlobalLock) { $configuration | Add-Member enable_global_lock $true }
        $what = "the twins $($Things -join ', ')"
    }
    # The reference saves settings files: keep them out of the repository.
    $configuration | Add-Member -Force settings_folder (Join-Path $folder 'settings')
    $configPath = Join-Path $folder 'config.json'
    Write-Utf8File $configPath ($configuration | ConvertTo-Json -Depth 20)
}

if ($Reference) {
    $server = Get-ReferenceServer
    $who = 'the Rust reference server'
} else {
    $server = Get-BuiltExecutable $Preset 'conformance\twin-server-cpp\twin-server'
    $who = 'the C++ twin server'
}
$prefixShown = ''
if ($Prefix) { $prefixShown = $Prefix }
Write-Host "Serving $what with $who on http://${Address}:$Port$prefixShown/ (Ctrl-C stops it)" -ForegroundColor Green
Write-Host "Configuration: $configPath" -ForegroundColor DarkGray
try {
    & $server -c $configPath --host $Address --port $Port
    $code = $LASTEXITCODE
} finally {
    Remove-Item -Recurse -Force $folder -ErrorAction SilentlyContinue
}
exit $code
