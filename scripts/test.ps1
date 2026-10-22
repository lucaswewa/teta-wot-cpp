<#
.SYNOPSIS
Runs teta-wot-cpp's tests: all of them, a suite, a CTest regex, or a
GoogleTest filter.

.DESCRIPTION
Tests run from a built preset (scripts\build.ps1, or -Build). The suites are
CTest's labels:

  unit          the GoogleTest suites (teta-wot-tests)
  examples      every example's demonstration
  conformance   the reference's fixtures over sockets: the HTTP scenarios
                against the C++ twin server, and the examples that serve
                against their Rust originals' answers (needs Python 3)
  compile-fail  declarations that mustn't compile

-GTest runs teta-wot-tests directly with a --gtest_filter, which prints
GoogleTest's own output: handy while working on one test.

.PARAMETER Suite
all (the default), unit, examples, conformance or compile-fail.

.PARAMETER Filter
Only the tests whose CTest name matches this regex (ctest -R), such as
'Http\.' or 'example\.light'.

.PARAMETER GTest
Run teta-wot-tests with this --gtest_filter instead of CTest, such as
'Http.*:Cors.*' or 'Http/Fixtures.*default'.

.PARAMETER Jobs
How many tests run at once (by default, one per processor).

.PARAMETER Build
Build the preset first.

.PARAMETER ShowOutput
Show every test's output (ctest -V), not only that of the failures.

.EXAMPLE
scripts\test.ps1 -Suite conformance

.EXAMPLE
scripts\test.ps1 -GTest 'Sse.*'
#>
param(
    [string]$Preset,
    [ValidateSet('all', 'unit', 'examples', 'conformance', 'compile-fail')]
    [string]$Suite = 'all',
    [string]$Filter,
    [string]$GTest,
    [int]$Jobs = [System.Environment]::ProcessorCount,
    [switch]$Build,
    [switch]$ShowOutput
)

. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $Preset) { $Preset = Get-DefaultPreset }

if ($Build) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
# The compile-fail tests build targets: they need the compiler.
Enter-BuildEnvironment

if ($GTest) {
    $tests = Get-BuiltExecutable $Preset 'tests\teta-wot-tests'
    & $tests "--gtest_filter=$GTest"
    exit $LASTEXITCODE
}

$arguments = @('--preset', $Preset, '-j', $Jobs)
switch ($Suite) {
    'unit' { $arguments += @('-LE', 'example|compile_fail|conformance') }
    'examples' { $arguments += @('-L', 'example') }
    'conformance' { $arguments += @('-L', 'conformance') }
    'compile-fail' { $arguments += @('-L', 'compile_fail') }
}
if ($Filter) { $arguments += @('-R', $Filter) }
if ($ShowOutput) { $arguments += '-V' }

Push-Location $RepoRoot
try {
    Write-Host "> ctest $($arguments -join ' ')" -ForegroundColor DarkGray
    & ctest @arguments
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($code -eq 0) {
    Write-Host 'All tests passed' -ForegroundColor Green
} else {
    Write-Host 'Some tests failed' -ForegroundColor Red
}
exit $code
