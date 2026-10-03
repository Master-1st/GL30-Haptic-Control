[CmdletBinding()]
param(
    [string]$SdkPath = 'G:\Agent\.tools\esp-idf-v5.5.1',
    [string]$ToolsPath = 'G:\Agent\.tools\esp-idf-tools',
    [string]$BuildDirectory = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Definition
$projectDirectory = (Resolve-Path (Join-Path $scriptDirectory '..')).Path

if (-not (Test-Path -LiteralPath $SdkPath -PathType Container)) {
    throw "ESP-IDF SDK directory does not exist: $SdkPath"
}
if (-not (Test-Path -LiteralPath $ToolsPath -PathType Container)) {
    throw "ESP-IDF tools directory does not exist: $ToolsPath"
}

$SdkPath = (Resolve-Path $SdkPath).Path
$ToolsPath = (Resolve-Path $ToolsPath).Path
$pythonEnvironment = Join-Path $ToolsPath 'python_env\idf5.5_py3.14_env'
$pythonScripts = Join-Path $pythonEnvironment 'Scripts'
$pythonExecutable = Join-Path $pythonScripts 'python.exe'
$idfPy = Join-Path $SdkPath 'tools\idf.py'
$exportScript = Join-Path $SdkPath 'export.ps1'

foreach ($requiredPath in @($pythonExecutable, $idfPy, $exportScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required ESP-IDF file does not exist: $requiredPath"
    }
}

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectDirectory 'build-idf'
} elseif (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectDirectory $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

# Put the managed IDF Python environment first so export.ps1 and idf.py use
# the installed ESP-IDF interpreter rather than a system Python.
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PATH = $SdkPath
$env:Path = "$pythonScripts;$env:Path"

Push-Location $SdkPath
try {
    . $exportScript
} finally {
    Pop-Location
}

$activePython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $activePython -PathType Leaf)) {
    throw "ESP-IDF export did not expose its Python environment: $activePython"
}

Push-Location $projectDirectory
try {
if ($Clean -and (Test-Path -LiteralPath $BuildDirectory -PathType Container)) {
    & $activePython $idfPy '-B' $BuildDirectory 'fullclean'
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-IDF fullclean failed with exit code $LASTEXITCODE"
    }
}

$sdkConfigPath = Join-Path $projectDirectory 'sdkconfig'
$targetConfigured = $false
$consoleConfigured = $false
if (Test-Path -LiteralPath $sdkConfigPath -PathType Leaf) {
    $targetConfigured = Select-String -LiteralPath $sdkConfigPath -Pattern '^CONFIG_IDF_TARGET="esp32s3"$' -Quiet
    $consoleConfigured = Select-String -LiteralPath $sdkConfigPath -Pattern '^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y$' -Quiet
}
if (-not $targetConfigured -or -not $consoleConfigured) {
    # set-target regenerates the local sdkconfig from sdkconfig.defaults.
    & $activePython $idfPy '-B' $BuildDirectory 'set-target' 'esp32s3'
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-IDF set-target failed with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$logPath = Join-Path $BuildDirectory 'build-firmware.log'

function Invoke-LoggedIdf {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $activePython $idfPy @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-IDF command failed with exit code $LASTEXITCODE. See $logPath"
    }
}

# This entrypoint intentionally performs a build only. It never invokes flash.
Invoke-LoggedIdf @('-B', $BuildDirectory, 'build')

$expectedArtifacts = @(
    (Join-Path $BuildDirectory 'gl30_haptic_control.bin'),
    (Join-Path $BuildDirectory 'bootloader\bootloader.bin'),
    (Join-Path $BuildDirectory 'partition_table\partition-table.bin')
)

Write-Output 'ESP-IDF build succeeded.'
Write-Output "Build directory: $BuildDirectory"
Write-Output "Build log: $logPath"
foreach ($artifact in $expectedArtifacts) {
    if (Test-Path -LiteralPath $artifact -PathType Leaf) {
        Write-Output "Artifact: $artifact"
    } else {
        Write-Warning "Expected artifact was not found: $artifact"
    }
}
} finally {
    Pop-Location
}
