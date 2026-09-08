[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [ValidateRange(10, 300)][int]$DurationSeconds = 60,
    [switch]$ConfirmUsbOnly
)
$ErrorActionPreference = 'Stop'
if (-not $ConfirmUsbOnly) { throw 'Only run with USB-connected NUCLEO, no EVM/motor/wires. Supply -ConfirmUsbOnly after checking.' }
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$taskEvidence = Join-Path $taskRoot 'artifacts\bench\nucleo-g474re-foc'
[void][IO.Directory]::CreateDirectory($taskEvidence)
$taskLog = New-Object 'System.Collections.Generic.List[string]'
$taskSerial = New-Object IO.Ports.SerialPort $Port,115200,'None',8,'One'
$taskSerial.NewLine = "`n"
$taskSerial.ReadTimeout = 500
$taskSerial.WriteTimeout = 2000
$taskCheckCount = 0

function Assert-Task([bool]$Condition, [string]$Message) {
    $script:taskCheckCount++
    if (-not $Condition) { throw $Message }
}
function Wait-TaskLine([string]$Pattern, [int]$Seconds = 3) {
    $deadlineTask = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadlineTask) {
        try {
            $lineTask = $taskSerial.ReadLine().Trim()
            $taskLog.Add('RX ' + $lineTask)
            if ($lineTask -match $Pattern) { return $lineTask }
        } catch [TimeoutException] { }
    }
    throw "No response matching: $Pattern"
}
function Send-Task([string]$Command, [string]$Pattern) {
    $taskLog.Add('TX ' + $Command)
    $taskSerial.WriteLine($Command)
    return Wait-TaskLine $Pattern
}
function Read-TaskStatus {
    $lineTask = Send-Task 'STATUS' '^STATUS '
    $fieldsTask = @{}
    foreach ($matchTask in [regex]::Matches($lineTask, '([a-z_]+)=(-?\d+)')) {
        $fieldsTask[$matchTask.Groups[1].Value] = [long]$matchTask.Groups[2].Value
    }
    Assert-Task ($fieldsTask['moe'] -eq 0 -and $fieldsTask['off'] -eq 1) 'Power outputs were not disabled'
    return $fieldsTask
}

try {
    $taskSerial.Open()
    $taskSerial.DiscardInBuffer()
    $initialTask = Read-TaskStatus
    Assert-Task ($initialTask['mode'] -eq 0 -and $initialTask['fault'] -eq 0) 'Start must be fault-free OFF; reset board first'
    Assert-Task (($initialTask['health'] -band 31) -eq 0) 'Expected all external hardware health bits absent'
    [void](Send-Task 'BREAKTEST' '^OK BREAKTEST_INTERNAL_PULL_NO_OUTPUT$')
    [void](Send-Task 'ALIGN' '^ERR ALIGN_NOT_READY$')
    [void](Send-Task 'IQ 100 50' '^ERR IQ_NEEDS_ALIGNMENT$')
    [void](Send-Task 'IQ 101 50' '^ERR IQ_RANGE')
    [void](Send-Task 'IQ -101 50' '^ERR IQ_RANGE')
    [void](Send-Task 'IQ 100 2001' '^ERR IQ_RANGE')
    [void](Send-Task 'IQ 999999999999999999999999 10' '^ERR IQ_RANGE')
    [void](Send-Task 'IQ NaN 100' '^ERR IQ_RANGE')
    [void](Send-Task 'IQ 1 50 trailing' '^ERR IQ_RANGE')
    [void](Send-Task 'PREPARE' '^ERR PREPARE_')
    [void](Send-Task 'NOT_A_COMMAND' '^ERR UNKNOWN_COMMAND$')
    [void](Send-Task ('Z' * 120) '^ERR LINE_TOO_LONG_OR_BINARY$')
    [void](Send-Task 'FAULTTEST' '^OK FAULT_LATCHED_NO_OUTPUT$')
    $faultTask = Read-TaskStatus
    Assert-Task ($faultTask['mode'] -eq 5 -and $faultTask['fault'] -eq 1024) 'Fault injection must latch'
    [void](Send-Task 'STOP' '^OK STOP$')
    $stoppedTask = Read-TaskStatus
    Assert-Task ($stoppedTask['mode'] -eq 5 -and $stoppedTask['fault'] -eq 1024) 'STOP must not clear a fault'
    [void](Send-Task 'ALIGN' '^ERR ALIGN_NOT_READY$')
    [void](Send-Task 'CLEAR' '^OK CLEAR_REQUIRES_PREPARE_AND_ALIGNMENT$')
    [void](Send-Task 'SELFTEST' '^OK SELFTEST_NO_OUTPUT$')
    [void](Wait-TaskLine '^OK SELFTEST_ALGORITHM_ONLY_OUTPUT_OFF$')
    $beforeTask = Read-TaskStatus
    Assert-Task ($beforeTask['self_fail'] -eq 0 -and $beforeTask['self_left'] -eq 0) 'Synthetic algorithm self-test failed'
    Assert-Task ($beforeTask['self_max'] -gt 0 -and $beforeTask['self_max'] -lt 3600) 'Algorithm deadline failed'
    $taskWatch = [Diagnostics.Stopwatch]::StartNew()
    $taskNonce = 0
    while ($taskWatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $tokenTask = 'FOC_' + $taskNonce.ToString('D6')
        [void](Send-Task ('PING ' + $tokenTask) ('^PONG ' + $tokenTask + '$'))
        $taskNonce++
        if (($taskNonce % 100) -eq 0) {
            $midTask = Read-TaskStatus
            Assert-Task ($midTask['mode'] -eq 0 -and $midTask['fault'] -eq 0) 'Unexpected fault or transition during load'
        }
    }
    $afterTask = Read-TaskStatus
    $taskWatch.Stop()
    $adcHzTask = ($afterTask['adc'] - $beforeTask['adc']) / $taskWatch.Elapsed.TotalSeconds
    $encHzTask = ($afterTask['enc_irq'] - $beforeTask['enc_irq']) / $taskWatch.Elapsed.TotalSeconds
    Assert-Task ($adcHzTask -gt 19500 -and $adcHzTask -lt 20500) '20k ADC schedule out of host timing tolerance'
    Assert-Task ($encHzTask -gt 3900 -and $encHzTask -lt 4100) '4k encoder schedule out of host timing tolerance'
    Assert-Task ($afterTask['adc_bad'] -eq 0 -and $afterTask['deadline'] -eq 0) 'ADC synchronization or ISR deadline failure'
    Assert-Task ($afterTask['uart_err'] -eq 0) 'UART errors observed'
    Assert-Task ($afterTask['rate'] -eq 1 -and $afterTask['isr_max'] -lt 3600) 'On-board timing gate failed'
    [void](Send-Task 'STOP' '^OK STOP$')
    $handoffTask = Read-TaskStatus
    $resultTask = [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o'); result = 'PASS'; checks = $taskCheckCount
        board = 'NUCLEO-G474RE'; probe_serial = '003E002F3235511337333439'
        port = $Port; duration_seconds = $taskWatch.Elapsed.TotalSeconds; nonce_roundtrips = $taskNonce
        adc_hz_host_estimate = $adcHzTask; encoder_irq_hz_host_estimate = $encHzTask
        status_before = $beforeTask; status_after = $afterTask; handoff_status = $handoffTask
        physical_motor_tested = $false; external_driver_tested = $false
    }
    $resultTask | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskEvidence 'bare-board-result.json') -Encoding UTF8
    $resultTask | ConvertTo-Json -Depth 5
} finally {
    if ($taskSerial.IsOpen) { $taskSerial.WriteLine('STOP'); $taskSerial.Close() }
    $taskSerial.Dispose()
    $taskLog | Set-Content -LiteralPath (Join-Path $taskEvidence 'bare-board-transcript.log') -Encoding UTF8
}
