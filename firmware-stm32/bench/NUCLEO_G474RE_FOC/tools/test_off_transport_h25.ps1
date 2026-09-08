[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Port,[Parameter(Mandatory=$true)][string]$OutDir)
# No ALIGN/IQ/HAPTIC/BREAK/flash/supply writes. PREPARE keeps all six PWM inputs low.
$ErrorActionPreference='Stop'
$out=[IO.Path]::GetFullPath($OutDir);$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if(!$out.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)){throw 'New repository evidence directory required'}
[void][IO.Directory]::CreateDirectory($out)
$s=[IO.Ports.SerialPort]::new($Port,115200,'None',8,'One');$s.NewLine=[string][char]10;$s.ReadTimeout=10;$s.WriteTimeout=300
$log=[IO.StreamWriter]::new((Join-Path $out 'serial.log'),$false,[Text.UTF8Encoding]::new($false));$log.AutoFlush=$true
$result=[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Result='INCOMPLETE';MotorOutputCommandSent=$false;OutputStopped=$false;Pings=0;PrepareStopCycles=0;PrepareCancellationCycles=0;RejectedLines=0;FragmentedLines=0}
function WriteLine([string]$command){$log.WriteLine("$([DateTime]::UtcNow.ToString('o')) TX $command");$s.WriteLine($command)}
function Receive([string]$pattern){$watch=[Diagnostics.Stopwatch]::StartNew();while($watch.ElapsedMilliseconds -lt 4000){try{$line=$s.ReadLine().Trim();if(!$line){continue};$log.WriteLine("$([DateTime]::UtcNow.ToString('o')) RX $line");if($line -match $pattern){return $line};if($line -match '^ERR |^BOOT |^EVENT FAULT'){throw "Unexpected $line"}}catch [TimeoutException]{}};throw "Timeout: $pattern"}
function Command([string]$command,[string]$pattern){WriteLine $command;return Receive $pattern}
function Status{ $line=Command 'STATUS' '^STATUS ';$st=@{};foreach($m in [regex]::Matches($line,'(\w+)=(-?\d+)')){$st[$m.Groups[1].Value]=[long]$m.Groups[2].Value};return $st }
function Healthy($st){foreach($k in @('fault','moe','deadline','adc_bad','enc_err','uart_err','self_fail','self_left')){if($st[$k] -ne 0){throw "Not healthy: $k=$($st[$k])"}};if($st.rate -ne 1 -or $st.vm_mv -lt 9000 -or $st.vm_mv -gt 15000){throw 'Rate/VM not qualified'}}
function Off{[void](Command 'STOP' '^OK STOP$');$st=Status;if($st.mode -notin @(0,5) -or $st.moe -ne 0 -or $st.off -ne 1 -or $st.calibrated -ne 0 -or $st.zero -ne 0){throw 'STOP state not confirmed'};$result.OutputStopped=$true;return $st}
try{
 $s.Open();$s.DiscardInBuffer();$result.Before=Off;Healthy $result.Before
 $result.EncoderField=Command 'ENC_FIELD' '^ENC_FIELD ';[void](Receive '^OK ENC_FIELD_READ_ONLY_OUTPUT_OFF$')
 for($batch=0;$batch -lt 250;$batch++){
  # <= 128 RX bytes per burst, not a deliberate overflow or hardware error.
  for($i=0;$i -lt 8;$i++){WriteLine ("PING {0:d4}" -f ($batch*8+$i))}
  for($i=0;$i -lt 8;$i++){[void](Receive ('^PONG '+("{0:d4}" -f ($batch*8+$i))+'$'));$result.Pings++}
  if($batch%25 -eq 0){$st=Status;Healthy $st;if($st.mode -ne 0 -or $st.off -ne 1){throw 'Burst traffic altered OFF'}}
 }
 for($split=1;$split -lt 12;$split++){
  $line='PING split12';$log.WriteLine("TX FRAGMENT split=$split");$s.Write($line.Substring(0,$split));Start-Sleep -Milliseconds 2;$s.WriteLine($line.Substring($split));[void](Receive '^PONG split12$');$result.FragmentedLines++
 }
 # Parser discards complete invalid frames. No UART BREAK or induced RX overflow.
 foreach($line in @((('X'*96)+'PREPARE'),('PING '+[char]0+'PREPARE'),('PING '+[char]127+'PREPARE'))){$log.WriteLine('TX INVALID_FRAME');$s.WriteLine($line);[void](Receive '^ERR LINE_TOO_LONG_OR_BINARY$');$result.RejectedLines++;[void](Command 'PING resync' '^PONG resync$');$st=Status;Healthy $st;if($st.mode -ne 0 -or $st.off -ne 1){throw 'Invalid frame armed or prepared device'}}
 for($i=0;$i -lt 50;$i++){
  $result.OutputStopped=$false;[void](Command 'PREPARE' '^OK PREPARED_NO_OUTPUT$');$st=Status;Healthy $st
  if($st.mode -ne 1 -or $st.zero -ne 1 -or $st.health -ne 63 -or $st.calibrated -ne 0){throw 'PREPARE state mismatch'}
  $st=Off;Healthy $st;$result.PrepareStopCycles++
 }
 for($i=0;$i -lt 20;$i++){
  WriteLine 'PREPARE';WriteLine 'STOP';[void](Receive '^OK STOP$');$st=Status;Healthy $st
  Start-Sleep -Milliseconds 100 # Exceeds PREPARE qualification time; detect delayed rearm.
  $st=Status;Healthy $st;if($st.mode -ne 0 -or $st.zero -ne 0 -or $st.off -ne 1){throw 'Cancelled prepare completed later'};$result.PrepareCancellationCycles++
 }
 if($result.Pings -ne 2000 -or $result.PrepareStopCycles -ne 50 -or $result.PrepareCancellationCycles -ne 20 -or $result.RejectedLines -ne 3 -or $result.FragmentedLines -ne 11){throw 'Test-plan coverage mismatch'}
 $result.FinalStatus=Off;Healthy $result.FinalStatus;$result.Result='OFF_TRANSPORT_AND_PREPARE_STRESS_PASS'
}catch{$result.Result='FAIL';$result.Error=$_.Exception.Message;throw}
finally{
 if($s.IsOpen){try{$result.FinalStatus=Off}catch{$result.Result='FAIL';$result.OutputStopped=$false;$result.StopError=$_.Exception.Message;try{$s.WriteLine('STOP')}catch{}};$s.Close()};$s.Dispose();$log.Dispose()
 $result.CompletedUtc=[DateTime]::UtcNow.ToString('o');$result | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out 'validation.json') -Encoding utf8
}
$result | Select-Object Result,Pings,PrepareStopCycles,PrepareCancellationCycles,RejectedLines,FragmentedLines,OutputStopped | ConvertTo-Json
