# run-suite.ps1 - GRIDEd's GUI regression suite: builds, runs and debugs the three test projects
# (C90 cdemo, C++11 cppshapes, Shalimar shlmatrix) on Windows and TMS320C6747 through GRIDE itself,
# and prints PASS / FAIL per check. About two minutes. Needs the desktop (it sends keys to GRIDE);
# leave the mouse and keyboard alone while it runs.
#
#   powershell -ExecutionPolicy Bypass -File tests\gui\run-suite.ps1 [-Gride <path to GRIDE.exe>] [-Only <name regex>]
#
# Exit code: the number of failed checks.
param([string]$Gride, [string]$Only = '.')

$ErrorActionPreference = 'Continue'
if ($Gride) { $global:GrideExe = [IO.Path]::GetFullPath($Gride) }
$global:S = Join-Path $env:TEMP 'gride-gui-tests'
. "$PSScriptRoot\lib\session.ps1"
if (-not (Test-Path $GrideExe)) { Write-Host "GRIDE.exe not found: $GrideExe"; exit 1 }

# a fresh working copy of the projects (no obj/out), so every run builds from nothing
if (Test-Path $E2E) { Remove-Item -Recurse -Force $E2E }
Copy-Item -Recurse "$PSScriptRoot\projects" $E2E

$results = New-Object System.Collections.Generic.List[object]
function Check($scenario, $what, $text, $expect) {
    $ok = $text -match $expect
    $line = (($text -split "`r?`n" | Where-Object { $_.Trim() }) | Select-Object -Last 1)
    $results.Add([pscustomobject]@{ Scenario = $scenario; Check = $what; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Saw = "$line".Trim() })
}
function Fresh($proj, $config, $target, $bps, $open) {
    KillAll; Write-Ws $proj $config $target $bps
    $a = @("$E2E\e2e.grws"); if ($open) { $a += "$E2E\$open" }
    Start-GrideWith $a
}
# the text a step added to the Debug log
function StepText($item) { (Dbg-Step $item $item) -join "`n" }
# the Locals pane's rows as "name | value | type | address", one per line (read with Win32 ListView messages)
function LocalsText {
    Start-Sleep -Milliseconds 800
    (ListViews | ForEach-Object { LV-Rows $_.H 4 } | Where-Object { $_ -match '\S' }) -join "`n"
}

# ---- builds: the whole workspace, both configurations, both targets ----
foreach ($target in 'Windows', 'TMS320C6747') {
    foreach ($config in 'Debug', 'Release') {
        $name = "build $config $target"
        if ($name -notmatch $Only) { continue }
        Fresh 'cdemo' $config $target $null $null
        Menu 'Build', 'Rebuild Workspace'
        $o = Wait-Out '\d+ succeeded, \d+ failed' 240
        Check $name '3 projects built' $o '3 succeeded, 0 failed'
    }
}

# ---- debug sessions: [step, expected Debug line] ----
$debug = @(
    @{ Name = 'debug C90 Windows'; Proj = 'cdemo'; Target = 'Windows'; Bps = '{ "cdemo/src/main.c": [8] }'; Open = 'cdemo\src\main.c'; Steps = @(
        @('Start Debugging', 'stopped at main\.c:8'), @('Locals', 'n \| 4 \| int'), @('Step Into', 'stopped at greet\.c:\d+ in greet'), @('Step Out', 'stopped at main\.c:8'),
        @('Step Over', 'stopped at main\.c:9'), @('Step Into', 'stopped at main\.c:10'), @('Start Debugging', 'returned 3')) },
    @{ Name = 'debug C90 C6747'; Proj = 'cdemo'; Target = 'TMS320C6747'; Bps = '{ "cdemo/src/main.c": [8] }'; Open = 'cdemo\src\main.c'; Steps = @(
        @('Start Debugging', 'stopped at main\.c:8'), @('Locals', 'n \| 4 \| int \| 0x'), @('Step Over', 'stopped at main\.c:9'), @('Step Into', 'stopped at printf\.cpp'), @('Start Debugging', 'returned 3')) },
    @{ Name = 'debug C++11 Windows'; Proj = 'cppshapes'; Target = 'Windows'; Bps = '{ "cppshapes/src/main.cpp": [9] }'; Open = 'cppshapes\src\main.cpp'; Steps = @(
        @('Start Debugging', 'stopped at main\.cpp:9'), @('Step Into', 'stopped at rect\.cpp:16'), @('Step Out', 'stopped at main\.cpp:9'), @('Start Debugging', 'returned 0')) },
    @{ Name = 'debug C++11 C6747'; Proj = 'cppshapes'; Target = 'TMS320C6747'; Bps = '{ "cppshapes/src/main.cpp": [9] }'; Open = 'cppshapes\src\main.cpp'; Steps = @(
        @('Start Debugging', 'stopped at main\.cpp:9'), @('Locals', 'shapes \| \{\.\.\.\} \| class vector'), @('Step Over', 'stopped at main\.cpp:10'), @('Start Debugging', 'returned 0')) },
    @{ Name = 'debug Shalimar Windows'; Proj = 'shlmatrix'; Target = 'Windows'; Bps = '{ "shlmatrix/src/matrix.shl": [9] }'; Open = $null; Steps = @(
        @('Start Debugging', 'stopped at matrix\.shl:9'), @('Step Into', 'stopped at linalg\.shl:\d+ in mul'), @('Step Out', 'stopped at matrix\.shl:(9|10)'),
        @('Step Over', 'stopped at matrix\.shl:\d+'), @('Step Out', 'no source to show'), @('Start Debugging', 'returned 0')) },
    @{ Name = 'debug Shalimar C6747'; Proj = 'shlmatrix'; Target = 'TMS320C6747'; Bps = '{ "shlmatrix/src/matrix.shl": [9] }'; Open = $null; Steps = @(
        @('Start Debugging', 'stopped at matrix\.shl:9'), @('Step Into', 'stopped at linalg\.shl:\d+'), @('Locals', '(?s)a \| \{\.\.\.\} \| real\[\]\[\].*i \| \S+ \| int'),
        @('Step Out', 'stopped at matrix\.shl:\d+'), @('Start Debugging', 'returned 0')) }
)
foreach ($d in $debug) {
    if ($d.Name -notmatch $Only) { continue }
    Fresh $d.Proj 'Debug' $d.Target $d.Bps $d.Open
    Get-Output 'Debug' | Out-Null
    foreach ($s in $d.Steps) {
        if ($s[0] -eq 'Locals') { Check $d.Name 'Locals' (LocalsText) $s[1]; continue }
        $t = StepText $s[0]
        $box = Dialogs
        if ($box) { $t += "`n[message box] " + ($box -join ' '); Close-Dialogs }
        Check $d.Name $s[0] $t $s[1]
        if ($box) { $results[$results.Count - 1].Result = 'FAIL' }
    }
}
KillAll

$results | Format-Table -AutoSize -Wrap | Out-String -Width 220 | Write-Host
$failed = @($results | Where-Object Result -eq 'FAIL').Count
Write-Host ("{0} checks, {1} failed" -f $results.Count, $failed)
exit $failed
