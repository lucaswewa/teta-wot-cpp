<#
.SYNOPSIS
Builds teta-wot-cpp with a CMake preset, setting up the build environment
first (MSVC through vswhere and vcvars64.bat on Windows, and VCPKG_ROOT).

.DESCRIPTION
Configures the preset if its build folder has never been configured (or with
-Configure or -Fresh), then builds it. vcpkg installs the dependencies at the
first configure, which takes a while (Drogon alone takes about ten minutes).

.PARAMETER Preset
The CMake preset: windows-msvc (the default on Windows), windows-clang-cl, ...

.PARAMETER Target
Build only these targets, such as teta-wot-tests, twin-server or
example-light.

.PARAMETER Configure
Configure again (after changing a preset, vcpkg.json or a cache variable).

.PARAMETER Fresh
Configure from scratch (cmake --fresh). vcpkg's packages are kept.

.PARAMETER Clean
Remove the build's outputs before building (--clean-first).

.PARAMETER Test
Run every test afterwards (scripts\test.ps1).

.EXAMPLE
scripts\build.ps1

.EXAMPLE
scripts\build.ps1 -Target teta-wot-tests,twin-server -Test
#>
param(
    [string]$Preset,
    [string[]]$Target,
    [switch]$Configure,
    [switch]$Fresh,
    [switch]$Clean,
    [switch]$Test
)

. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $Preset) { $Preset = Get-DefaultPreset }
if ($Target) { $Target = Split-List $Target }

Enter-BuildEnvironment
$started = Get-Date
Push-Location $RepoRoot
try {
    $cache = Join-Path (Get-BuildDir $Preset) 'CMakeCache.txt'
    if ($Fresh) {
        Invoke-Native cmake @('--preset', $Preset, '--fresh')
    } elseif ($Configure -or -not (Test-Path $cache)) {
        Invoke-Native cmake @('--preset', $Preset)
    }
    $arguments = @('--build', '--preset', $Preset)
    if ($Target) { $arguments += @('--target') + $Target }
    if ($Clean) { $arguments += '--clean-first' }
    Invoke-Native cmake $arguments
} finally {
    Pop-Location
}
$elapsed = (Get-Date) - $started
Write-Host ("Built {0} in {1:mm\:ss}" -f $Preset, $elapsed) -ForegroundColor Green

if ($Test) {
    & (Join-Path $PSScriptRoot 'test.ps1') -Preset $Preset
    exit $LASTEXITCODE
}
