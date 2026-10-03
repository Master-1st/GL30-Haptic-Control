[CmdletBinding()]
param(
    [string]$DeviceSerial = 'AC:27:6E:D2:F6:5C',
    [string]$Python = 'G:\Agent\.tools\esp-idf-tools\python_env\idf5.5_py3.14_env\Scripts\python.exe',
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'usb_clock.py'
$pythonWindowless = Join-Path (Split-Path -Parent $Python) 'pythonw.exe'
$projectPath = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$startupLink = Join-Path ([Environment]::GetFolderPath('Startup')) 'GL30 USB Clock.lnk'

# Only stop our named helper; no other Python/serial programs are touched.
& $Python $scriptPath stop
if ($LASTEXITCODE -ne 0) { throw 'Could not stop GL30 clock helper' }
$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'pythonw.exe' -and $_.CommandLine -like "*$scriptPath* watch *"
}
foreach ($process in $existing) {
    $running = Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue
    if ($running -and -not $running.WaitForExit(10000)) {
        throw 'GL30 USB clock did not stop within 10 seconds'
    }
}
if ($Remove) {
    if (Test-Path -LiteralPath $startupLink) { Remove-Item -LiteralPath $startupLink }
    Write-Output 'GL30 USB clock automatic startup removed.'
    return
}
foreach ($path in @($Python, $pythonWindowless, $scriptPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $path" }
}
& $Python -c 'import serial'
if ($LASTEXITCODE -ne 0) { throw 'pyserial is required in the selected Python environment' }
$logPath = Join-Path $projectPath 'outputs\esp32-usb-clock\service.log'
$arguments = '"' + $scriptPath + '" watch --device-serial "' + $DeviceSerial + '" --log "' + $logPath + '"'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($startupLink)
$shortcut.TargetPath = $pythonWindowless
$shortcut.Arguments = $arguments
$shortcut.WorkingDirectory = $projectPath
$shortcut.WindowStyle = 7
$shortcut.Description = 'Automatically synchronize the GL30 screen clock over USB.'
$shortcut.Save()
Start-Process -FilePath $pythonWindowless -ArgumentList $arguments -WorkingDirectory $projectPath -WindowStyle Hidden
Write-Output "Started GL30 USB clock; automatic login startup: $startupLink"
Write-Output "Log: $logPath"
