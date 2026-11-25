$out = 'C:/Users/jeffr/Fretwork/render/cpu_20261115/load_full.log'
$end = (Get-Date).AddHours(5)
while ((Get-Date) -lt $end -and -not (Test-Path 'C:/Users/jeffr/Fretwork/render/cpu_20261115/full.done')) {
  $v = (Get-Counter '\Processor(_Total)\% Processor Time').CounterSamples[0].CookedValue
  $top = (Get-Process | Sort-Object CPU -Descending | Select-Object -First 3 | ForEach-Object { $_.ProcessName }) -join '|'
  Add-Content -Path $out -Value ("{0} total={1:N1}% top={2}" -f (Get-Date -Format o), $v, $top)
  Start-Sleep -Seconds 15
}
