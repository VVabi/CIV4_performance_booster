<#
.SYNOPSIS
  Runs the auto-play of every test save in testsaves\ and compares the new AutoPlay.log with the reference log
  next to the save.

.DESCRIPTION
  A pair is <name>.CivBeyondSwordSave + <name>.log in testsaves\ (the .log is an AutoPlay.log of a reference run
  started from that save). For each pair:
  1. the number of "game turn" lines of the reference log is the number of turns
  2. that number is written to <mod>\autorun.txt (the mod then starts auto-play after the load and quits the game)
  3. the current Logs\AutoPlay.log is moved aside, the save is started and the script waits until the game exits
  4. autorun.txt is removed again and the per-turn lines (everything except the turn slice, which depends on the
     frame count) are compared with the reference; both run summary lines are printed

  Exit code 0 = all pairs identical, 1 = at least one differs, 2 = a run did not work.

.EXAMPLE
  .\Tools\run_reference_check.ps1
  .\Tools\run_reference_check.ps1 -Name Pangea
#>
param(
	[string]$Name = "*",
	[string]$TestSaves = "",
	[string]$LogDir = "",
	[int]$TimeoutMinutes = 60,
	[switch]$KeepAutorun
)

$ErrorActionPreference = "Stop"
$mod = Split-Path -Parent $PSScriptRoot
if (-not $TestSaves) { $TestSaves = Join-Path $mod "testsaves" }
if (-not $LogDir) { $LogDir = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "My Games\Beyond the Sword\Logs" }
$autorunTxt = Join-Path $mod "autorun.txt"
$autorunLog = Join-Path $mod "autorun.log"
$newLog = Join-Path $LogDir "AutoPlay.log"

function Get-TurnLines($path) {
	Get-Content $path | Where-Object { $_ -match '^\s+game turn \d+:' } |
		ForEach-Object { $_ -replace '\s*\(turn slice \d+\)\s*$', '' }
}

# returns 0 = identical, 1 = different, 2 = run failed
function Test-Pair($save, $reference) {
	$refLines = @(Get-TurnLines $reference)
	$turns = $refLines.Count
	if ($turns -le 0) { Write-Host "No 'game turn' lines in $reference"; return 2 }
	Write-Host "Save $save, reference $reference ($turns turns)"

	if (Test-Path $newLog) {
		$backup = Join-Path $LogDir ("AutoPlay_before_check_{0:yyyyMMdd_HHmmss}.log" -f (Get-Date))
		Move-Item $newLog $backup
		Write-Host "Old AutoPlay.log moved to $backup"
	}
	if (Test-Path $autorunLog) { Remove-Item $autorunLog }

	Set-Content -Path $autorunTxt -Value $turns -Encoding ASCII
	try {
		Write-Host "Starting the game, auto-play for $turns turns..."
		$start = Get-Date
		Start-Process -FilePath $save
		$proc = $null
		for ($i = 0; $i -lt 120 -and -not $proc; $i++) {
			Start-Sleep -Seconds 1
			$proc = Get-Process -Name "Civ4BeyondSword" -ErrorAction SilentlyContinue | Select-Object -First 1
		}
		if (-not $proc) { Write-Host "The game did not start within 2 minutes."; return 2 }
		if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
			Write-Host "The game is still running after $TimeoutMinutes minutes, leaving it alone."
			# it may still write AutoPlay.log: the remaining saves are not run (see the loop below)
			$script:bAbort = $true
			return 2
		}
		Write-Host ("Game exited after {0:N0} s." -f ((Get-Date) - $start).TotalSeconds)
	}
	finally {
		if (-not $KeepAutorun -and (Test-Path $autorunTxt)) { Remove-Item $autorunTxt }
	}

	if (-not (Test-Path $newLog)) {
		Write-Host "No new AutoPlay.log; see $autorunLog"
		if (Test-Path $autorunLog) { Get-Content $autorunLog }
		return 2
	}
	# the run worked: its autorun.log is not needed any more (it is kept above when the run failed)
	if (Test-Path $autorunLog) { Remove-Item $autorunLog }

	$newLines = @(Get-TurnLines $newLog)
	foreach ($pair in @(@("reference", $reference), @("new      ", $newLog))) {
		$summary = Get-Content $pair[1] | Where-Object { $_ -match '^AI auto-play:' } | Select-Object -Last 1
		Write-Host ("  {0}: {1}" -f $pair[0], $summary)
	}

	$n = [Math]::Min($refLines.Count, $newLines.Count)
	$first = -1
	for ($i = 0; $i -lt $n; $i++) {
		if ($refLines[$i] -ne $newLines[$i]) { $first = $i; break }
	}
	if ($first -lt 0 -and $refLines.Count -eq $newLines.Count) {
		Write-Host "IDENTICAL: all $turns turns match the reference."
		return 0
	}
	if ($first -ge 0) {
		Write-Host "DIFFERENT, first difference at line $($first + 1):"
		Write-Host "  reference:$($refLines[$first])"
		Write-Host "  new      :$($newLines[$first])"
	}
	else {
		Write-Host "DIFFERENT: the new log has $($newLines.Count) turns, the reference $($refLines.Count) (the first $n match)."
	}
	return 1
}

if (Get-Process -Name "Civ4BeyondSword" -ErrorAction SilentlyContinue) {
	Write-Host "Civ4BeyondSword is already running, close it first."; exit 2
}

$pairs = @()
foreach ($save in Get-ChildItem $TestSaves -Filter "$Name.CivBeyondSwordSave") {
	$reference = [IO.Path]::ChangeExtension($save.FullName, ".log")
	if (Test-Path $reference) { $pairs += , @($save.FullName, $reference) }
	else { Write-Host "Skipping $($save.Name): no $([IO.Path]::GetFileName($reference))" }
}
if ($pairs.Count -eq 0) { Write-Host "No save/log pairs found in $TestSaves"; exit 2 }

$results = @()
$script:bAbort = $false
foreach ($pair in $pairs) {
	Write-Host ""
	Write-Host "=== $([IO.Path]::GetFileNameWithoutExtension($pair[0])) ==="
	$results += [pscustomobject]@{ Name = [IO.Path]::GetFileNameWithoutExtension($pair[0]); Code = (Test-Pair $pair[0] $pair[1]) }
	if ($script:bAbort) {
		Write-Host ""
		Write-Host "Stopped: that game is still running, so the remaining saves were not run."
		break
	}
}

Write-Host ""
Write-Host "=== Summary ==="
foreach ($r in $results) {
	$text = @("identical", "DIFFERENT", "run failed")[$r.Code]
	Write-Host ("{0,-30} {1}" -f $r.Name, $text)
}
exit (($results | Measure-Object -Property Code -Maximum).Maximum)
