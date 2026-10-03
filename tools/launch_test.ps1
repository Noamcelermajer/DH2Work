# Repeat-launch startup test for the DH2Work orientation guard.
# Runs N launches, counting startup SIGSEGVs (the accelerometer/nativeSetOrientation fault).
param(
    [int]$Runs = 8,
    [string]$Adb = "C:\Users\NacWorkstation\Documents\deepseek-harness\default-workspace\tools\platform-tools\adb.exe",
    [string]$Evidence = "C:\Users\NacWorkstation\Documents\Dungeon hunter 2 Rework\_device-evidence\dh2work-r2"
)
New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
$log = Join-Path $Evidence "launch-test.txt"
"DH2Work r2 startup repeat-launch test  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Set-Content $log
$crashes = 0
$survived = 0
for ($i = 1; $i -le $Runs; $i++) {
    & $Adb logcat -c 2>&1 | Out-Null
    & $Adb shell "am force-stop local.dh2.fold7" 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    & $Adb shell "am start -n local.dh2.fold7/com.zettabridge.launcher.Dh2Activity" 2>&1 | Out-Null
    Start-Sleep -Seconds 7
    # LAUNCH GAME button centre on the 2184x1968 unfolded panel.
    & $Adb shell "input tap 1092 807" 2>&1 | Out-Null
    Start-Sleep -Seconds 28
    $procs = & $Adb shell "ps -A | grep 'local.dh2.fold7:guest'" 2>&1
    $alive = ($procs -join "`n") -match 'local\.dh2\.fold7:guest'
    $segv = (& $Adb shell "logcat -d -v brief 2>/dev/null | grep -c 'guest SIGSEGV'" 2>&1) -join ''
    $segv = ($segv -replace '\D', '')
    if (-not $segv) { $segv = '0' }
    $pc = (& $Adb shell "logcat -d -v brief 2>/dev/null | grep 'guest SIGSEGV' | tail -1" 2>&1) -join ' '
    if ($alive) { $survived++ } else { $crashes++ }
    $line = "run {0,2}: guest_alive={1,-5} sigsegv_count={2}  {3}" -f $i, $alive, $segv, $pc.Trim()
    $line | Add-Content $log
    Write-Output $line
}
$summary = "SUMMARY runs=$Runs survived=$survived died=$crashes"
$summary | Add-Content $log
Write-Output $summary
