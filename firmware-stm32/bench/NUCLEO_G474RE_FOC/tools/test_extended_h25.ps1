[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Port,
 [Parameter(Mandatory=$true)][string]$BuildDir,
 [Parameter(Mandatory=$true)][string]$Programmer,
 [Parameter(Mandatory=$true)][string]$Python,
 [Parameter(Mandatory=$true)][string]$OutDir,
 [switch]$RunBoundedHardware
)
# H25 only. Does not flash, raise limits, send BREAK, or change supply settings.
$ErrorActionPreference='Stop'
if(!$RunBoundedHardware){throw 'Explicit -RunBoundedHardware required; read the bench README and test plan first'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$out=[IO.Path]::GetFullPath($OutDir)
if(!$out.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)){throw 'Use a NEW evidence directory inside this repository'}
$expected='B98161DB5EDAE02EBCE29F0F645405C2DA73CC1B99387A67D55181E98964DFB7'
$bin=Join-Path $BuildDir 'NUCLEO_G474RE_FOC_HAPTIC25.bin'
if((Get-FileHash -LiteralPath $bin).Hash -ne $expected){throw 'Wrong H25 binary'}
& $Python (Join-Path $PSScriptRoot 'verify_source_identity.py') (Join-Path $BuildDir 'source-identity.json')
if($LASTEXITCODE -ne 0){throw 'H25 source identity verification failed'}
[void][IO.Directory]::CreateDirectory($out)
$s=[IO.Ports.SerialPort]::new($Port,115200,'None',8,'One');$s.NewLine=[string][char]10;$s.ReadTimeout=10;$s.WriteTimeout=300
$log=[IO.StreamWriter]::new((Join-Path $out 'serial.log'),$false,[Text.UTF8Encoding]::new($false));$log.AutoFlush=$true
$clock=[Diagnostics.Stopwatch]::StartNew();$script:active=$false;$script:nextLease=0L;$script:nextStatus=0L
$statuses=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new()
$result=[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Result='INCOMPLETE';Firmware='20260907_HAPTIC25';BinarySHA256=$expected;SupplySettingChanged=$false;ProtectionChanges=$false;InstrumentedThermalTest=$false;Cases=$cases;OutputStopped=$false}
function WriteCommand([string]$text){$log.WriteLine("$([DateTime]::UtcNow.ToString('o')) TX $text");$s.WriteLine($text)}
function Receive([string]$pattern,[int]$timeout=4000){
 $lines=[Collections.Generic.List[string]]::new();$watch=[Diagnostics.Stopwatch]::StartNew()
 while($watch.ElapsedMilliseconds -lt $timeout){
  if($script:active -and $clock.ElapsedMilliseconds -ge $script:nextLease){WriteCommand 'KEEPALIVE';$script:nextLease=$clock.ElapsedMilliseconds+20}
  if($script:active -and $clock.ElapsedMilliseconds -ge $script:nextStatus){WriteCommand 'STATUS';$script:nextStatus=$clock.ElapsedMilliseconds+180}
  try{
   $line=$s.ReadLine().Trim();if(!$line){continue};$log.WriteLine("$([DateTime]::UtcNow.ToString('o')) RX $line");$lines.Add($line)
   if($line -match '^STATUS '){$st=[ordered]@{HostMs=$clock.ElapsedMilliseconds};foreach($m in [regex]::Matches($line,'(\w+)=(-?\d+)')){$st[$m.Groups[1].Value]=[long]$m.Groups[2].Value};$statuses.Add([pscustomobject]$st)}
   if($line -match $pattern){return $lines.ToArray()}
   if($line -match '^ERR |^BOOT |^EVENT FAULT'){throw "Unexpected device response: $line"}
  }catch [TimeoutException]{}
 }
 throw "Timeout waiting for $pattern"
}
function Send([string]$text,[string]$pattern,[int]$timeout=4000){WriteCommand $text;return Receive $pattern $timeout}
function Status{[void](Send 'STATUS' '^STATUS ');return $statuses[-1]}
function CheckCounters($st){foreach($name in @('deadline','adc_bad','uart_err','enc_err','self_fail','self_left')){if($st.$name -ne 0){throw "Diagnostic error: $name=$($st.$name)"}};if($st.rate -ne 1 -or $st.vm_mv -lt 9000 -or $st.vm_mv -gt 15000){throw 'Rate/VM health failed'}}
function CheckReady($st){CheckCounters $st;if($st.mode -ne 3 -or $st.calibrated -ne 1 -or $st.fault -ne 0 -or $st.moe -ne 0 -or $st.health -ne 63 -or $st.zero -ne 1){throw 'Not READY with output disabled'}}
function StopOutput{
 $script:active=$false;[void](Send 'STOP' '^OK STOP$');$st=Status
 if($st.moe -ne 0 -or $st.off -ne 1 -or $st.calibrated -ne 0 -or $st.zero -ne 0 -or $st.mode -notin @(0,5)){throw 'STOP did not hard-disable and invalidate calibration'}
 $result.OutputStopped=$true;$result.FinalStatus=$st;return $st
}
function Capture([string]$label){
 $dest=Join-Path $out $label
 & $Python (Join-Path $PSScriptRoot 'read_frozen_capture.py') --out $dest --build-dir $BuildDir --programmer $Programmer *> (Join-Path $out "$label-reader.log")
 if($LASTEXITCODE -ne 0){throw "Frozen capture failed: $label"}
 return Get-Content (Join-Path $dest 'validation.json') -Raw | ConvertFrom-Json
}
function CheckCapture($capture,[int]$fault=0,[double]$phaseMax=0.35){
 if($capture.Result -ne 'FROZEN_CAPTURE_READ_PASS' -or $capture.Total -le 0 -or $capture.Summary.NotSynchronized -ne 0 -or $capture.IdleOutlierTriggered -ne 0 -or $capture.Gate.Faults -ne $fault -or $capture.Summary.MaxAbsPhaseA -gt $phaseMax){throw 'Capture health failed'}
 if($fault -eq 0 -and $capture.Trip.Captured -ne 0){throw 'Unexpected first-trip snapshot'}
 if($fault -ne 0 -and ($capture.Trip.Captured -ne 1 -or $capture.Trip.Fault -ne $fault)){throw 'Expected fault evidence absent or contaminated'}
 if($capture.Count -gt 1 -and ($capture.Summary.GapUsMin -ne 50 -or $capture.Summary.GapUsMax -ne 50)){throw 'ADC cadence differs from 20kHz'}
}
function StartLease{$result.OutputStopped=$false;$script:active=$true;$script:nextLease=0;$script:nextStatus=$clock.ElapsedMilliseconds+100}
function Settled{
 $watch=[Diagnostics.Stopwatch]::StartNew();$stable=[Collections.Generic.List[long]]::new()
 while($watch.ElapsedMilliseconds -lt 3000){
  $st=Status;CheckReady $st
  if([Math]::Abs($st.vel_mrad_s) -le 1500){$stable.Add([long]$st.angle_mrad)}else{$stable.Clear()}
  if($stable.Count -ge 5){$range=$stable | Measure-Object -Maximum -Minimum;if($range.Maximum-$range.Minimum -le 10){return $st};$stable.RemoveAt(0)}
  Start-Sleep -Milliseconds 100 # Physical zero-output settling, not task polling.
 }
 throw 'No five consecutive settled observations within 3s; do not force start'
}
function Align([string]$label){
 $st=Status;CheckCounters $st;if($st.mode -ne 0 -or $st.fault -ne 0 -or $st.moe -ne 0 -or $st.off -ne 1){throw 'Alignment requires healthy hard-OFF'}
 $case=[ordered]@{Name=$label;Command='PREPARE + ALIGN';Before=$st;Result='INCOMPLETE'};$cases.Add($case)
 [void](Send 'PREPARE' '^OK PREPARED_NO_OUTPUT$');$st=Status
 if($st.mode -ne 1 -or $st.health -ne 63 -or $st.zero -ne 1 -or $st.moe -ne 0 -or $st.fault -ne 0){throw 'PREPARE failed'}
 for($i=0;$i -lt 12;$i++){
  $lines=@(Send 'CURRENT_DIAG' '^OK CURRENT_DIAG_OUTPUT_OFF$');$live=@($lines | Where-Object {$_ -match '^CURRENT_LIVE '})[0]
  if($live -notmatch 'raw=(\d+),(\d+),(\d+),(\d+) zero_mc=(\d+),(\d+),(\d+) valid=1 sync=1 bad=0'){throw 'Invalid prepared ADC zero'}
  $values=$Matches.Clone();for($phase=1;$phase -le 3;$phase++){if([Math]::Abs(([double]$values[$phase]-[double]$values[$phase+4]/1000)*3.3/4095/0.6) -gt 0.02){throw 'Prepared ADC drift exceeds 20mA'}}
 }
 StartLease;WriteCommand 'ALIGN';$case.Replies=@(Receive '^OK ALIGNED_RAM_ONLY$' 6500);$script:active=$false
 $case.After=Status;$case.Timing=@(Send 'TIMING_DIAG' '^OK TIMING_DIAG_OUTPUT_OFF$');$case.Capture=Capture $label
 CheckReady $case.After;CheckCapture $case.Capture 0 0.65;$case.Result='PASS'
}
function Timed([string]$label,[string]$command,[int]$duration){
 $case=[ordered]@{Name=$label;Command=$command;Before=(Settled);DurationMs=$duration;Result='INCOMPLETE'};$cases.Add($case)
 StartLease;WriteCommand $command;$case.Replies=@(Receive '^OK PULSE_COMPLETE_OUTPUT_OFF$' ($duration+2000));$script:active=$false
 $case.After=Status;$case.Timing=@(Send 'TIMING_DIAG' '^OK TIMING_DIAG_OUTPUT_OFF$');$case.Capture=Capture $label
 CheckReady $case.After;CheckCapture $case.Capture
 if([Math]::Abs($case.Capture.Total-($duration*20+1)) -gt 2){throw "Wrong timer duration: $label total=$($case.Capture.Total)"}
 $case.Result='PASS_BOUNDED_NOT_TACTILE_METROLOGY'
}
try{
 $s.Open();$s.DiscardInBuffer();$result.Before=StopOutput;CheckCounters $result.Before
 if($result.Before.fault -ne 0){throw 'Existing fault: preserve and investigate before output'}
 $readback=Join-Path $out 'flash-preflight.bin'
 & $Programmer -c port=SWD mode=HOTPLUG freq=1000 -u 0x08000000 (Get-Item $bin).Length $readback *> (Join-Path $out 'flash-preflight.log')
 if($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $readback).Hash -ne $expected){throw 'Live image identity failed'}
 $result.LiveImageVerified=$true
 Align 'align-1'
 Timed 'iq-min-positive' 'IQ 1 1' 1
 Timed 'iq-min-negative' 'IQ -1 1' 1
 Timed 'haptic-min' 'HAPTIC 0 1' 1
 Timed 'iq-duration-max' 'IQ 1 2000' 2000
 Timed 'free-duration-max' 'HAPTIC 0 10000' 10000
 for($kind=0;$kind -lt 8;$kind++){Timed "haptic-$kind-1000ms" "HAPTIC $kind 1000" 1000}
 for($i=0;$i -lt 20;$i++){Timed ("repeat-{0:d2}" -f $i) 'HAPTIC 0 50' 50}
 # All following commands are sent while zero-command FREE is actually ACTIVE.
 $case=[ordered]@{Name='active-reject-and-stop';Before=(Settled);Result='INCOMPLETE';Rejected=[Collections.Generic.List[object]]::new()};$cases.Add($case)
 StartLease;[void](Send 'HAPTIC 0 10000' '^OK HAPTIC_KEEPALIVE$');$case.Active=Status
 if($case.Active.mode -ne 4 -or $case.Active.moe -ne 1 -or $case.Active.fault -ne 0){throw 'No ACTIVE evidence before stop test'}
 $rejects=@(@('PREPARE','ERR PREPARE_NEEDS_OFF_SELFTEST_AND_VM9_15V'),@('ALIGN','ERR ALIGN_NOT_READY'),@('IQ 1 20','ERR IQ_NEEDS_ALIGNMENT'),@('HAPTIC 1 20','ERR HAPTIC_NEEDS_ALIGNMENT'),@('CLEAR','ERR CLEAR_RELEASE_BUTTON_AND_STOP'),@('SELFTEST','ERR SELFTEST_REQUIRES_OFF'),@('FAULTTEST','ERR FAULTTEST_REQUIRES_OFF'),@('CURRENT_DIAG','ERR CURRENT_DIAG_REQUIRES_OUTPUT_OFF'),@('ENC_DIAG','ERR ENC_DIAG_REQUIRES_OFF_RELEASE_BUTTON'))
 foreach($r in $rejects){$lines=@(Send $r[0] ('^'+[regex]::Escape($r[1])+'$'));$st=Status;CheckCounters $st;if($st.mode -ne 4 -or $st.fault -ne 0 -or $st.moe -ne 1 -or $st.hap -ne 1){throw 'Rejected command altered ACTIVE state'};$case.Rejected.Add([ordered]@{Command=$r[0];Response=$lines[-1];After=$st})}
 $case.After=StopOutput;$case.Capture=Capture 'active-stop';CheckCapture $case.Capture;$case.Result='PASS'
 # STOP invalidated qualification: rejected commands must not restart the bridge.
 foreach($r in @(@('IQ 1 20','ERR IQ_NEEDS_ALIGNMENT'),@('HAPTIC 0 20','ERR HAPTIC_NEEDS_ALIGNMENT'))){[void](Send $r[0] ('^'+[regex]::Escape($r[1])+'$'));$st=Status;if($st.mode -ne 0 -or $st.moe -ne 0 -or $st.off -ne 1 -or $st.calibrated -ne 0){throw 'Output restarted after STOP'}}
 Align 'align-2'
 $case=[ordered]@{Name='lease-expiry';Before=(Settled);IntentionalFault=4;Result='INCOMPLETE'};$cases.Add($case)
 # Deliberately send NO KEEPALIVE after this start; physical disconnect not needed.
 $result.OutputStopped=$false;$script:active=$false;[void](Send 'HAPTIC 0 1000' '^OK HAPTIC_KEEPALIVE$');$case.Active=Status
 if($case.Active.mode -ne 4 -or $case.Active.moe -ne 1){throw 'No ACTIVE evidence before lease expiry'}
 $case.Event=@(Receive '^EVENT FAULT_LATCHED$' 1000);$case.After=Status
 $case.Timing=@(Send 'TIMING_DIAG' '^OK TIMING_DIAG_OUTPUT_OFF$');$case.Capture=Capture 'lease-expiry'
 CheckCounters $case.After;CheckCapture $case.Capture 4
 if($case.After.mode -ne 5 -or $case.After.fault -ne 4 -or $case.After.moe -ne 0 -or $case.After.off -ne 1 -or $case.After.calibrated -ne 0){throw 'Lease did not latch hard-OFF fault4'}
 if($case.Capture.Total -lt 1999 -or $case.Capture.Total -gt 2003){throw 'Lease timing not approximately 100ms at 20kHz'}
 $case.Result='PASS_EXPECTED_FAULT4'
 [void](StopOutput)
 # One explicit recovery only AFTER the deliberately induced fault was verified.
 [void](Send 'CLEAR' '^OK CLEAR_REQUIRES_PREPARE_AND_ALIGNMENT$');$result.ExpectedFaultCleared=$true
 $result.FinalStatus=StopOutput;CheckCounters $result.FinalStatus;if($result.FinalStatus.fault -ne 0){throw 'Nonzero final fault'}
 $result.Result='EXTENDED_BOUNDED_HARDWARE_PASS'
}catch{
 $result.Result='FAIL';$result.Error=$_.Exception.Message
 # No CLEAR/retry on unexpected fault. Stop and retain whatever capture exists.
 if($s.IsOpen){try{[void](StopOutput)}catch{$result.StopError=$_.Exception.Message};try{$result.FailureCapture=Capture 'unexpected-final'}catch{$result.CaptureError=$_.Exception.Message}}
 throw
}finally{
 if($s.IsOpen){try{[void](StopOutput)}catch{$result.Result='FAIL';$result.OutputStopped=$false;$result.StopError=$_.Exception.Message;try{WriteCommand 'STOP'}catch{}};$s.Close()};$s.Dispose();$log.Dispose()
 $statuses | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out 'status.json') -Encoding utf8
 $result.CompletedUtc=[DateTime]::UtcNow.ToString('o');$result | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $out 'validation.json') -Encoding utf8
}
$result | Select-Object Result,OutputStopped,FinalStatus | ConvertTo-Json -Depth 3
