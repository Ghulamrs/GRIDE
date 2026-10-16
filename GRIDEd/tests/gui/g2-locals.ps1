# g2-locals.ps1 - GRIDE 2.0 on the TMS320C6747: rebuild a project, break at a line, F8, and print the Locals pane.
#   powershell -ExecutionPolicy Bypass -File g2-locals.ps1 -Pro <project.pro> -Source <file> -Line <n>
param([Parameter(Mandatory)][string]$Pro, [Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][int]$Line,
      [string]$Gride = 'C:\Users\GRA\work1\GRIDE\x64\Release\GRIDE.exe')
$global:GrideExe = $Gride
$global:S = Join-Path $env:TEMP 'gride-gui-tests'
. "$PSScriptRoot\lib\session.ps1"

# GRIDE 2.0's Output pane is a plain EDIT control, read while hidden too
function AllText { (All-Windows | Where-Object { $_.Cls -match '\.EDIT\.' } | ForEach-Object { [W32]::Text($_.H) }) -join "`n" }
function Key($k) { Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 300 }
function WaitFor($pattern, $seconds) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        Start-Sleep -Milliseconds 300
        if (Dialogs) { return "[dialog] " + ((Dialogs) -join ' ') }
        $t = AllText; if ($t -match $pattern) { return ($t -split "`r?`n" | Where-Object { $_ -match $pattern } | Select-Object -Last 1) }
    }
    "[TIMEOUT $pattern]"
}

KillAll
Start-GrideWith @($Pro, $Source)
Menu 'Build','Rebuild Project'; "rebuild: " + (WaitFor '=+ \w+: \d+ succeeded, \d+ failed' 60)
Key '^g'; Start-Sleep -Milliseconds 500; [System.Windows.Forms.SendKeys]::SendWait("$Line{ENTER}"); Start-Sleep -Milliseconds 400; Key '{F9}'
Key '{F8}'; "F8:      " + (WaitFor 'stopped at [^\r\n]*|stopped answering|not installed[^\r\n]*|no locals[^\r\n]*' 45)
Start-Sleep 2
foreach ($lv in ListViews) { $rows = @(LV-Rows $lv.H 4 | Where-Object { $_ -match '\S' }); if ($rows.Count) { "Locals:"; $rows | ForEach-Object { "  $_" } } }
$d = Dialogs; if ($d) { "!! dialog: " + ($d -join ' '); Close-Dialogs }
KillAll
