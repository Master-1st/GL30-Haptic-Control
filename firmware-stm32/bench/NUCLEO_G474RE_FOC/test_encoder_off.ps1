[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [ValidateRange(5, 600)][int]$DurationSeconds = 120,
    [ValidateRange(0, 10)][int]$SelfTestRuns = 3,
    [string]$EvidenceDirectory = '',
    [switch]$ConfirmDriverPowerOff
)

$ErrorActionPreference = 'Stop'
if (-not $ConfirmDriverPowerOff) { throw 'Use -ConfirmDriverPowerOff before running with power-off hardware path.' }

$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$evidenceDir = if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    Join-Path $taskRoot 'output\bench\encoder-off'
} else { [IO.Path]::GetFullPath($EvidenceDirectory) }
[void][IO.Directory]::CreateDirectory($evidenceDir)
$transcriptPath = Join-Path $evidenceDir 'encoder-off-transcript.log'
$resultPath = Join-Path $evidenceDir 'encoder-off-result.json'

$serial = New-Object IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$serial.NewLine = "`n"
$serial.ReadTimeout = 500
$serial.WriteTimeout = 1000
$logWriter = New-Object System.IO.StreamWriter($transcriptPath, $false, (New-Object Text.UTF8Encoding($false)))

$checkCount = 0
$statusReads = 0
$requiredFields = @('mode','fault','moe','off','button','calibrated','drv','vm_mv','health','self_left','rate','deadline','self_fail','isr_max','self_max','age_us','adc_bad','enc_ok','enc_err','uart_err','adc','enc_irq')
$result = @{ utc = [DateTime]::UtcNow.ToString('o'); result = 'PASS'; mode = 'encoder_off'; port = $Port }
$ignorePatterns = @('^BOOT ', '^OK SELFTEST_ALGORITHM_ONLY_OUTPUT_OFF$')

function Assert-Ok([bool]$Condition, [string]$Message) {
    $script:checkCount++
    if (-not $Condition) { throw $Message }
}

function Log-Uart([string]$Direction, [string]$Text) {
    $logWriter.WriteLine(('[{0}] {1} {2}' -f [DateTime]::UtcNow.ToString('o'), $Direction, $Text))
    $logWriter.Flush()
}

function Send-Usart([string]$Command) {
    $serial.WriteLine($Command)
    Log-Uart 'TX' $Command
}

function Read-UartLine {
    $line = $serial.ReadLine().Trim()
    Log-Uart 'RX' $line
    return $line
}

function Read-UartForPattern {
    [CmdletBinding()]
    param(
        [string]$Pattern,
        [int]$TimeoutMs = 5000,
        [string[]]$Ignore = @()
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    :readLoop while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = Read-UartLine
        } catch [System.TimeoutException] {
            Start-Sleep -Milliseconds 20
            continue readLoop
        }
        if ($line -match '^ERR ') { throw "UART returned error: $line" }
        foreach ($ig in $Ignore) {
            if ($line -match $ig) { continue readLoop }
        }
        if ($line -match $Pattern) { return $line }
    }
    throw "UART timeout waiting pattern [$Pattern] in ${TimeoutMs}ms"
}

function Parse-Status([string]$Line) {
    $status = @{}
    foreach ($m in [regex]::Matches($Line, '([a-z_]+)=(-?\d+)')) {
        $status[$m.Groups[1].Value] = [long]$m.Groups[2].Value
    }
    return $status
}

function Ensure-StatusFields([hashtable]$Status, [string]$Context) {
    foreach ($f in $requiredFields) {
        Assert-Ok ($Status.ContainsKey($f)) "$Context missing field $f"
    }
}

function Check-SafetyState([hashtable]$Status, [string]$Context) {
    Assert-Ok ($Status['mode'] -eq 0) "$Context mode must be 0"
    Assert-Ok ($Status['fault'] -eq 0) "$Context fault must be 0"
    Assert-Ok ($Status['moe'] -eq 0) "$Context moe must be 0"
    Assert-Ok ($Status['off'] -eq 1) "$Context off must be 1"
    Assert-Ok ($Status['button'] -eq 0) "$Context button must be 0"
    Assert-Ok ($Status['calibrated'] -eq 0) "$Context calibrated must be 0"
    Assert-Ok ($Status['drv'] -eq 4294967295) "$Context drv must be 4294967295, got $($Status['drv'])"
    Assert-Ok ($Status['vm_mv'] -lt 9000) "$Context vm_mv must be <9000, got $($Status['vm_mv'])"
}

function Check-LoopState([hashtable]$Status, [string]$Context) {
    Assert-Ok (($Status['health'] -band 2) -eq 2) "$Context encoder health bit (2) must be set"
    if ($Status['self_left'] -eq 0) { Assert-Ok (($Status['health'] -band 34) -eq 34) "$Context health must include 34" }
    Assert-Ok ($Status['deadline'] -eq 0) "$Context deadline must be 0"
    Assert-Ok ($Status['adc_bad'] -eq 0) "$Context adc_bad must be 0"
    Assert-Ok ($Status['age_us'] -le 750) "$Context age_us must be <= 750"
    Assert-Ok ($Status['self_fail'] -eq 0) "$Context self_fail must be 0"
    Assert-Ok ($Status['isr_max'] -lt 3600) "$Context isr_max must be < 3600"
    Assert-Ok ($Status['self_max'] -lt 3600) "$Context self_max must be < 3600"
}

function Check-ErrorBaseline([hashtable]$Status, [long]$BaselineEncErr, [long]$BaselineUartErr, [string]$Context) {
    Assert-Ok ($Status['enc_err'] -eq $BaselineEncErr) "$Context enc_err changed from baseline"
    Assert-Ok ($Status['uart_err'] -eq $BaselineUartErr) "$Context uart_err changed from baseline"
}

function Get-Status {
    Send-Usart 'STATUS'
    $script:statusReads++
    $line = Read-UartForPattern -Pattern '^STATUS ' -TimeoutMs 5000 -Ignore $ignorePatterns
    $status = Parse-Status $line
    Assert-Ok ($line -like 'STATUS *') "Unexpected STATUS response: $line"
    return $status
}

function Expect-Pattern([string]$Command, [string]$Pattern) {
    Send-Usart $Command
    return Read-UartForPattern -Pattern $Pattern -TimeoutMs 5000 -Ignore $ignorePatterns
}

function Wait-StatusWindow {
    [CmdletBinding()]
    param(
        [scriptblock]$Condition,
        [string]$Err,
        [int]$TimeoutSec,
        [long]$BaselineEncErr,
        [long]$BaselineUartErr,
        [string]$Context,
        [switch]$RequireLoopChecks
    )
    $until = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    while ([DateTime]::UtcNow -lt $until) {
        $s = Get-Status
        Ensure-StatusFields $s $Context
        Check-SafetyState $s $Context
        Check-ErrorBaseline $s $BaselineEncErr $BaselineUartErr $Context
        if ($RequireLoopChecks) { Check-LoopState $s $Context }
        if (& $Condition $s) { return $s }
        Start-Sleep -Milliseconds 100
    }
    throw $Err
}

function OffHealthy([hashtable]$Status) {
    return $Status['off'] -eq 1 -and $Status['mode'] -eq 0 -and $Status['fault'] -eq 0 -and $Status['self_left'] -eq 0 -and (($Status['health'] -band 34) -eq 34)
}

function Build-Metrics([hashtable]$A, [hashtable]$B, [double]$Seconds) {
    $adcDelta = [long]$B['adc'] - [long]$A['adc']
    $encIrqDelta = [long]$B['enc_irq'] - [long]$A['enc_irq']
    $encOkDelta = [long]$B['enc_ok'] - [long]$A['enc_ok']
    $encErrDelta = [long]$B['enc_err'] - [long]$A['enc_err']
    $uartErrDelta = [long]$B['uart_err'] - [long]$A['uart_err']
    return [ordered]@{
        adc_delta = $adcDelta
        enc_irq_delta = $encIrqDelta
        enc_ok_delta = $encOkDelta
        enc_err_delta = $encErrDelta
        uart_err_delta = $uartErrDelta
        adc_hz = [Math]::Round($adcDelta / $Seconds, 3)
        enc_irq_hz = [Math]::Round($encIrqDelta / $Seconds, 3)
        enc_ok_rate = if ($encIrqDelta -gt 0) { [Math]::Round($encOkDelta / [double]$encIrqDelta, 6) } else { 0.0 }
    }
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    Send-Usart ''   # optional clear-line sync to drop leftover partial text

    $startRaw = Get-Status
    Ensure-StatusFields $startRaw 'Start'
    Check-SafetyState $startRaw 'Start'

    $start = Wait-StatusWindow {
            param($s)
            ($s['self_left'] -eq 0 -and $s['rate'] -eq 1)
        } 'Startup self_left/rate not ready in 5s' 5 $startRaw['enc_err'] $startRaw['uart_err'] 'Startup'

    Check-SafetyState $start 'Startup'
    Check-LoopState $start 'Startup'
    Check-ErrorBaseline $start $startRaw['enc_err'] $startRaw['uart_err'] 'Startup'

    $baselineEncErr = [long]$start['enc_err']
    $baselineUartErr = [long]$start['uart_err']
    $lastEncOk = [long]$start['enc_ok']
    # Cached evidence only; this command must not wake/configure the TI driver.
    $driverDiagBefore = Expect-Pattern 'DRV_DIAG' '^DRV_DIAG (NONE|cached=1 stage=)'

    for ($i = 0; $i -lt $SelfTestRuns; $i++) {
        [void](Expect-Pattern 'SELFTEST' '^OK SELFTEST_NO_OUTPUT$')
        $selfStatus = Wait-StatusWindow {
                param($s)
                ((OffHealthy $s) -and $s['self_left'] -eq 0 -and $s['self_fail'] -eq 0 -and (($s['health'] -band 34) -eq 34))
            } "SELFTEST #$($i + 1) did not recover in 5s" 5 $baselineEncErr $baselineUartErr "Selftest #$($i + 1)" -RequireLoopChecks
        Assert-Ok ($selfStatus['enc_ok'] -gt $lastEncOk) ('enc_ok did not grow after SELFTEST #' + ($i + 1))
        $lastEncOk = [long]$selfStatus['enc_ok']
        Check-SafetyState $selfStatus ('Selftest #'+($i + 1))
        Check-LoopState $selfStatus ('Selftest #'+($i + 1))
    }

    $runStart = Get-Status
    Ensure-StatusFields $runStart 'RunStart'
    Check-SafetyState $runStart 'RunStart'
    Check-LoopState $runStart 'RunStart'
    Check-ErrorBaseline $runStart $baselineEncErr $baselineUartErr 'RunStart'
    Assert-Ok ((OffHealthy $runStart) -and $runStart['self_fail'] -eq 0) 'off+health state invalid before sampling'
    Assert-Ok ($runStart['enc_ok'] -ge $lastEncOk) 'enc_ok must not regress before sampling'

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $lastProgress = -1
    while ($sw.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $s = Get-Status
        Ensure-StatusFields $s 'Run'
        Check-SafetyState $s 'Run'
        Check-LoopState $s 'Run'
        Check-ErrorBaseline $s $baselineEncErr $baselineUartErr 'Run'
        Assert-Ok ((OffHealthy $s) -and $s['self_fail'] -eq 0) 'runtime mode/state invalid'
        Assert-Ok ($s['enc_ok'] -ge $lastEncOk) 'enc_ok regression during run'
        $lastEncOk = [long]$s['enc_ok']

        $elapsedSec = [int][Math]::Floor($sw.Elapsed.TotalSeconds)
        if (($elapsedSec % 30) -eq 0 -and $elapsedSec -ne $lastProgress) {
            Write-Host ("[{0}] t={1}s enc_ok={2} enc_err={3} uart_err={4}" -f (Get-Date).ToString('HH:mm:ss'), $elapsedSec, $s['enc_ok'], $s['enc_err'], $s['uart_err'])
            $lastProgress = $elapsedSec
        }
        Start-Sleep -Milliseconds ([Math]::Max(1, 1000 - [int]$sw.ElapsedMilliseconds % 1000))
    }
    $sw.Stop()

    $runEnd = Get-Status
    Ensure-StatusFields $runEnd 'RunEnd'
    Check-SafetyState $runEnd 'RunEnd'
    Check-LoopState $runEnd 'RunEnd'
    Check-ErrorBaseline $runEnd $baselineEncErr $baselineUartErr 'RunEnd'
    Assert-Ok ((OffHealthy $runEnd) -and $runEnd['self_fail'] -eq 0) 'end-state off/health/self_fail invalid'
    Assert-Ok ($runEnd['isr_max'] -lt 3600 -and $runEnd['self_max'] -lt 3600) "end isr_max/self_max too high: $($runEnd['isr_max'])/$($runEnd['self_max'])"

    $dur = [Math]::Max(1e-6, $sw.Elapsed.TotalSeconds)
    $metric = Build-Metrics $runStart $runEnd $dur
    Assert-Ok ($runEnd['isr_max'] -lt 3600) "end isr_max too high: $($runEnd['isr_max'])"
    Assert-Ok ($runEnd['self_max'] -lt 3600) "end self_max too high: $($runEnd['self_max'])"
    Assert-Ok ($metric['enc_err_delta'] -eq 0) 'enc_err delta must be 0 since baseline'
    Assert-Ok ($metric['uart_err_delta'] -eq 0) 'uart_err delta must be 0 since baseline'
    Assert-Ok ($metric['enc_ok_delta'] -gt 0) 'enc_ok must grow during run'
    Assert-Ok ($metric['enc_ok_rate'] -ge 0.999) "enc_ok coverage too low: $($metric['enc_ok_rate'])"
    Assert-Ok ($metric['adc_hz'] -ge 20000 * 0.98 -and $metric['adc_hz'] -le 20000 * 1.02) "adc_hz $($metric['adc_hz']) out of ±2%"
    Assert-Ok ($metric['enc_irq_hz'] -ge 4000 * 0.98 -and $metric['enc_irq_hz'] -le 4000 * 1.02) "enc_irq_hz $($metric['enc_irq_hz']) out of ±2%"
    $driverDiagAfter = Expect-Pattern 'DRV_DIAG' '^DRV_DIAG (NONE|cached=1 stage=)'
    Assert-Ok ($driverDiagAfter -ceq $driverDiagBefore) 'cached driver diagnostics changed during encoder-only selftests'

    $result.result = 'PASS'
    $result.duration_seconds = [Math]::Round($dur, 3)
    $result.checks = $checkCount
    $result.status_reads = $statusReads
    $result.status = @{ start = $runStart; end = $runEnd; startup = $start; selftest_runs = $SelfTestRuns}
    $result.counters = @{initial_enc_ok = $runStart['enc_ok']; final_enc_ok = $runEnd['enc_ok']; baseline_enc_err = $baselineEncErr; baseline_uart_err = $baselineUartErr}
    $result.metrics = $metric
    $result.driver_diag = @{before = $driverDiagBefore; after = $driverDiagAfter; unchanged = $true}
    $result.paths = @{transcript = $transcriptPath; result_json = $resultPath}
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    $result | ConvertTo-Json -Depth 8
} catch {
    $result.result = 'FAIL'
    $result.error = $_.Exception.Message
    $result.checks = $checkCount
    $result.status_reads = $statusReads
    $result.paths = @{transcript = $transcriptPath; result_json = $resultPath}
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    throw
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $logWriter.Dispose()
    $serial.Dispose()
}
