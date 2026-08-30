[CmdletBinding()]
param(
  [string]$CubeMxPath = $env:STM32CUBEMX_EXE,
  [string]$FirmwarePackagePath = $env:STM32CUBE_FW_G4
)

$ErrorActionPreference = "Stop"

function Resolve-CubeMxPath([string]$requestedPath) {
  if (-not [string]::IsNullOrWhiteSpace($requestedPath)) {
    if (-not (Test-Path -LiteralPath $requestedPath -PathType Leaf)) {
      throw "STM32CubeMX executable not found: $requestedPath"
    }
    return (Resolve-Path -LiteralPath $requestedPath).Path
  }

  $command = Get-Command STM32CubeMX.exe -ErrorAction SilentlyContinue
  if ($null -ne $command) {
    return $command.Source
  }

  $candidates = @()
  if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
    $candidates += Join-Path $env:ProgramFiles "STMicroelectronics\STM32Cube\STM32CubeMX\STM32CubeMX.exe"
  }
  $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
  if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
    $candidates += Join-Path $programFilesX86 "STMicroelectronics\STM32Cube\STM32CubeMX\STM32CubeMX.exe"
  }

  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }

  throw "STM32CubeMX was not found. Pass -CubeMxPath or set STM32CUBEMX_EXE."
}

function ConvertTo-CubeMxQuotedPath([string]$path) {
  if ($path.Contains('"')) {
    throw "CubeMX paths containing a quote character are not supported: $path"
  }
  return '"' + $path + '"'
}

$cubeMx = Resolve-CubeMxPath $CubeMxPath
$keilConfig = Join-Path $PSScriptRoot "configure_keil.ps1"
$log = Join-Path $PSScriptRoot "cubemx-generation.log"
$projectRoot = $PSScriptRoot
$projectName = "GL30_AMOLED_V7"
$ioc = Join-Path $projectRoot "$projectName\$projectName.ioc"

foreach ($required in @($cubeMx, $keilConfig, $ioc)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required CubeMX input not found: $required"
  }
}

# CubeMX persists project setCustomFWPath into the IOC. Preserve the repository
# input byte-for-byte so a caller-specific package path never enters the diff.
$iocOriginalBytes = [IO.File]::ReadAllBytes($ioc)

if (-not [string]::IsNullOrWhiteSpace($FirmwarePackagePath)) {
  if (-not (Test-Path -LiteralPath $FirmwarePackagePath -PathType Container)) {
    throw "STM32CubeG4 firmware package not found: $FirmwarePackagePath"
  }
  $FirmwarePackagePath = (Resolve-Path -LiteralPath $FirmwarePackagePath).Path
}

function Get-CubeMxProcesses {
  @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
      $_.Name -ieq "STM32CubeMX.exe" -or
      ($_.Name -ieq "java.exe" -and $_.CommandLine -and $_.CommandLine.Contains($cubeMx))
    })
}

function Assert-NoCubeMxProcess([string]$phase) {
  $running = Get-CubeMxProcesses
  if ($running.Count -ne 0) {
    $ids = ($running | ForEach-Object ProcessId) -join ", "
    throw "CubeMX single-instance guard failed during $phase. Running PID(s): $ids"
  }
}

function Assert-CubeMxLog([string]$path, [string]$phase) {
  $content = Get-Content -LiteralPath $path -Raw
  $knownOptionalMessages = @(
    '\(OptionalMessage_ERROR\) Pin34 \(VP_RIF_VS_RIF1\) cannot be retrieved for this MCU',
    '\(OptionalMessage_ERROR\) IP \(ADC[123]\) : Parameter \(CommonPathInternal\) has invalid value \(null\|null\|null\|null\)',
    '\(OptionalMessage_ERROR\) IP \(RCC\) : Invalid parameter \(FamilyName\)',
    '\(OptionalMessage_ERROR\) IP \(RCC\) : Parameter \(RNGFreq_Value\) has invalid value \(160000000\)',
    '\(OptionalMessage_ERROR\) IP \(RCC\) : Parameter \(USBFreq_Value\) has invalid value \(160000000\)'
  )
  foreach ($knownMessage in $knownOptionalMessages) {
    $content = $content -replace "(?m)^.*$knownMessage\r?\n?", ''
  }

  $generateIndex = $content.LastIndexOf("project generate")
  if ($generateIndex -lt 0) {
    throw "STM32CubeMX did not reach the $phase step. See $log"
  }
  $generateSection = $content.Substring($generateIndex)
  if ($content -match '(?m)^KO\r?$' -or
      $content -match '\(OptionalMessage_ERROR\)' -or
      $content -match 'LL is not supported' -or
      $content -match 'NumberFormatException' -or
      $generateSection -notmatch '(?m)^OK\r?$') {
    throw "STM32CubeMX rejected the $phase step. See $log"
  }
}

$scriptPath = Join-Path ([IO.Path]::GetTempPath()) ("gl30-cubemx-{0}.script" -f [guid]::NewGuid())
$scriptLines = @(
  "config load $(ConvertTo-CubeMxQuotedPath $ioc)",
  "project name $projectName",
  "project toolchain MDK-ARM"
)
if (-not [string]::IsNullOrWhiteSpace($FirmwarePackagePath)) {
  $scriptLines += "project setCustomFWPath $(ConvertTo-CubeMxQuotedPath $FirmwarePackagePath)"
}
$scriptLines += @(
  'SetCopyLibrary "copy only"',
  'setDriver GPIO LL',
  'setDriver DMA LL',
  'setDriver RCC LL',
  'setDriver ADC LL',
  'setDriver I2C LL',
  'setDriver IWDG LL',
  'setDriver SPI LL',
  'setDriver TIM1 LL',
  'setDriver TIM2 LL',
  'setDriver TIM6 LL',
  'setDriver TIM7 LL',
  'setDriver USART LL',
  'project generate',
  'exit'
)

try {
  [IO.File]::WriteAllLines($scriptPath, $scriptLines, [Text.UTF8Encoding]::new($false))
  Assert-NoCubeMxProcess "startup"

  $bundledJava = Join-Path (Split-Path -Parent $cubeMx) "jre\bin\java.exe"
  if (Test-Path -LiteralPath $bundledJava -PathType Leaf) {
    $arguments = @(
      "--add-opens=java.desktop/java.awt=ALL-UNNAMED",
      "--add-exports=java.desktop/sun.awt=ALL-UNNAMED",
      "-Djavax.net.ssl.trustStoreType=WINDOWS-ROOT",
      "-jar",
      $cubeMx,
      "-q",
      $scriptPath
    )
    & $bundledJava @arguments 2>&1 | Out-File -LiteralPath $log -Encoding utf8
  } else {
    & $cubeMx -q $scriptPath 2>&1 | Out-File -LiteralPath $log -Encoding utf8
  }

  $exitCode = $LASTEXITCODE
  Assert-NoCubeMxProcess "after exit"
  if ($exitCode -ne 0) {
    throw "STM32CubeMX exited with code $exitCode. See $log"
  }
  Assert-CubeMxLog $log "MDK-ARM generation"

  $project = Join-Path $projectRoot "$projectName\MDK-ARM\$projectName.uvprojx"
  if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "STM32CubeMX did not create the expected MDK-ARM project: $project"
  }

  & $keilConfig -ProjectPath $project
  Write-Output "STM32CubeMX MDK-ARM generation completed: $project"
} finally {
  [IO.File]::WriteAllBytes($ioc, $iocOriginalBytes)
  if (Test-Path -LiteralPath $scriptPath) {
    Remove-Item -LiteralPath $scriptPath -Force
  }
}
