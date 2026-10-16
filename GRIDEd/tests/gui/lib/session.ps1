. "$PSScriptRoot\harness.ps1"
function Attach($name = 'GRIDE') {
  $global:P = Get-Process $name -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $P) { throw "no $name process" }
  $global:H = $P.MainWindowHandle; $global:Root = $AE::FromHandle($H)
}
function KillAll { Get-Process GRIDE,cdb,sim6747,vm6747,cdemo,cppshapes,shlmatrix,conv1,conv2 -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 400 }
# menu by mnemonic keys (no UIA): e.g. MKeys '%t','t'
function MKeys([string[]]$ks) { Focus-Gride; foreach ($k in $ks) { [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 350 } }
function Pick-Source($src) {
  $cb = SourceCombo
  $ec = $cb.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
  $ec.Expand(); Start-Sleep -Milliseconds 120
  $item = Find-Named $cb $src 'ListItem'
  if ($item) { $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select() }
  $ec.Collapse(); Start-Sleep -Milliseconds 120
}
function OutText { [W32]::Text((OutBox)) }
function Tail($t, $n = 40) { ($t -split "`r?`n" | Select-Object -Last $n) -join "`n" }
function Close-Dialogs { [Enum32]::TopOf([uint32]$P.Id) | Where-Object { $_ -ne $H } | ForEach-Object { [W32]::SendMessage($_, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null } }
function Shot-Desktop($name) {
  Start-Sleep -Milliseconds 500
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height; $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size); New-Item -ItemType Directory -Force "$S\shots" | Out-Null; $f = "$S\shots\$name.png"; $bmp.Save($f); $f
}
# Locals pane rows via Win32 ListView messages (not UIA)
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class LV {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int a, bool i, uint pid);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, int s, int t, int pr);
  [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr p, IntPtr a, int s, int t);
  [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out IntPtr w);
  [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out IntPtr r);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static string Cell(IntPtr lv, int row, int col) {
    uint pid; GetWindowThreadProcessId(lv, out pid);
    IntPtr hp = OpenProcess(0x38, false, pid);
    IntPtr mem = VirtualAllocEx(hp, IntPtr.Zero, 4096, 0x1000, 4);
    IntPtr txt = (IntPtr)((long)mem + 128);
    byte[] item = new byte[72]; // LVITEMW x64
    BitConverter.GetBytes(1).CopyTo(item, 0); // mask LVIF_TEXT
    BitConverter.GetBytes(row).CopyTo(item, 4); BitConverter.GetBytes(col).CopyTo(item, 8);
    BitConverter.GetBytes((long)txt).CopyTo(item, 24); BitConverter.GetBytes(1024).CopyTo(item, 32);
    IntPtr w; WriteProcessMemory(hp, mem, item, item.Length, out w);
    int n = (int)SendMessage(lv, 0x1073, (IntPtr)row, mem); // LVM_GETITEMTEXTW
    byte[] buf = new byte[2048]; IntPtr r; ReadProcessMemory(hp, txt, buf, buf.Length, out r);
    VirtualFreeEx(hp, mem, 0, 0x8000); CloseHandle(hp);
    return Encoding.Unicode.GetString(buf, 0, Math.Max(0, n * 2));
  }
  public static int Count(IntPtr lv) { return (int)SendMessage(lv, 0x1004, IntPtr.Zero, IntPtr.Zero); }
}
"@
function ListViews { All-Windows | Where-Object { $_.Cls -like '*SysListView32*' -and $_.Vis } }
function LV-Rows($h, $cols = 3) { $n = [LV]::Count($h); for ($i = 0; $i -lt $n; $i++) { (0..($cols-1) | ForEach-Object { [LV]::Cell($h, $i, $_) }) -join ' | ' } }
