[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [switch]$StatusOnly,
    [switch]$ConfirmBenchWiring
)
$ErrorActionPreference = 'Stop'
if (-not $StatusOnly -and -not $ConfirmBenchWiring) {
    throw 'Read README_CN.md and check wiring, secured motor and current-limited supply first. Then use -ConfirmBenchWiring. For read-only inspection use -StatusOnly.'
}
$taskSerial = New-Object IO.Ports.SerialPort $Port,115200,'None',8,'One'
$taskSerial.NewLine = "`n"
$taskSerial.ReadTimeout = 20
$taskSerial.WriteTimeout = 500

function Invoke-BenchCommand([string]$Command, [string]$DonePattern,
                             [int]$TimeoutMs = 2000, [bool]$KeepAlive = $false) {
    $taskSerial.WriteLine($Command)
    $watchTask = [Diagnostics.Stopwatch]::StartNew()
    $lastLeaseTask = -100L
    $lastStatusTask = 0L
    while ($watchTask.ElapsedMilliseconds -lt $TimeoutMs) {
        if ($KeepAlive -and $watchTask.ElapsedMilliseconds - $lastLeaseTask -ge 30) {
            $taskSerial.WriteLine('KEEPALIVE')
            $lastLeaseTask = $watchTask.ElapsedMilliseconds
        }
        if ($KeepAlive -and $watchTask.ElapsedMilliseconds - $lastStatusTask -ge 250) {
            $taskSerial.WriteLine('STATUS')
            $lastStatusTask = $watchTask.ElapsedMilliseconds
        }
        try {
            $lineTask = $taskSerial.ReadLine().Trim()
            if (-not $lineTask) { continue }
            Write-Host $lineTask
            if ($lineTask -match '^ERR ' -or $lineTask -eq 'EVENT FAULT_LATCHED' -or
                ($KeepAlive -and $lineTask -match '^BOOT ')) {
                if (-not $StatusOnly) { $taskSerial.WriteLine('STOP') }
                Write-Warning 'Stopped. Inspect the cause before CLEAR / PREPARE / ALIGN again; do not bypass a fault.'
                return $false
            }
            if ($lineTask -match $DonePattern) { return $true }
        } catch [TimeoutException] { }
    }
    if (-not $StatusOnly) { $taskSerial.WriteLine('STOP') }
    throw "No completion within ${TimeoutMs} ms for $Command; inspect the board before retrying."
}

try {
    $taskSerial.Open()
    $taskSerial.DiscardInBuffer()
    if ($StatusOnly) {
        [void](Invoke-BenchCommand 'STATUS' '^STATUS ')
        return
    }
    [void](Invoke-BenchCommand 'STOP' '^OK STOP$')
    [void](Invoke-BenchCommand 'STATUS' '^STATUS ')
    Write-Host 'No automatic motor action. Enter each operation manually.'
    Write-Host 'Commands: status | arm_diag | drv_diag | drv_poll | enc_diag | enc_field | current_diag | enc_clear | prepare | align | iq 50 200 | haptic 1 5000 | stop | clear | quit'
    Write-Host 'HAPTIC: 0=free, 1=damping, 2=24 detents, 3=spring, 4=endstops +/-45deg, 5=friction, 6=inertia, 7=velocity 1rad/s. Iq <=100mA, duration <=10s.'
    Write-Host 'ARM_DIAG reads the last arm snapshot in OFF/FAULT; no SPI, fault clearing, or output enable.'
    Write-Host 'DRV_DIAG reads the last configure snapshot only; it does not perform driver SPI, clear faults, or enable outputs.'
    Write-Host 'DRV_POLL reads the first failed periodic driver poll in OFF/FAULT; no hardware operation.'
    Write-Host 'ENC_DIAG reads cached raw encoder evidence; ENC_CLEAR explicitly reads/clears sensor errors once. Both require outputs OFF.'
    Write-Host 'ENC_FIELD reads angle/field diagnostics without writing sensor registers. CURRENT_DIAG reads live ADC and the retained first-fault snapshot. Outputs must be OFF.'
    Write-Host 'ALIGN / IQ / HAPTIC start on command; holding NUCLEO B1 is no longer required.'
    Write-Host 'B1 is not a stop control. Remove 12V to stop physically; host heartbeat loss, limits and driver faults still stop output.'
    Write-Host 'PREPARE enables driver electronics but NOT bridge outputs. ALIGN can move the rotor.'
    while ($true) {
        $commandTask = (Read-Host 'FOC (outputs idle while waiting for input)').Trim().ToUpperInvariant()
        switch -Regex ($commandTask) {
            '^STATUS$' { [void](Invoke-BenchCommand 'STATUS' '^STATUS '); break }
            '^ARM_DIAG$' { [void](Invoke-BenchCommand 'ARM_DIAG' '^OK ARM_DIAG_CACHED_OUTPUT_OFF$'); break }
            '^DRV_DIAG$' { [void](Invoke-BenchCommand 'DRV_DIAG' '^OK DRV_DIAG_OUTPUT_OFF$'); break }
            '^DRV_POLL$' { [void](Invoke-BenchCommand 'DRV_POLL' '^OK DRV_POLL_OUTPUT_OFF$'); break }
            '^ENC_DIAG$' { [void](Invoke-BenchCommand 'ENC_DIAG' '^OK ENC_DIAG_OUTPUT_OFF$'); break }
            '^ENC_FIELD$' { [void](Invoke-BenchCommand 'ENC_FIELD' '^OK ENC_FIELD_READ_ONLY_OUTPUT_OFF$'); break }
            '^CURRENT_DIAG$' { [void](Invoke-BenchCommand 'CURRENT_DIAG' '^OK CURRENT_DIAG_OUTPUT_OFF$'); break }
            '^ENC_CLEAR$' { [void](Invoke-BenchCommand 'ENC_CLEAR' '^OK ENC_CLEAR_ATTEMPT_OUTPUT_OFF$'); break }
            '^PREPARE$' { [void](Invoke-BenchCommand 'PREPARE' '^OK PREPARED_NO_OUTPUT$' 3000); break }
            '^ALIGN$' { [void](Invoke-BenchCommand 'ALIGN' '^OK ALIGNED_RAM_ONLY$' 7000 $true); break }
            '^IQ [+-]?\d+ \d+$' {
                [void](Invoke-BenchCommand $commandTask '^OK PULSE_COMPLETE_OUTPUT_OFF$' 3000 $true)
                break
            }
            '^HAPTIC [0-7] \d+$' {
                [void](Invoke-BenchCommand $commandTask '^OK PULSE_COMPLETE_OUTPUT_OFF$' 11000 $true)
                break
            }
            '^STOP$' { [void](Invoke-BenchCommand 'STOP' '^OK STOP$'); break }
            '^CLEAR$' { [void](Invoke-BenchCommand 'CLEAR' '^OK CLEAR_REQUIRES_PREPARE_AND_ALIGNMENT$'); break }
            '^(QUIT|EXIT)$' { return }
            default { Write-Host 'Use status / arm_diag / drv_diag / drv_poll / enc_diag / enc_field / current_diag / enc_clear / prepare / align / iq <signed_mA> <ms> / haptic <0..7> <ms> / stop / clear / quit.' }
        }
    }
} finally {
    if ($taskSerial.IsOpen) {
        if (-not $StatusOnly) {
            try { $taskSerial.WriteLine('STOP') } catch { }
        }
        $taskSerial.Close()
    }
    $taskSerial.Dispose()
}
