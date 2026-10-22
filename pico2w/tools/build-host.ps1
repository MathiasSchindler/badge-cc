param(
    [string]$Compiler = "cc",
    [switch]$Test
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root "build\host"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot "picoctl_protocol.c"),
    (Join-Path $PSScriptRoot "meshtastic.c"),
    (Join-Path $PSScriptRoot "lorawan.c"),
    (Join-Path $root "src\usb\report.c")
)
$flags = @("-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
           "-I$(Join-Path $root 'src\include')")
$libraries = @("-lsetupapi", "-lcfgmgr32", "-lwinusb", "-lbcrypt", "-lole32")
& $Compiler @flags "-municode" (Join-Path $PSScriptRoot "picoctl.c") @sources @libraries "-o" (Join-Path $output "picoctl.exe")
if ($LASTEXITCODE -ne 0) { throw "picoctl compilation failed" }
if ($Test) {
    & $Compiler @flags (Join-Path $root "tests\picoctl_test.c") @sources "-o" (Join-Path $output "picoctl_test.exe")
    if ($LASTEXITCODE -ne 0) { throw "Protocol test compilation failed" }
    & (Join-Path $output "picoctl_test.exe")
    if ($LASTEXITCODE -ne 0) { throw "Protocol tests failed" }
    & $Compiler @flags "-municode" (Join-Path $root "tests\picoctl_windows_test.c") @sources @libraries "-o" (Join-Path $output "picoctl_windows_test.exe")
    if ($LASTEXITCODE -ne 0) { throw "Windows test compilation failed" }
    & (Join-Path $output "picoctl_windows_test.exe")
    if ($LASTEXITCODE -ne 0) { throw "Windows tests failed" }
}
Write-Output "Built $(Join-Path $output 'picoctl.exe'); no device was opened or flashed."
