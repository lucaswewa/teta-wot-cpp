# Shared by the scripts in this folder (dot-source it): the repository's
# paths, the build environment, and running native commands.
#
# Windows PowerShell 5.1 compatible, and ASCII only (5.1 reads files without
# a byte order mark as ANSI).

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$RepoRoot = Split-Path -Parent $PSScriptRoot

# The twins' names in the scenarios, and the class names configurations use
# for them (conformance/twins).
$TwinClasses = [ordered]@{
    mything        = 'labthings_fastapi.example_things:MyThing'
    broken         = 'labthings_fastapi.example_things:ThingWithBrokenAffordances'
    counter        = 'twins.counter:TestThing'
    types          = 'twins.types_probe:TypesThing'
    http_probe     = 'twins.http_probe:HttpProbe'
    settings_probe = 'twins.settings_probe:SettingsProbe'
    ws_probe       = 'twins.ws_probe:ThingWithProperties'
}

function Test-Windows {
    return [System.Environment]::OSVersion.Platform -eq 'Win32NT'
}

function Get-DefaultPreset {
    if (Test-Windows) { return 'windows-msvc' }
    return 'linux-gcc'
}

function Get-BuildDir([string]$Preset) {
    return Join-Path $RepoRoot "build\$Preset"
}

# An executable built by `$Preset`, by its path in the build tree without
# `.exe` (such as `tests/teta-wot-tests`). Throws if it hasn't been built.
function Get-BuiltExecutable([string]$Preset, [string]$RelativePath) {
    $path = Join-Path (Get-BuildDir $Preset) $RelativePath
    if (Test-Windows) { $path += '.exe' }
    if (-not (Test-Path $path)) {
        throw "$path doesn't exist: build it first (scripts\build.ps1 -Preset $Preset)"
    }
    return $path
}

# Runs a native command and throws if it fails.
function Invoke-Native([string]$Command, [string[]]$Arguments) {
    Write-Host "> $Command $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

# A list parameter's items, whether PowerShell parsed `a,b` as an array or,
# run with `powershell -File`, passed it as one string.
function Split-List([string[]]$Items) {
    return @($Items | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

function Get-Python {
    foreach ($name in 'python3', 'python', 'py') {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    throw 'Python 3 is needed (python3, python or py on PATH)'
}

function Get-VisualStudioPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe isn't at ${vswhere}: is Visual Studio (with the C++ workload) installed?"
    }
    $path = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $path) {
        throw 'No Visual Studio with the C++ x64 tools was found'
    }
    return $path
}

# The vcpkg the build uses: TETA_WOT_VCPKG_ROOT, else VCPKG_ROOT, else the
# vcpkg on PATH, ignoring Visual Studio's own copy (a developer prompt points
# VCPKG_ROOT at it, and another vcpkg would rebuild every package) unless
# there is nothing else.
function Resolve-VcpkgRoot([string]$VisualStudioPath, [string]$VcpkgRootBefore) {
    $bundled = if ($VisualStudioPath) { Join-Path $VisualStudioPath 'VC\vcpkg' } else { $null }
    $candidates = @($env:TETA_WOT_VCPKG_ROOT, $VcpkgRootBefore)
    $onPath = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += Split-Path -Parent $onPath.Source }
    foreach ($candidate in $candidates) {
        if (-not $candidate) { continue }
        if ($bundled -and ($candidate.TrimEnd('\') -ieq $bundled)) { continue }
        if (Test-Path (Join-Path $candidate 'scripts\buildsystems\vcpkg.cmake')) { return $candidate }
    }
    if ($bundled -and (Test-Path $bundled)) {
        Write-Warning "Using Visual Studio's own vcpkg ($bundled); set TETA_WOT_VCPKG_ROOT to use another"
        return $bundled
    }
    throw 'No vcpkg found: set VCPKG_ROOT (or TETA_WOT_VCPKG_ROOT) to a vcpkg clone'
}

# Makes this PowerShell session a developer environment for the build: on
# Windows, the MSVC x64 tools (vcvars64.bat, found with vswhere) unless they
# are already set up; everywhere, VCPKG_ROOT.
function Enter-BuildEnvironment {
    if ($env:TETA_WOT_BUILD_ENVIRONMENT -eq '1') { return }
    $vcpkgBefore = $env:VCPKG_ROOT
    $visualStudio = $null
    if (Test-Windows) {
        $visualStudio = Get-VisualStudioPath
        if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
            $vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
            Write-Host "Setting up the MSVC environment ($vcvars)" -ForegroundColor DarkGray
            # A temporary batch file: no quoting of paths through cmd /c.
            $batch = Join-Path ([System.IO.Path]::GetTempPath()) "teta-wot-vcvars-$PID.cmd"
            Set-Content -Path $batch -Encoding Ascii -Value "@call `"$vcvars`" >nul 2>&1`r`n@set"
            try {
                $variables = & cmd.exe /c $batch
            } finally {
                Remove-Item $batch -ErrorAction SilentlyContinue
            }
            foreach ($line in $variables) {
                $at = $line.IndexOf('=')
                if ($at -gt 0) {
                    Set-Item -Path "env:$($line.Substring(0, $at))" -Value $line.Substring($at + 1)
                }
            }
            if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
                throw "vcvars64.bat didn't put cl.exe on PATH"
            }
        }
    }
    $env:VCPKG_ROOT = Resolve-VcpkgRoot $visualStudio $vcpkgBefore
    $env:TETA_WOT_BUILD_ENVIRONMENT = '1'
    Write-Host "VCPKG_ROOT=$env:VCPKG_ROOT" -ForegroundColor DarkGray
}

# The Rust reference server (teta-wot v0.1.0, conformance/reference-rs),
# built with cargo if it hasn't been.
function Get-ReferenceServer {
    $crate = Join-Path $RepoRoot 'conformance\reference-rs'
    $path = Join-Path $crate 'target\release\reference-server'
    if (Test-Windows) { $path += '.exe' }
    if (-not (Test-Path $path)) {
        if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
            throw "$path doesn't exist, and cargo isn't on PATH to build it (https://rustup.rs)"
        }
        Write-Host 'Building the reference server (teta-wot v0.1.0)' -ForegroundColor DarkGray
        Invoke-Native cargo @('build', '--release', '--locked', '--manifest-path', (Join-Path $crate 'Cargo.toml'))
    }
    return $path
}

# Writes text as UTF-8 without a byte order mark (which servers' JSON
# parsers refuse), as Set-Content can't in Windows PowerShell 5.1.
function Write-Utf8File([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}
