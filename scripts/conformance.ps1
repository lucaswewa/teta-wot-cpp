<#
.SYNOPSIS
Compares the C++ servers with the Rust reference (conformance/run.py):
against the captured fixtures, live against the reference, or recaptures the
fixtures.

.DESCRIPTION
Modes:

  check    the C++ twin server against the fixtures (the default)
  diff     the Rust reference and the C++ twin server, live, side by side
  capture  rewrites the fixtures from the Rust reference (review the diff!)

With -Examples, the examples that serve are compared instead: the C++
examples against fixtures captured from teta-wot's own examples, which diff
and capture build from a teta-wot v0.1.0 checkout (-TetaWot).

The reference server (conformance/reference-rs) and teta-wot's examples are
built with cargo when they are needed and missing.

.PARAMETER Mode
check (the default), diff or capture.

.PARAMETER Scenario
Scenarios to run. By default, every scenario whose Things the C++ twins
have (or, with -Examples, every example scenario).

.PARAMETER Examples
Compare the examples that serve, rather than the twin servers.

.PARAMETER TetaWot
A checkout of teta-wot at v0.1.0, for its examples (by default ..\teta-wot
next to this repository).

.EXAMPLE
scripts\conformance.ps1

.EXAMPLE
scripts\conformance.ps1 -Mode diff -Scenario default,locked

.EXAMPLE
scripts\conformance.ps1 -Examples -Mode diff
#>
param(
    [ValidateSet('check', 'diff', 'capture')]
    [string]$Mode = 'check',
    [string[]]$Scenario,
    [switch]$Examples,
    [string]$TetaWot,
    [string]$Preset
)

. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $Preset) { $Preset = Get-DefaultPreset }
if ($Scenario) { $Scenario = Split-List $Scenario }
$python = Get-Python
$runner = Join-Path $RepoRoot 'conformance\run.py'
$suffix = if (Test-Windows) { '.exe' } else { '' }

if ($Examples) {
    $folder = Join-Path $RepoRoot 'conformance\scenarios\examples'
    $names = @(Get-ChildItem $folder -Filter *.json | ForEach-Object { $_.BaseName })
    if ($Scenario) { $names = $Scenario }
    $cpp = (Join-Path (Get-BuildDir $Preset) 'examples') + "\{name}\example-{name}$suffix"
    $servers = @()
    if ($Mode -ne 'check') {
        if (-not $TetaWot) { $TetaWot = Join-Path (Split-Path -Parent $RepoRoot) 'teta-wot' }
        if (-not (Test-Path (Join-Path $TetaWot 'Cargo.toml'))) {
            throw "No teta-wot checkout at $TetaWot (clone https://github.com/lucaswewa/teta-wot at v0.1.0, or pass -TetaWot)"
        }
        $version = & git -C $TetaWot describe --tags 2>$null
        if ($version -ne 'v0.1.0') {
            Write-Warning "$TetaWot is at '$version', not v0.1.0: its examples may answer differently"
        }
        $missing = @($names | Where-Object { -not (Test-Path (Join-Path $TetaWot "target\release\$_$suffix")) })
        if ($missing.Count -gt 0) {
            Write-Host "Building teta-wot's examples: $($missing -join ', ')" -ForegroundColor DarkGray
            $arguments = @('build', '--release', '--locked', '--manifest-path', (Join-Path $TetaWot 'Cargo.toml'))
            foreach ($name in $missing) { $arguments += @('-p', $name) }
            Invoke-Native cargo $arguments
        }
        $servers += (Join-Path $TetaWot 'target\release') + "\{name}$suffix"
    }
    if ($Mode -ne 'capture') { $servers += $cpp }
    $arguments = @($runner, $Mode, '--examples')
} else {
    $folder = Join-Path $RepoRoot 'conformance\scenarios\http'
    if ($Scenario) {
        $names = $Scenario
    } else {
        # The scenarios whose Things the C++ twins have.
        $classes = @($TwinClasses.Values) + 'labthings_fastapi.example_things:ThingThatCantStart'
        $names = @()
        foreach ($file in Get-ChildItem $folder -Filter *.json) {
            $config = (Get-Content -Raw -Encoding UTF8 -Path $file.FullName | ConvertFrom-Json).config
            $needed = @($config.things.PSObject.Properties | ForEach-Object {
                    if ($_.Value -is [string]) { $_.Value } elseif ($_.Value.PSObject.Properties.Name -contains 'cls') { $_.Value.cls } else { $_.Value.class }
                })
            $unknown = @($needed | Where-Object { $classes -notcontains $_ })
            if ($unknown.Count -eq 0) {
                $names += $file.BaseName
            } else {
                Write-Host "Skipping $($file.BaseName): the C++ twins don't have $($unknown -join ', ') yet" -ForegroundColor DarkGray
            }
        }
    }
    $servers = @()
    if ($Mode -ne 'check') { $servers += Get-ReferenceServer }
    if ($Mode -ne 'capture') { $servers += Get-BuiltExecutable $Preset 'conformance\twin-server-cpp\twin-server' }
    $arguments = @($runner, $Mode)
}

foreach ($server in $servers) { $arguments += @('--server', $server) }
$arguments += $names
if ($Mode -eq 'capture') {
    Write-Warning 'capture rewrites the fixtures: review them with git diff before committing'
}
& $python @arguments
$code = $LASTEXITCODE
if ($code -eq 0) {
    Write-Host "conformance $Mode passed" -ForegroundColor Green
} else {
    Write-Host "conformance $Mode found differences" -ForegroundColor Red
}
exit $code
