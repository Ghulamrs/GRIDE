# g2-check.ps1 - the checks GRIDE 2.0 (the repo's "GRIDE 2.0" folder) shares with GRIDEd's suite, driven by its own
# keys (F8 start/continue, F7 step over, F6 step into, Debug > Step Out). One project per run:
#   powershell -ExecutionPolicy Bypass -File g2-check.ps1 -Pro <project.pro> -Source <file to break in> -Line <n> [-Gride <exe>]
param([Parameter(Mandatory)][string]$Pro, [Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][int]$Line,
      [string]$Gride = (Join-Path $PSScriptRoot '..\..\..\GRIDE 2.0\x64\Release\GRIDE.exe'))
$global:GrideExe = $Gride
$global:S = Join-Path $env:TEMP 'gride-gui-tests'
. "$PSScriptRoot\lib\session.ps1"

# everything GRIDE 2.0 shows in its rich-text panes (the Output pane among them), newest text included
# GRIDE 2.0's Output pane is a plain EDIT control (its editor is the RICHEDIT)
# (read while hidden too: a stop brings the Locals tab forward)
function AllText { (All-Windows | Where-Object { $_.Cls -match '\.EDIT\.' } | ForEach-Object { [W32]::Text($_.H) }) -join "`n" }
function WaitFor($pattern, $seconds = 60) {
    $before = (AllText).Length; $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) { Start-Sleep -Milliseconds 300; $t = AllText
        if ($t -match $pattern) { return ($t -split "`r?`n" | Where-Object { $_ -match $pattern } | Select-Object -Last 1) } }
    return "[TIMEOUT waiting for $pattern]"
}
function Key($k) { Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 300 }

KillAll
"PATH has cdb: " + [bool]($env:PATH -match 'Debuggers')
Start-GrideWith @($Pro, $Source)
Menu 'Build','Rebuild Project'; "rebuild:    " + (WaitFor '=+ \w+: \d+ succeeded, \d+ failed' 120)
"            " + (((AllText) -split "`r?`n" | Where-Object { $_ -match 'also compiled|error' } | Select-Object -Last 2) -join ' / ')
Key '^g'; Start-Sleep -Milliseconds 500; [System.Windows.Forms.SendKeys]::SendWait("$Line{ENTER}"); Start-Sleep -Milliseconds 500; Key '{F9}'
Key '{F8}';  "F8 start:   " + (WaitFor 'stopped at [^\r\n]*|not installed[^\r\n]*|stopped answering' 60)
$n = (AllText).Length
Key '{F6}';  Start-Sleep 2; "F6 into:    " + (((AllText).Substring([Math]::Min($n,(AllText).Length)) -split "`r?`n" | Where-Object { $_ -match 'stopped|answering' } | Select-Object -Last 1))
$n = (AllText).Length
Menu 'Debug','Step Out'; Start-Sleep 2; "Step Out:   " + (((AllText).Substring([Math]::Min($n,(AllText).Length)) -split "`r?`n" | Where-Object { $_ -match 'stopped|answering|no source' } | Select-Object -Last 1))
Key '{F8}';  "F8 end:     " + (WaitFor 'returned \d+|stopped answering' 60)
$d = Dialogs; if ($d) { "!! dialog: " + ($d -join ' '); Close-Dialogs }
KillAll
