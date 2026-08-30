[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectPath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ProjectPath)) {
  throw "Keil project not found: $ProjectPath"
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$content = [System.IO.File]::ReadAllText($ProjectPath, $utf8NoBom)
$newline = if ($content.Contains("`r`n")) { "`r`n" } else { "`n" }

function Replace-RequiredBlock {
  param(
    [string]$Text,
    [string]$OldBlock,
    [string]$NewBlock,
    [string]$Label
  )

  if ($Text.Contains($NewBlock)) {
    return $Text
  }

  $count = ([regex]::Matches($Text, [regex]::Escape($OldBlock))).Count
  if ($count -ne 1) {
    throw "Expected one CubeMX $Label block, found $count in $ProjectPath"
  }

  return $Text.Replace($OldBlock, $NewBlock)
}

$toolset = "      <ToolsetName>ARM-ADS</ToolsetName>"
$compilerBlock =
  $toolset + $newline +
  "      <pCCUsed>6210000::V6.21::ARMCLANG</pCCUsed>" + $newline +
  "      <uAC6>1</uAC6>"

if (-not $content.Contains("<pCCUsed>")) {
  $content = Replace-RequiredBlock $content $toolset $compilerBlock "toolset"
} else {
  $content = [regex]::Replace(
    $content,
    '<pCCUsed>[^<]*</pCCUsed>',
    '<pCCUsed>6210000::V6.21::ARMCLANG</pCCUsed>',
    1)
  $content = [regex]::Replace(
    $content,
    '<uAC6>[^<]*</uAC6>',
    '<uAC6>1</uAC6>',
    1)
}

$generatedDefine =
  "USE_FULL_LL_DRIVER,HSE_VALUE=24000000,HSE_STARTUP_TIMEOUT=100," +
  "LSE_STARTUP_TIMEOUT=5000,LSE_VALUE=32768,EXTERNAL_CLOCK_VALUE=12288000," +
  "HSI_VALUE=16000000,LSI_VALUE=32000,VDD_VALUE=3300,PREFETCH_ENABLE=0," +
  "INSTRUCTION_CACHE_ENABLE=1,DATA_CACHE_ENABLE=1,STM32G474xx"
$configuredDefine = $generatedDefine.Replace(
  "USE_FULL_LL_DRIVER,",
  "USE_FULL_LL_DRIVER,GL30_BUILD_ONLY=0,")
$generatedIncludes =
  "../Core/Inc;../Drivers/STM32G4xx_HAL_Driver/Inc;" +
  "../Drivers/CMSIS/Device/ST/STM32G4xx/Include;../Drivers/CMSIS/Include"
$configuredIncludes =
  "../Core/Inc;../../../app;../../../config;../../../drivers;" +
  "../../../control;../../../haptics;../../../trace;../../../protocol;" +
  "../../../safety;../Drivers/STM32G4xx_HAL_Driver/Inc;" +
  "../Drivers/CMSIS/Device/ST/STM32G4xx/Include;../Drivers/CMSIS/Include"
$warningFlags =
  "-Wno-invalid-utf8 -Wno-unsafe-buffer-usage -Wno-padded " +
  "-Wno-extra-semi-stmt -Wno-declaration-after-statement " +
  "-Wno-missing-noreturn"
$generatedControls =
  "              <MiscControls />" + $newline +
  "              <Define>$generatedDefine</Define>" + $newline +
  "              <Undefine />" + $newline +
  "              <IncludePath>$generatedIncludes</IncludePath>"
$configuredControls =
  "              <MiscControls>$warningFlags</MiscControls>" + $newline +
  "              <Define>$configuredDefine</Define>" + $newline +
  "              <Undefine />" + $newline +
  "              <IncludePath>$configuredIncludes</IncludePath>"
if (-not (
    $content.Contains("<MiscControls>$warningFlags</MiscControls>") -and
    $content.Contains("<Define>$configuredDefine</Define>") -and
    $content.Contains("<IncludePath>$configuredIncludes</IncludePath>"))) {
  $controlsPattern =
    [regex]::Escape("              <MiscControls />") + '\r?\n' +
    [regex]::Escape("              <Define>$generatedDefine</Define>") + '\r?\n' +
    [regex]::Escape("              <Undefine />") + '\r?\n' +
    [regex]::Escape("              <IncludePath>$generatedIncludes</IncludePath>")
  $controlMatches = [regex]::Matches($content, $controlsPattern)
  if ($controlMatches.Count -ne 1) {
    throw "Expected one CubeMX C compiler controls block, found $($controlMatches.Count) in $ProjectPath"
  }
  $content = [regex]::Replace(
    $content,
    $controlsPattern,
    $configuredControls,
    1)
}

$group = @(
  "        <Group>",
  "          <GroupName>Application/GL30</GroupName>",
  "          <Files>",
  "            <File>",
  "              <FileName>gl30_app.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../app/gl30_app.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>foc.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../control/foc.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>haptics.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../haptics/haptics.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>v6_protocol.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../protocol/v6_protocol.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>safety_supervisor.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../safety/safety_supervisor.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>timebase.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../trace/timebase.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>trace_buffer.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../trace/trace_buffer.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>drv8316.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../drivers/drv8316.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>factory_encoder_pending.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../drivers/factory_encoder_pending.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>ll_i2c_bus.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../drivers/ll_i2c_bus.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>ina228.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../drivers/ina228.c</FilePath>",
  "            </File>",
  "            <File>",
  "              <FileName>veml7700.c</FileName>",
  "              <FileType>1</FileType>",
  "              <FilePath>../../../drivers/veml7700.c</FilePath>",
  "            </File>",
  "          </Files>",
  "        </Group>"
) -join $newline

$groupPattern =
  '(?s)\r?\n        <Group>\r?\n          <GroupName>Application/GL30</GroupName>' +
  '.*?\r?\n        </Group>'
$matches = [regex]::Matches($content, $groupPattern)
if ($matches.Count -gt 1) {
  throw "More than one Application/GL30 group in $ProjectPath"
}
if ($matches.Count -eq 1) {
  $content = [regex]::Replace($content, $groupPattern, '', 1)
}

$driversAnchor =
  "        <Group>" + $newline +
  "          <GroupName>Drivers/STM32G4xx_HAL_Driver</GroupName>"
$driversPattern =
  [regex]::Escape("        <Group>") + '\r?\n' +
  [regex]::Escape("          <GroupName>Drivers/STM32G4xx_HAL_Driver</GroupName>")
if (([regex]::Matches($content, $driversPattern)).Count -ne 1) {
  throw "Expected one CubeMX driver-group anchor in $ProjectPath"
}
$content = [regex]::Replace(
  $content,
  $driversPattern,
  $group + $newline + $driversAnchor,
  1)

$validation = [System.Xml.XmlDocument]::new()
$validation.LoadXml($content)
[System.IO.File]::WriteAllText($ProjectPath, $content, $utf8NoBom)

Write-Output "Keil ARMCLANG 6.21 integration completed: $ProjectPath"
