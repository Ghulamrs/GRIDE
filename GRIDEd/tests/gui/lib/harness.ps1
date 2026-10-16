# Scenario harness for GRIDE end-to-end tests.
if (-not $global:S) { $global:S = Join-Path $env:TEMP 'gride-gui-tests' }   # working copy of the projects, logs
. "$PSScriptRoot\uia.ps1"; . "$PSScriptRoot\win.ps1"
$global:E2E = "$S\e2e"
$global:LogDir = "$S\runs"; New-Item -ItemType Directory -Force $LogDir | Out-Null

function Write-Ws($startup, $config, $target, $bps) {
  $b = if ($bps) { $bps } else { '{}' }
  $json = @"
{
  "gride": "1.0", "type": "workspace", "name": "e2e",
  "projects": [ {"path": "cdemo/cdemo.pro"}, {"path": "cppshapes/cppshapes.pro"}, {"path": "shlmatrix/shlmatrix.pro"} ],
  "startup": "$startup", "activeConfig": "$config", "activeTarget": "$target",
  "breakpoints": $b
}
"@
  [IO.File]::WriteAllText("$E2E\e2e.grws", $json)
}

function OutBox {
  # the Output pane's RichEdit: sibling of the ToolStrip that holds the Build/Debug/Tools combo
  $cb = All-Windows | Where-Object { $_.Cls -like '*COMBOBOX*' -and $_.Text -in 'Build','Debug','Tools' } | Select-Object -First 1
  if (-not $cb) { return [IntPtr]::Zero }
  $pane = [Enum32]::GetParent([Enum32]::GetParent($cb.H))
  $r = Rich-Windows | Where-Object { [Enum32]::GetParent($_.H) -eq $pane } | Select-Object -First 1
  if ($r) { return $r.H } else { return [IntPtr]::Zero }
}
function Editor-Text { $r = Rich-Windows | Where-Object { $_.Vis -and $_.H -ne (OutBox) } | Select-Object -First 1; if ($r) { $r.Text } }
function SourceCombo {
  $h = (All-Windows | Where-Object { $_.Cls -like '*COMBOBOX*' -and $_.Text -in 'Build','Debug','Tools' } | Select-Object -First 1).H
  $AE::FromHandle($h)
}
function Get-Output($src) {
  $cb = SourceCombo
  $ec = $cb.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
  $ec.Expand(); Start-Sleep -Milliseconds 120
  $item = Find-Named $cb $src 'ListItem'
  if ($item) { $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select() }
  $ec.Collapse(); Start-Sleep -Milliseconds 120
  [W32]::Text((OutBox))
}
function Status { $sb = $Root.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, [System.Windows.Automation.ControlType]::StatusBar))); ($sb.FindAll($TS::Children, $CT) | ForEach-Object { $_.Current.Name }) -join ' | ' }

# wait until the visible output contains $pattern (regex) or stops changing for $quiet polls
function Wait-Out($pattern, $timeoutS = 180, $quiet = 4) {
  $last = ''; $same = 0
  for ($i = 0; $i -lt $timeoutS * 4; $i++) {
    Start-Sleep -Milliseconds 250
    if ($P.HasExited) { return "[GRIDE EXITED]" }
    $d = $null; if (-not [Enum32]::IsWindowEnabled($H)) { $d = Dialogs }
    if ($d) { return "[DIALOG OPEN]`n" + ($d -join "`n") + "`n" + [W32]::Text((OutBox)) }
    $t = [W32]::Text((OutBox))
    if ($pattern -and $t -match $pattern) { Start-Sleep 1; return [W32]::Text((OutBox)) }
    if ($t -eq $last) { $same++ } else { $same = 0; $last = $t }
    if (-not $pattern -and $same -ge $quiet -and $t.Length -gt 0) { return $t }
  }
  return "[TIMEOUT waiting for '$pattern']`n" + [W32]::Text((OutBox))
}
# Any extra top-level windows of GRIDE (dialogs / message boxes): title + all child text
function Dialogs {
  [Enum32]::TopOf([uint32]$P.Id) | Where-Object { $_ -ne $H -and [W32]::Text($_) } | ForEach-Object {
    $d = $_
    "<<dialog '$([W32]::Text($d))'>>"
    [Enum32]::Children($d) | ForEach-Object { $t = [W32]::Text($_); if ($t) { "  [$([Enum32]::Cls($_) -replace 'WindowsForms10\.|\.app\.0.*','')] $t" } }
  }
}
function Pane-Items($title) {
  # rows of the ListViews in the GRIDE window: name of each list item and its sub-items
  $lists = $Root.FindAll($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, [System.Windows.Automation.ControlType]::List)))
  foreach ($l in $lists) {
    $rows = $l.FindAll($TS::Children, $CT)
    if ($rows.Count -eq 0) { continue }
    "<<list hwnd=$($l.Current.NativeWindowHandle) rows=$($rows.Count)>>"
    foreach ($r in $rows) { "  " + (($r.FindAll($TS::Children, $CT) | ForEach-Object { $_.Current.Name }) -join ' | ') }
  }
}
function Log($name, $text) { $f = "$LogDir\$name.txt"; [IO.File]::AppendAllText($f, $text + "`r`n"); }

function Start-GrideWith([string[]]$paths) {
  Get-Process GRIDE -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 500
  $global:P = Start-Process $GrideExe -ArgumentList (($paths | ForEach-Object { "`"$_`"" }) -join ' ') -PassThru
  for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $global:P.Refresh(); if ($global:P.MainWindowHandle -ne 0) { break } }
  $global:H = $global:P.MainWindowHandle; $global:Root = $AE::FromHandle($global:H)
  # ready once the Output pane exists, instead of a fixed 4 s
  for ($i = 0; $i -lt 40 -and (OutBox) -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 200 }
}
# the selected node of the Solution Explorer tree and the open editor tabs (window title names the active doc)
function Tree-Selected {
  $tv = All-Windows | Where-Object { $_.Cls -like '*SysTreeView32*' -and $_.Vis } | Select-Object -First 1
  if (-not $tv) { return '(no tree)' }
  $t = $AE::FromHandle($tv.H)
  $sel = $t.FindAll($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.SelectionItemPattern]::IsSelectedProperty, $true)))
  ($sel | ForEach-Object { $_.Current.Name }) -join ', '
}
function Snap($label) { "[$label] title='$((Get-Process -Id $P.Id).MainWindowTitle)' explorer-selected='$(Tree-Selected)'" }
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public struct RECT { public int L, T, R, B; }
public static class WR { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); }
"@
function Shot($name) {
  Focus-Gride; Start-Sleep -Milliseconds 500
  $r = New-Object RECT; [WR]::GetWindowRect($H, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T); $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $f = "$S\shots\$name.png"; New-Item -ItemType Directory -Force "$S\shots" | Out-Null; $bmp.Save($f); $f
}

# one debug step: its shortcut key (no menu search), then poll the already-chosen Debug log every 0.3 s
$global:DbgKeys = @{ 'Start Debugging' = '{F5}'; 'Step Over' = '{F10}'; 'Step Into' = '{F11}'; 'Step Out' = '+{F11}' }
function Dbg-Step($item, $label) {
  $ob = OutBox; $before = [W32]::Text($ob).Length; $sw = [Diagnostics.Stopwatch]::StartNew()
  if ($DbgKeys.ContainsKey($item)) { Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait($DbgKeys[$item]) } else { Menu 'Debug',$item }
  $t = ''
  while ($sw.Elapsed.TotalSeconds -lt 90) { Start-Sleep -Milliseconds 300; if ($P.HasExited) { return "GRIDE EXITED" }
    $t = [W32]::Text($ob); if ($t.Length -gt $before -and $t.Substring($before) -match '(stopped at|ran to the end|stopped answering|no source)') { break } }
  "===== $label  ({0:N1} s)" -f $sw.Elapsed.TotalSeconds
  ($t.Substring([Math]::Min($before,$t.Length)) -split "`n" | Where-Object { $_ -match 'stopped|ran to the end|greet\(|demo done|error' }) -join "`n" }
