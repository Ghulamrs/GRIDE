# Helpers to drive GRIDE: launch, menus via UIA, keys via SendKeys, pane text via WM_GETTEXT.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, int f, IntPtr x);
  public static string Text(IntPtr h) {
    int n = (int)SendMessage(h, 0x000E, IntPtr.Zero, IntPtr.Zero);
    var sb = new StringBuilder(n + 2); SendMessage(h, 0x000D, (IntPtr)(n + 1), sb); return sb.ToString();
  }
}
"@
$global:AE = [System.Windows.Automation.AutomationElement]
$global:TS = [System.Windows.Automation.TreeScope]
$global:CT = [System.Windows.Automation.Condition]::TrueCondition
if (-not $global:GrideExe) { $global:GrideExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\x64\Release\GRIDE.exe')) }

function Start-Gride($arg) {
  Get-Process GRIDE -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  $global:P = Start-Process $GrideExe -ArgumentList "`"$arg`"" -PassThru
  for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $global:P.Refresh(); if ($global:P.MainWindowHandle -ne 0) { break } }
  Start-Sleep -Seconds 4
  $global:H = $global:P.MainWindowHandle
  $global:Root = $AE::FromHandle($global:H)
}
function Focus-Gride { [W32]::ShowWindow($H, 9) | Out-Null; [W32]::SetForegroundWindow($H) | Out-Null; Start-Sleep -Milliseconds 300 }
function Keys($k) { Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 400 }

function Find-Named($el, $name, $ctl) {
  $c = New-Object System.Windows.Automation.PropertyCondition($AE::NameProperty, $name)
  $all = $el.FindAll($TS::Descendants, $c)
  foreach ($x in $all) { if (-not $ctl -or $x.Current.ControlType.ProgrammaticName -like "*$ctl") { return $x } }
  return $null
}
# Menu path like "Build","Build Solution": expand each, invoke the last
function Menu([string[]]$path) {
  Focus-Gride
  $cur = $Root
  for ($i = 0; $i -lt $path.Count; $i++) {
    $item = $null
    for ($t = 0; $t -lt 20 -and -not $item; $t++) {
      $item = Find-Named $AE::RootElement $path[$i] 'MenuItem'
      if (-not $item) { $item = Find-Named $Root $path[$i] 'MenuItem' }
      if (-not $item) { Start-Sleep -Milliseconds 200 }
    }
    if (-not $item) { throw "menu item not found: $($path -join ' > ') at '$($path[$i])'" }
    if ($i -lt $path.Count - 1) {
      try { $item.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand() }
      catch { $item.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
      Start-Sleep -Milliseconds 400
    } else {
      $item.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    }
  }
  Start-Sleep -Milliseconds 500
}
function Dump($el, $depth, $max) {
  if ($depth -gt $max) { return }
  $kids = $el.FindAll($TS::Children, $CT)
  foreach ($k in $kids) {
    $c = $k.Current
    $t = ''
    if ($c.ControlType.ProgrammaticName -match 'Edit|Document|Pane') { try { $t = [W32]::Text([IntPtr]$c.NativeWindowHandle); if ($t.Length -gt 60) { $t = $t.Substring(0, 60) + '...' } } catch {} }
    ('  ' * $depth) + "$($c.ControlType.ProgrammaticName) '$($c.Name)' auto='$($c.AutomationId)' cls='$($c.ClassName)' hwnd=$($c.NativeWindowHandle) $($t -replace "`r?`n",' | ')"
    Dump $k ($depth + 1) $max
  }
}
