# pane-probe.ps1 - the debugger's panes and commands beyond stepping, on one target: Call Stack, Registers,
# Watch, Memory, Disassembly, Up/Down the Stack, Set Variable, Run to Cursor. Stops in cdemo's greet().
#   powershell -ExecutionPolicy Bypass -File pane-probe.ps1 -Target Windows|TMS320C6747
param([string]$Target = 'TMS320C6747', [string]$Gride)
if ($Gride) { $global:GrideExe = $Gride }
$global:S = Join-Path $env:TEMP 'gride-gui-tests'
. "$PSScriptRoot\lib\session.ps1"
if (-not (Test-Path "$E2E\e2e.grws")) { Copy-Item -Recurse "$PSScriptRoot\projects" $E2E }

function Key($k) { Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 400 }
function DebugLog { [W32]::Text((OutBox)) }
function WaitLog($pattern, $seconds = 30) {
    $before = (DebugLog).Length; $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        Start-Sleep -Milliseconds 300; $t = DebugLog
        if ($t.Length -gt $before -and $t.Substring($before) -match $pattern) { return $Matches[0] }
        if (Dialogs) { return "[dialog] " + ((Dialogs) -join ' ') }
    }
    "[TIMEOUT $pattern]"
}
# the visible list in the bottom area after a pane is shown, its rows "a | b | c | d"
function PaneRows($pane) {
    Menu 'Debug','Windows',$pane; Start-Sleep -Milliseconds 900
    @(ListViews | ForEach-Object { LV-Rows $_.H 4 } | Where-Object { $_ -match '\S' })
}
# the visible rich-text views other than the editor and the Output pane (Memory, Disassembly)
function PaneText($pane) {
    Menu 'Debug','Windows',$pane; Start-Sleep -Milliseconds 900
    $out = OutBox
    (Rich-Windows | Where-Object { $_.Vis -and $_.H -ne $out } | ForEach-Object { [W32]::Text($_.H) }) | Where-Object { $_ -notmatch 'int greet' } | Select-Object -First 1
}

KillAll
Write-Ws 'cdemo' 'Debug' $Target '{ "cdemo/src/greet.c": [5] }'
Start-GrideWith @("$E2E\e2e.grws", "$E2E\cdemo\src\greet.c")
Get-Output 'Debug' | Out-Null
"######## $Target"
Key '{F5}'; "F5:            " + (WaitLog 'stopped at [^\r\n]*' 60)

"Call Stack:";    PaneRows 'Call Stack' | Select-Object -First 4 | ForEach-Object { "   $_" }
$regs = PaneRows 'Registers'
"Registers:      $($regs.Count) rows; " + (($regs | Select-Object -First 3) -join ' ; ')
"Memory:         " + ((PaneText 'Memory') -split "`n" | Select-Object -First 2) -join ' / '
"Disassembly:    " + ((PaneText 'Disassembly') -split "`n" | Where-Object { $_ -match '\S' } | Select-Object -First 3) -join ' / '

Menu 'Debug','Watch Expression...'; Start-Sleep -Milliseconds 500
[System.Windows.Forms.SendKeys]::SendWait('n{ENTER}'); Start-Sleep -Milliseconds 600; [System.Windows.Forms.SendKeys]::SendWait('n*2{ENTER}'); Start-Sleep -Milliseconds 900
"Watch:";         PaneRows 'Watch' | ForEach-Object { "   $_" }

Menu 'Debug','Up the Stack'; Start-Sleep -Milliseconds 900
"Up the Stack:   status '$(Status)'"
"   Locals:      " + ((PaneRows 'Locals') -join ' ; ')
Menu 'Debug','Down the Stack'; Start-Sleep -Milliseconds 900
"Down the Stack: " + ((PaneRows 'Locals') -join ' ; ')

Menu 'Debug','Set Variable...'; Start-Sleep -Milliseconds 500
[System.Windows.Forms.SendKeys]::SendWait('sum = 7{ENTER}'); Start-Sleep -Milliseconds 1200
"Set sum = 7:    " + ((PaneRows 'Locals') -join ' ; ')

Key '^g'; Start-Sleep -Milliseconds 600; [System.Windows.Forms.SendKeys]::SendWait('7{ENTER}'); Start-Sleep -Milliseconds 600
Key '^{F10}'; "Run to Cursor:  " + (WaitLog 'stopped at [^\r\n]*' 30)
"   Locals:      " + ((PaneRows 'Locals') -join ' ; ')
Key '{F5}'; "F5 to the end:  " + (WaitLog 'returned \d+' 30)
"   output:      " + (((DebugLog) -split "`r?`n" | Where-Object { $_ -match 'greet\(4\)' } | Select-Object -Last 1))
$d = Dialogs; if ($d) { "!! dialog: " + ($d -join ' '); Close-Dialogs }
KillAll
