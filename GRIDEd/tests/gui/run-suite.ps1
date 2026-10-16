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
# a debug pane shown from Debug > Windows: its list's rows, or its text view (Memory, Disassembly)
function PaneRows($pane) { Menu 'Debug','Windows',$pane; LocalsText }
function PaneText($pane) {
    Menu 'Debug','Windows',$pane; Start-Sleep -Milliseconds 800
    $out = OutBox
    (Rich-Windows | Where-Object { $_.Vis -and $_.H -ne $out } | ForEach-Object { [W32]::Text($_.H) }) -join "`n"
}
function TypeKeys($k) { Start-Sleep -Milliseconds 400; [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 900 }
$DbgKeys['Run to Cursor'] = '^{F10}'
# a step that is not a move: what to do, and the text its check reads
function Act($what) {
    $verb, $rest = $what -split ':', 2
    switch ($verb) {
        'Locals'    { return LocalsText }
        'CallStack' { return PaneRows 'Call Stack' }
        'Registers' { return PaneRows 'Registers' }
        'Memory'    { return PaneText 'Memory' }
        'Watch'     { Menu 'Debug','Watch Expression...'; TypeKeys "$rest{ENTER}"; return PaneRows 'Watch' }
        'Up'        { Menu 'Debug','Up the Stack'; Start-Sleep -Milliseconds 600; return PaneRows 'Locals' }
        'Down'      { Menu 'Debug','Down the Stack'; Start-Sleep -Milliseconds 600; return PaneRows 'Locals' }
        'Set'       { Menu 'Debug','Set Variable...'; TypeKeys "$rest{ENTER}"; return PaneRows 'Locals' }
        'GoTo'      { Focus-Gride; TypeKeys '^g'; TypeKeys "$rest{ENTER}"; return 'ok' }
        'Output'    { return Get-Output 'Debug' }
        'Convert'   {
            # Tools > Convert > C -> Shalimar (Ctrl+Alt+S): its dialog's Convert, then No to adding the file to the project
            Remove-Item ($rest) -ErrorAction SilentlyContinue
            Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait('^%s'); Start-Sleep 2
            $dlg = [Enum32]::TopOf([uint32]$P.Id) | Where-Object { [W32]::Text($_) -eq 'Convert Source' } | Select-Object -First 1
            if (-not $dlg) { return 'no Convert Source dialog' }
            $btn = [Enum32]::Children([IntPtr]$dlg) | Where-Object { [W32]::Text($_) -eq 'Convert' } | Select-Object -First 1
            [SuiteMsg]::PostMessage([IntPtr]$btn, 0xF5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep 4
            $ask = [Enum32]::TopOf([uint32]$P.Id) | Where-Object { [W32]::Text($_) -eq 'Convert' } | Select-Object -First 1
            if ($ask) { $no = [Enum32]::Children([IntPtr]$ask) | Where-Object { [W32]::Text($_) -eq '&No' } | Select-Object -First 1
                        [SuiteMsg]::PostMessage([IntPtr]$no, 0xF5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; Start-Sleep 2 }
            return (Get-Output 'Tools') + "`ntitle: " + (Get-Process -Id $P.Id).MainWindowTitle + "`nwritten: " + (Test-Path $rest)
        }
    }
    return $null
}
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SuiteMsg { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@

# ---- builds: the whole workspace, both configurations, both targets ----
foreach ($target in 'Windows', 'TMS320C6747') {
    foreach ($config in 'Debug', 'Release') {
        $name = "build $config $target"
        if ($name -notmatch $Only) { continue }
        Fresh 'cdemo' $config $target $null $null
        Menu 'Build', 'Rebuild Workspace'
        $o = Wait-Out '\d+ succeeded, \d+ failed' 240
        Check $name '4 projects built' $o '4 succeeded, 0 failed'
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
        @('Start Debugging', 'stopped at main\.cpp:9'), @('Locals', 'shapes \| \{items_ = 0x[0-9a-f]+, size_ = 1, cap_ = \d+\} \| class vector'),
        @('Step Into', 'stopped at rect\.cpp:16'), @('Step Out', 'stopped at main\.cpp:9'),
        @('Step Over', 'stopped at main\.cpp:10'), @('Start Debugging', 'returned 0')) },
    @{ Name = 'debug Shalimar Windows'; Proj = 'shlmatrix'; Target = 'Windows'; Bps = '{ "shlmatrix/src/matrix.shl": [9] }'; Open = $null; Steps = @(
        @('Start Debugging', 'stopped at matrix\.shl:9'), @('Step Into', 'stopped at linalg\.shl:\d+ in mul'),
        @('Locals', 'a \| \{\{1, 2, 0\}, \{0, 1, 3\}, \{4, 0, 1\}\} \| struct real\[\]\[\] \*'), @('Step Out', 'stopped at matrix\.shl:(9|10)'),
        @('Step Over', 'stopped at matrix\.shl:\d+'), @('Step Out', 'no source to show'), @('Start Debugging', 'returned 0')) },
    @{ Name = 'debug Shalimar C6747'; Proj = 'shlmatrix'; Target = 'TMS320C6747'; Bps = '{ "shlmatrix/src/matrix.shl": [9] }'; Open = $null; Steps = @(
        @('Start Debugging', 'stopped at matrix\.shl:9'), @('Step Into', 'stopped at linalg\.shl:\d+'),
        @('Locals', '(?s)a \| \{\{1, 2, 0\}, \{0, 1, 3\}, \{4, 0, 1\}\} \| real\[\]\[\].*i \| \S+ \| int'),
        @('Step Out', 'stopped at matrix\.shl:\d+'), @('Start Debugging', 'returned 0')) },
    # arrays, a 2-D array, structs within structs, a char array and a string pointer (phase D)
    @{ Name = 'debug C90 data C6747'; Proj = 'cdata'; Target = 'TMS320C6747'; Bps = '{ "cdata/src/data.c": [22] }'; Open = 'cdata\src\data.c'; Steps = @(
        @('Start Debugging', 'stopped at data\.c:22'),
        @('Locals', '(?s)primes \| \{2, 3, 5, 7, 11\} \|.*grid \| \{\{1, 2, 3\}, \{4, 5, 6\}\} \|.*p \| \{x = 3, y = -4\} \|.*b \| \{corner = \{\{x = 0, y = 0\}, \{x = 6, y = 2\}\}, label = "box", area = 12\.5\} \|.*name \| \(char \*\) 0x[0-9a-f]+ "cdata"'),
        @('Start Debugging', 'returned 28')) },
    # Tools > Convert: greet.c to Shalimar with c2s, the result opened, "Converted to" in the Tools output
    @{ Name = 'convert C to Shalimar'; Proj = 'cdemo'; Target = 'Windows'; Bps = $null; Open = 'cdemo\src\greet.c'; Steps = @(
        ,@("Convert:$E2E\cdemo\src\greet.shl", '(?s)Converted to .*greet\.shl.*title: greet\.shl - .*written: True')) },   # the comma: one step, not flattened
    @{ Name = 'debug C90 data Windows'; Proj = 'cdata'; Target = 'Windows'; Bps = '{ "cdata/src/data.c": [22] }'; Open = 'cdata\src\data.c'; Steps = @(
        @('Start Debugging', 'stopped at data\.c:22'), @('Locals', '(?s)primes \|.*total \| 28 \|'), @('Start Debugging', 'returned 28')) }
)
# ---- the other debugger panes and commands, stopped in greet(): Call Stack, Registers, Memory, Watch,
#      Up/Down the Stack, Set Variable (checked by what the program then prints), Run to Cursor ----
foreach ($target in 'Windows', 'TMS320C6747') {
    $debug += @{ Name = "panes C90 $target"; Proj = 'cdemo'; Target = $target; Bps = '{ "cdemo/src/greet.c": [5] }'; Open = 'cdemo\src\greet.c'; Steps = @(
        @('Start Debugging', 'stopped at greet\.c:5'),
        @('CallStack', '(?s)greet \| greet\.c \| 5.*main \| main\.c \| 8'),
        @('Registers', '(?m)^(A0|rax) \| '),
        @('Memory', '(?m)^[0-9A-F]{8,16}  04 00 00 00 '),
        @('Watch:n*2', 'n\*2 \| 8 \|'),
        @('Up', '(?s)n \| 4 \|.*g \| '),
        @('Down', 'sum \| 0 \|'),
        @('Set:sum = 7', 'sum \| 7 \|'),
        @('GoTo:7', 'ok'),
        @('Run to Cursor', 'stopped at greet\.c:7'),
        @('Locals', 'sum \| 17 \|'),
        @('Start Debugging', 'returned 3'),
        @('Output', 'greet\(4\) is 17')) }
}
foreach ($d in $debug) {
    if ($d.Name -notmatch $Only) { continue }
    Fresh $d.Proj 'Debug' $d.Target $d.Bps $d.Open
    Get-Output 'Debug' | Out-Null
    foreach ($s in $d.Steps) {
        $acted = Act $s[0]
        if ($null -ne $acted) { Check $d.Name ($s[0] -replace ':.*$', '') $acted $s[1]; continue }
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
