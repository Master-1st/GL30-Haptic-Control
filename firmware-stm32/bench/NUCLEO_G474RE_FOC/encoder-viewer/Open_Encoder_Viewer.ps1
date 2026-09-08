[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [switch]$Check,
    [switch]$BuildOnly
)

$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$taskOutput = Join-Path $taskRoot 'output\bench\encoder-viewer'
$taskExe = Join-Path $taskOutput 'GL30-Encoder-Viewer.exe'
$taskSource = Join-Path $PSScriptRoot 'EncoderViewer.cs'
$taskCompiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'

if (-not (Test-Path -LiteralPath $taskCompiler -PathType Leaf)) {
    throw 'Windows .NET Framework C# compiler is missing. No software was installed.'
}
[void][IO.Directory]::CreateDirectory($taskOutput)

# Do not replace a running viewer or open a second COM4 client.
$taskRunning = @(Get-Process -Name 'GL30-Encoder-Viewer' -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $taskExe })
if ($taskRunning.Count) {
    Write-Host "The encoder window is already running (PID $($taskRunning[0].Id)). Close it before rebuilding."
    return
}

$taskReferences = @('/r:System.dll', '/r:System.Core.dll', '/r:System.Drawing.dll',
    '/r:System.Windows.Forms.dll', '/r:System.Windows.Forms.DataVisualization.dll')
& $taskCompiler /nologo /codepage:65001 /warn:4 /warnaserror /optimize+ /target:winexe "/out:$taskExe" @taskReferences $taskSource
if ($LASTEXITCODE -ne 0) { throw 'Encoder viewer compilation failed.' }

if ($Check) {
    $taskTests = Join-Path $taskOutput 'EncoderViewer.Tests.exe'
    $taskTestArgs = @('/nologo', '/codepage:65001', '/warn:4', '/warnaserror', '/optimize+', '/target:exe',
        "/out:$taskTests", '/main:Gl30.EncoderViewer.Tests.TestRunner') + $taskReferences +
        @($taskSource, (Join-Path $PSScriptRoot 'EncoderViewer.Tests.cs'))
    & $taskCompiler @taskTestArgs
    if ($LASTEXITCODE -ne 0) { throw 'Encoder viewer test compilation failed.' }
    & $taskTests
    if ($LASTEXITCODE -ne 0) { throw 'Encoder viewer unit tests failed.' }
    return
}
if ($BuildOnly) { Write-Host $taskExe; return }

# A visible window is intentional: this is the user's requested live display.
Start-Process -FilePath $taskExe -ArgumentList @('--port', $Port, '--output', ('"' + $taskOutput + '"')) -WindowStyle Normal | Out-Null
Write-Host "Opened encoder viewer on $Port. It sends STATUS only; keep TI motor power off."
