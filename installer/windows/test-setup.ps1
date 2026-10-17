# test-setup.ps1 - the Setup's health, per user (MSIINSTALLPERUSER=1, no elevation), once for each
# choice: msiexec /i with IDE=Black or IDE=Grey, then the chosen folder holds GRIDE.exe and the ten
# programs and the other folder is absent, the Start menu shortcut and the registry values are there,
# GRIDE.exe starts and answers and cpp11 runs; a deleted program comes back with msiexec /fa; msiexec /x
# takes the folder, the shortcut and the registry values away again.
#
#   test-setup.ps1 [-Msi <GRIDEx2-Setup-ver.msi>] [-Choices Black,Grey]
param([string]$Msi = "", [string[]]$Choices = @("Black", "Grey"))
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Msi) { $Msi = Get-ChildItem (Join-Path $root "installer\dist") -Filter "GRIDE-Setup-*.msi" | Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName }
$logs = Join-Path (Split-Path $Msi) "logs"; New-Item -ItemType Directory -Force $logs | Out-Null
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SetupWin {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$script:fail = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "  ok    $what" } else { "  FAIL  $what"; $script:fail++ } }
function Msiexec([string[]]$a, [string]$log) {
  $p = Start-Process msiexec.exe -ArgumentList ($a + @("/l*v", "`"$logs\$log`"")) -Wait -PassThru
  $p.ExitCode
}
$base = Join-Path $env:LOCALAPPDATA "Programs\GRIDE"
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\GRIDE"
$folders = @{ Black = "Black"; Grey = "Grey" }
$links = @{ Black = "GRIDE.lnk"; Grey = "GRIDE.lnk" }
"Setup: $Msi"
foreach ($c in $Choices) {
  $other = if ($c -eq "Black") { "Grey" } else { "Black" }
  "== $c ($($folders[$c]))"
  $rc = Msiexec @("/i", "`"$Msi`"", "/qn", "MSIINSTALLPERUSER=1", "IDE=$c") "install-$c.log"
  Check ($rc -eq 0) "msiexec /i IDE=$c -> $rc"
  $dir = Join-Path $base $folders[$c]
  Check (Test-Path "$dir\GRIDE.exe") "$dir\GRIDE.exe"
  $missing = @("c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747" | Where-Object { -not (Test-Path "$dir\$_.exe") })
  Check ($missing.Count -eq 0) "the ten programs beside it$(if ($missing) { ' - missing: ' + ($missing -join ', ') })"
  Check (Test-Path "$dir\toolchain.json") "toolchain.json"
  Check ((Get-ChildItem "$dir\lib" -Recurse -Filter *.lib -ErrorAction SilentlyContinue).Count -eq 8) "the 8 runtime libraries under lib\"
  Check ((Test-Path "$dir\examples\cppparse\cppparse.sln") -and (Test-Path "$dir\examples\cppparse\cppparse\cppparse.vcxproj")) "examples\cppparse (the solution and its project)"
  $shl = @(Get-ChildItem "$dir\examples\shalimar" -Recurse -Filter *.shl -ErrorAction SilentlyContinue)
  Check ($shl.Count -eq 20 -and (Test-Path "$dir\examples\shalimar\classic\factorial\factorial.pro")) "examples\shalimar (20 Shalimar sources, the 7 RIDE examples and the 11 classic programs)"
  Check ((Test-Path "$dir\examples\showcase\README.md") -and (Test-Path "$dir\examples\showcase\solve3-shalimar\solve3.shl") -and (Test-Path "$dir\examples\showcase\average-c90\src\rng.c") -and (Test-Path "$dir\examples\showcase\solve3-cpp11\src\Matrix.cpp")) "examples\showcase (solve3-shalimar, average-c90, solve3-cpp11)"
  Check (Test-Path "$dir\vcruntime140.dll") "the VC++ runtime beside GRIDE.exe"
  Check (-not (Test-Path (Join-Path $base $folders[$other]))) "$($folders[$other]) not installed"
  Check (Test-Path (Join-Path $menu $links[$c])) "Start menu: $($links[$c])"
  Check (Test-Path "HKCU:\Software\G. R. Akhtar\GRIDE 1.0.0\$c") "HKCU registry key"
  $v = & "$dir\cpp11.exe" --version 2>&1 | Select-Object -First 1
  Check ($LASTEXITCODE -eq 0 -and "$v" -match "cpp11|C\+\+") "cpp11 --version: $v"
  $p = Start-Process "$dir\GRIDE.exe" -PassThru
  $h = [IntPtr]::Zero
  for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.HasExited) { break }; $h = $p.MainWindowHandle }
  $r = [IntPtr]::Zero
  $answers = ($h -ne [IntPtr]::Zero) -and ([SetupWin]::SendMessageTimeout($h, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$r) -ne [IntPtr]::Zero)
  Check $answers "GRIDE.exe starts, shows '$($p.MainWindowTitle)' and answers"
  if (-not $p.HasExited) { [void][SetupWin]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force } }
  Remove-Item "$dir\c2s.exe" -Force
  $rc = Msiexec @("/fa", "`"$Msi`"", "/qn") "repair-$c.log"
  Check ($rc -eq 0 -and (Test-Path "$dir\c2s.exe")) "repair (/fa) puts a deleted c2s.exe back -> $rc"
  $rc = Msiexec @("/x", "`"$Msi`"", "/qn") "uninstall-$c.log"
  Check ($rc -eq 0) "msiexec /x -> $rc"
  Check (-not (Test-Path $dir)) "$dir removed"
  Check (-not (Test-Path (Join-Path $menu $links[$c]))) "the shortcut removed"
  Check (-not (Test-Path "HKCU:\Software\G. R. Akhtar\GRIDE 1.0.0\$c")) "the registry key removed"
}
if ($script:fail -eq 0) { "all checks passed" } else { "$($script:fail) check(s) failed"; exit 1 }
