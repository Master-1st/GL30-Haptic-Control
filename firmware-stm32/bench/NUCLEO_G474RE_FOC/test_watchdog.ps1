[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [string]$Programmer = 'G:\software\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.300.202508131133\tools\bin\STM32_Programmer_CLI.exe',
    [switch]$ConfirmUsbOnly
)
$ErrorActionPreference = 'Stop'
if (-not $ConfirmUsbOnly) { throw 'Disconnect EVM, motor and all jumpers; USB-only fault injection requires -ConfirmUsbOnly.' }
$taskEvidence = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\artifacts\bench\nucleo-g474re-foc'))
[void][IO.Directory]::CreateDirectory($taskEvidence)
$taskPort = New-Object IO.Ports.SerialPort $Port,115200,'None',8,'One'
$taskPort.NewLine = "`n"; $taskPort.ReadTimeout = 500; $taskPort.WriteTimeout = 1000
$taskLines = New-Object 'System.Collections.Generic.List[string]'
function Read-Expected([string]$Pattern) {
    $untilTask = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $untilTask) {
        try {
            $lineTask = $taskPort.ReadLine().Trim(); $taskLines.Add($lineTask)
            if ($lineTask -match $Pattern) { return $lineTask }
        } catch [TimeoutException] { }
    }
    throw "Missing watchdog evidence: $Pattern"
}
try {
    $taskPort.Open(); $taskPort.DiscardInBuffer(); $taskPort.WriteLine('STATUS')
    $beforeTask = Read-Expected '^STATUS '
    if ($beforeTask -notmatch 'mode=0 fault=0 ' -or $beforeTask -notmatch 'moe=0 off=1 ') {
        throw 'Fault injection requires verified output-off state.'
    }
    # Establish a fresh non-watchdog boot while UART is already listening.
    & $Programmer -c port=SWD sn=003E002F3235511337333439 mode=HOTPLUG -rst 2>&1 |
        Tee-Object -FilePath (Join-Path $taskEvidence 'watchdog-baseline-reset.log')
    if ($LASTEXITCODE -ne 0) { throw 'Baseline reset failed.' }
    [void](Read-Expected '^BOOT NUCLEO_G474RE_FOC ')
    [void](Read-Expected '^OK SELFTEST_ALGORITHM_ONLY_OUTPUT_OFF$')
    $taskPort.WriteLine('STATUS')
    $beforeTask = Read-Expected '^STATUS '
    if ($beforeTask -notmatch 'iwdg_reset=0' -or $beforeTask -notmatch 'moe=0 off=1 ') {
        throw 'A fresh non-watchdog output-off baseline was not observed.'
    }
    # Only TIM1_CR1.CEN is cleared; CMS=01 remains. In center mode DIR is read-only.
    # The injection itself has no reset command, flash/option-byte or GPIO writes.
    & $Programmer -c port=SWD sn=003E002F3235511337333439 mode=HOTPLUG -w32 0x40012C00 0x00000020 2>&1 |
        Tee-Object -FilePath (Join-Path $taskEvidence 'watchdog-inject.log')
    $injectExitTask = $LASTEXITCODE
    # Reset can race the debugger's write verification. Never accept its error as
    # success alone: require a new UART boot, self-test and hardware reset flag.
    $bootTask = Read-Expected '^BOOT NUCLEO_G474RE_FOC '
    $selfTestTask = Read-Expected '^OK SELFTEST_ALGORITHM_ONLY_OUTPUT_OFF$'
    $taskPort.WriteLine('STATUS')
    $afterTask = Read-Expected '^STATUS '
    if ($afterTask -notmatch 'iwdg_reset=1' -or $afterTask -notmatch 'mode=0 fault=0 ' -or
        $afterTask -notmatch 'moe=0 off=1 ' -or $afterTask -notmatch 'calibrated=0 ') {
        throw 'Expected independent-watchdog reset and no automatic re-arm.'
    }
    [ordered]@{result='PASS'; utc=[DateTime]::UtcNow.ToString('o'); before=$beforeTask; after=$afterTask;
        injected_register='TIM1_CR1.CEN=0'; outputs_enabled=$false;
        injection_cli_exit=$injectExitTask; fresh_boot=$bootTask; selftest=$selfTestTask;
        acceptance='Independent UART reboot and RCC IWDGRSTF; not CLI write-verification success'} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskEvidence 'watchdog-result.json') -Encoding UTF8
    $afterTask
    'PASS: ADC trigger loss caused an IWDG reset; reboot remained output-off and uncalibrated.'
} finally {
    if ($taskPort.IsOpen) { $taskPort.WriteLine('STOP'); $taskPort.Close() }
    $taskPort.Dispose()
    $taskLines | Set-Content -LiteralPath (Join-Path $taskEvidence 'watchdog-transcript.log') -Encoding UTF8
}
