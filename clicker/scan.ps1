param(
    [int]$Seconds = 40,
    [string]$ExpectedAddress,
    [string]$FlashImage,
    [string]$Serial,
    [string]$Picotool = "$PSScriptRoot\..\build\clicker\tools\picotool\picotool\picotool.exe"
)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 1 -or $Seconds -gt 120) { throw 'Seconds must be in 1..120' }
if ($FlashImage -and -not $Serial) { throw 'Flashing requires an explicit device serial' }

$interop = [System.Reflection.Assembly]::Load(
    'System.Runtime.WindowsRuntime, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089'
).Location
$runtime = [System.Reflection.Assembly]::Load(
    'System.Runtime, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a'
).Location
$eventInterop = [System.Reflection.Assembly]::Load(
    'System.Runtime.InteropServices.WindowsRuntime, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a'
).Location
$references = @(
    'System.dll', 'System.Core.dll', $interop, $runtime, $eventInterop,
    "$env:windir\System32\WinMetadata\Windows.Devices.winmd",
    "$env:windir\System32\WinMetadata\Windows.Foundation.winmd"
)
$compiler = New-Object System.CodeDom.Compiler.CompilerParameters
foreach ($reference in $references) { $null = $compiler.ReferencedAssemblies.Add($reference) }
Add-Type -CompilerParameters $compiler -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using Windows.Devices.Bluetooth.Advertisement;

public static class ClickerBleScan {
    public static readonly ConcurrentQueue<string> Seen = new ConcurrentQueue<string>();
    private static BluetoothLEAdvertisementWatcher watcher;
    private static ulong expectedAddress;
    public static void Start(string address) {
        expectedAddress = String.IsNullOrEmpty(address) ? 0 :
            Convert.ToUInt64(address.Replace(":", "").Replace("-", ""), 16);
        watcher = new BluetoothLEAdvertisementWatcher();
        watcher.ScanningMode = BluetoothLEScanningMode.Passive;
        watcher.Received += Received;
        watcher.Start();
    }
    private static void Received(BluetoothLEAdvertisementWatcher source,
                                 BluetoothLEAdvertisementReceivedEventArgs args) {
        if (args.Advertisement.LocalName == "Universe Clicker" &&
            (expectedAddress == 0 || args.BluetoothAddress == expectedAddress)) {
            Seen.Enqueue(String.Format("{0:o} {1:X12} RSSI={2} {3} {4}",
                args.Timestamp, args.BluetoothAddress, args.RawSignalStrengthInDBm,
                args.Advertisement.LocalName, args.AdvertisementType));
        }
    }
    public static string Status() { return watcher.Status.ToString(); }
    public static void Stop() { if (watcher != null) watcher.Stop(); }
}
'@

try {
    [ClickerBleScan]::Start($ExpectedAddress)
    $scanStartedAt = [DateTimeOffset]::Now
    $rebootedAt = $null
    Start-Sleep -Milliseconds 500
    if ([ClickerBleScan]::Status() -ne 'Started') {
        throw "Windows BLE watcher failed to start: $([ClickerBleScan]::Status())"
    }
    Write-Output 'Windows BLE scanner started; collecting only Universe Clicker advertisements.'
    if ($FlashImage) {
        & $Picotool load -v $FlashImage --ser $Serial *> "$PSScriptRoot\..\build\clicker\advertise-flash.log"
        if ($LASTEXITCODE -ne 0) { throw 'Advertising image flash/readback verification failed' }
        & $Picotool reboot --ser $Serial
        if ($LASTEXITCODE -ne 0) { throw 'Badge reboot failed' }
        $rebootedAt = [DateTimeOffset]::Now
    }
    $received = New-Object 'System.Collections.Generic.List[string]'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        $line = ''
        while ([ClickerBleScan]::Seen.TryDequeue([ref]$line)) {
            $received.Add($line)
        }
        Start-Sleep -Milliseconds 100
    }
    if ([ClickerBleScan]::Status() -ne 'Started') {
        throw "Windows BLE watcher stopped unexpectedly: $([ClickerBleScan]::Status())"
    }
    $log = "$PSScriptRoot\..\build\clicker\advertising-observation.txt"
    $received | Set-Content -LiteralPath $log
    @{
        scan_started_at = $scanStartedAt.ToString('o')
        rebooted_at = if ($rebootedAt) { $rebootedAt.ToString('o') } else { $null }
        scan_ended_at = [DateTimeOffset]::Now.ToString('o')
        expected_address = $ExpectedAddress
        serial = $Serial
        image_sha256 = if ($FlashImage) { (Get-FileHash $FlashImage -Algorithm SHA256).Hash } else { $null }
        count = $received.Count
        advertisements = @($received.ToArray())
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$PSScriptRoot\..\build\clicker\advertising-observation.json"
    Write-Output "Captured $($received.Count) matching advertisements."
    $received | Select-Object -First 3
    $received | Select-Object -Last 1
    if ($received.Count -eq 0) { throw 'No matching over-the-air BLE advertisement was observed' }
} finally {
    [ClickerBleScan]::Stop()
}
