# test-setup.ps1 - the Setup's health with its own defaults (per user, no elevation), once for each
# choice: Black, Grey, White on its own, and Black with White beside it (IDE=Black WHITE=1). After msiexec /i
# each chosen outlook's folder holds its IDE and the ten programs, the outlooks not chosen are absent, the
# Start menu shortcuts and registry values are there, one Installed apps entry names GRIDE, each IDE starts
# and answers and cpp11 runs; a deleted program comes back with msiexec /fa; msiexec /x takes every folder,
# shortcut, registry value and the Installed apps entry away again.
#
#   test-setup.ps1 [-Msi <GRIDE-Setup-ver.msi>] [-Choices Black,Grey,White,Black+White]
param([string]$Msi = "", [string[]]$Choices = @("Black", "Grey", "White", "Black+White"))
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Msi) { $Msi = Get-ChildItem (Join-Path $root "installer\dist") -Filter "GRIDE-Setup-*.msi" | Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName }
$logs = Join-Path (Split-Path $Msi) "logs"; New-Item -ItemType Directory -Force $logs | Out-Null
# the product's name as the package gives it (GRIDE <version>), which names its registry key and Installed apps entry
$wi = New-Object -ComObject WindowsInstaller.Installer; $pdb = $wi.OpenDatabase($Msi, 0); $pv = $pdb.OpenView("SELECT Value FROM Property WHERE Property='ProductName'"); $pv.Execute(); $productName = $pv.Fetch().StringData(1); $pv = $null; $pdb = $null
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SetupWin {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$script:fail = 0
function InstalledApps { Get-Package -ProviderName msi -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "GRIDE *" } | ForEach-Object { "$($_.Name) ($($_.Version))" } }   # Windows Installer's products, per-user ones included: what Settings > Installed apps lists
function Check([bool]$ok, [string]$what) { if ($ok) { "  ok    $what" } else { "  FAIL  $what"; $script:fail++ } }
function Msiexec([string[]]$a, [string]$log) {
  $p = Start-Process msiexec.exe -ArgumentList ($a + @("/l*v", "`"$logs\$log`"")) -Wait -PassThru
  $p.ExitCode
}
# a program's window comes up and answers a message within 5 s; then it is closed
function Starts([string]$exe) {
  $p = Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
  $h = [IntPtr]::Zero
  for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.HasExited) { break }; $h = $p.MainWindowHandle }
  $r = [IntPtr]::Zero
  $answers = ($h -ne [IntPtr]::Zero) -and ([SetupWin]::SendMessageTimeout($h, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$r) -ne [IntPtr]::Zero)
  $title = $p.MainWindowTitle
  if (-not $p.HasExited) { [void][SetupWin]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force } }
  return @($answers, $title)
}
$base = Join-Path $env:LOCALAPPDATA "Programs\GRIDE"
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\GRIDE"
$tools = @("c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747")
# where each outlook keeps its programs, its IDE and its Start menu entry
$outlook = @{
  Black = @{ Bin = "Black";     Ide = "GRIDE.exe"; Link = "GRIDE.lnk" }
  Grey  = @{ Bin = "Grey";      Ide = "GRIDE.exe"; Link = "GRIDE.lnk" }
  White = @{ Bin = "White\bin"; Ide = "RIDE.exe";  Link = "RIDE - The Simplest.lnk" }
}
"Setup: $Msi ($productName)"
foreach ($c in $Choices) {
  $chosen = @($c -split '\+')
  $props = @("IDE=$($chosen[0])"); if ($chosen.Count -gt 1 -and $chosen[1] -eq "White") { $props += "WHITE=1" }
  "== $c ($($props -join ' '))"
  # the Setup's own defaults, as a double-click takes them (per user, no elevation)
  $rc = Msiexec (@("/i", "`"$Msi`"", "/qn") + $props) "install-$($c -replace '\+','-').log"
  Check ($rc -eq 0) "msiexec /i $($props -join ' ') -> $rc"
  foreach ($o in $chosen) {
    $bin = Join-Path $base $outlook[$o].Bin
    Check (Test-Path "$bin\$($outlook[$o].Ide)") "$o`: $bin\$($outlook[$o].Ide)"
    $missing = @($tools | Where-Object { -not (Test-Path "$bin\$_.exe") })
    Check ($missing.Count -eq 0) "$o`: the ten programs beside it$(if ($missing) { ' - missing: ' + ($missing -join ', ') })"
    Check (Test-Path "$bin\vcruntime140.dll") "$o`: the VC++ runtime beside $($outlook[$o].Ide)"
    if ($o -eq "White") {
      $w = Join-Path $base "White"
      Check (Test-Path "$bin\RIDEConsole.exe") "White: RIDEConsole.exe"
      Check (Test-Path "$w\settings.json") "White: settings.json (its installed layout)"
      Check ((Get-ChildItem "$bin\lib" -Filter *.lib -ErrorAction SilentlyContinue).Count -ge 1 -and (Test-Path "$bin\lib\rts6x-tms6747")) "White: bin\lib (Shalimar runtimes, rts6x-tms6747)"
      Check (@(Get-ChildItem "$w\programs" -Recurse -File -ErrorAction SilentlyContinue).Count -ge 1 -and @(Get-ChildItem "$w\projects" -Recurse -File -ErrorAction SilentlyContinue).Count -ge 1) "White: programs\ and projects\ (RIDE's own examples)"
      Check (@(Get-ChildItem "$w\help" -Recurse -File -ErrorAction SilentlyContinue).Count -ge 1 -and (Test-Path "$w\include")) "White: help\ and include\"
    } else {
      Check (Test-Path "$bin\toolchain.json") "$o`: toolchain.json"
      Check ((Get-ChildItem "$bin\lib" -Recurse -Filter *.lib -ErrorAction SilentlyContinue).Count -eq 8) "$o`: the 8 runtime libraries under lib\"
      Check ((Test-Path "$bin\examples\cppparse\cppparse.sln") -and (Test-Path "$bin\examples\cppparse\cppparse\cppparse.vcxproj")) "$o`: examples\cppparse"
      $shl = @(Get-ChildItem "$bin\examples\shalimar" -Recurse -Filter *.shl -ErrorAction SilentlyContinue)
      Check ($shl.Count -eq 20 -and (Test-Path "$bin\examples\shalimar\classic\factorial\factorial.pro")) "$o`: examples\shalimar (20 Shalimar sources)"
      Check ((Test-Path "$bin\examples\showcase\README.md") -and (Test-Path "$bin\examples\showcase\solve3-shalimar\solve3.shl") -and (Test-Path "$bin\examples\showcase\average-c90\src\rng.c") -and (Test-Path "$bin\examples\showcase\solve3-cpp11\src\CMatrix.h")) "$o`: examples\showcase"
    }
    Check (Test-Path (Join-Path $menu $outlook[$o].Link)) "$o`: Start menu $($outlook[$o].Link)"
    Check (Test-Path "HKCU:\Software\G. R. Akhtar\$productName\$o") "$o`: HKCU registry key"
    $v = & "$bin\cpp11.exe" --version 2>&1 | Select-Object -First 1
    Check ($LASTEXITCODE -eq 0 -and "$v" -match "cpp11|C\+\+") "$o`: cpp11 --version: $v"
    $s = Starts "$bin\$($outlook[$o].Ide)"
    Check $s[0] "$o`: $($outlook[$o].Ide) starts, shows '$($s[1])' and answers"
  }
  foreach ($o in @("Black", "Grey", "White" | Where-Object { $chosen -notcontains $_ })) {
    Check (-not (Test-Path (Join-Path $base $o))) "$o not installed"
  }
  Check (@(InstalledApps).Count -eq 1) "Installed apps: $(@(InstalledApps) -join ', ')"
  $first = Join-Path $base $outlook[$chosen[0]].Bin
  Remove-Item "$first\c2s.exe" -Force
  $rc = Msiexec @("/fa", "`"$Msi`"", "/qn") "repair-$($c -replace '\+','-').log"
  Check ($rc -eq 0 -and (Test-Path "$first\c2s.exe")) "repair (/fa) puts a deleted c2s.exe back -> $rc"
  $rc = Msiexec @("/x", "`"$Msi`"", "/qn") "uninstall-$($c -replace '\+','-').log"
  Check ($rc -eq 0) "msiexec /x -> $rc"
  foreach ($o in $chosen) {
    Check (-not (Test-Path (Join-Path $base $o))) "$o removed"
    Check (-not (Test-Path (Join-Path $menu $outlook[$o].Link))) "$o`: the shortcut removed"
    Check (-not (Test-Path "HKCU:\Software\G. R. Akhtar\$productName\$o")) "$o`: the registry key removed"
  }
  Check (@(InstalledApps).Count -eq 0) "Installed apps entry removed"
}
if ($script:fail -eq 0) { "all checks passed" } else { "$($script:fail) check(s) failed"; exit 1 }
