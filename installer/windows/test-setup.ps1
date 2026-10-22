# test-setup.ps1 - the Setup's health, per user (MSIINSTALLPERUSER=1, no elevation), once for each
# choice: msiexec /i with IDE=Black, IDE=Grey or IDE=White, then the chosen folder holds GRIDE.exe and
# the ten programs and the other two folders are absent, the Start menu shortcut and the registry values are there,
# GRIDE.exe starts and answers and cpp11 runs; a deleted program comes back with msiexec /fa; msiexec /x
# takes the folder, the shortcut and the registry values away again.
#
#   test-setup.ps1 [-Msi <GRIDE-Setup-ver.msi>] [-Choices Black,Grey,White] [-Cpp11Version 1.25]
param([string]$Msi = "", [string[]]$Choices = @("Black", "Grey", "White"), [string]$Cpp11Version = "1.25")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Msi-Com.ps1")
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Msi) { $Msi = Get-ChildItem (Join-Path $root "installer\dist") -Filter "GRIDE-Setup-*.msi" | Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName }
$logs = Join-Path (Split-Path $Msi) "logs"; New-Item -ItemType Directory -Force $logs | Out-Null
# the package's own name, version, publisher and ProductCode, as Programs and Features shows them
$inst = New-MsiInstaller
$mdb = Open-MsiDatabase $inst $Msi 0
$prop = @{}; foreach ($r in @(Read-MsiRows $mdb "SELECT ``Property``, ``Value`` FROM ``Property``")) { $prop[$r[0]] = $r[1] }
$mdb = $null; [GC]::Collect(); [GC]::WaitForPendingFinalizers()
$regKey = "HKCU:\Software\$($prop.Manufacturer)\$($prop.ProductName)"
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
$folders = @{ Black = "Black"; Grey = "Grey"; White = "White" }
$links = @{ Black = "GRIDE.lnk"; Grey = "GRIDE.lnk"; White = "GRIDE.lnk" }
"Setup: $Msi ($($prop.ProductName), $($prop.Manufacturer), $($prop.ProductCode))"
foreach ($c in $Choices) {
  $others = @("Black", "Grey", "White" | Where-Object { $_ -ne $c })
  "== $c ($($folders[$c]))"
  $rc = Msiexec @("/i", "`"$Msi`"", "/qn", "MSIINSTALLPERUSER=1", "IDE=$c") "install-$c.log"
  Check ($rc -eq 0) "msiexec /i IDE=$c -> $rc"
  $dir = Join-Path $base $folders[$c]
  Check (Test-Path "$dir\GRIDE.exe") "$dir\GRIDE.exe"
  $missing = @("c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747" | Where-Object { -not (Test-Path "$dir\$_.exe") })
  Check ($missing.Count -eq 0) "the ten programs beside it$(if ($missing) { ' - missing: ' + ($missing -join ', ') })"
  # Black and Grey read toolchain.json, White the settings.json it ships beside it; all three install the one
  # examples folder - cppparse's .pro (the one GRIDE format) and its Visual Studio solution, the projects, the programs
  if ($c -eq "White") { Check (Test-Path "$dir\settings.json") "settings.json" } else { Check (Test-Path "$dir\toolchain.json") "toolchain.json" }
  $cp = "$dir\examples\projects\cppparse"
  Check ((Test-Path "$cp\cppparse.pro") -and (Test-Path "$cp\cppparse.sln") -and (Test-Path "$cp\cppparse\cppparse.vcxproj")) "examples\projects\cppparse (its .pro, solution and project)"
  # 28 .pro projects - the 14 of RIDE's set and cppparse's line, the showcase's 3, the classic Shalimar 11 - and
  # the 3 CCS projects; the 11 single-file programs; the one toolchain demo (2026-10-21: GRIDE1's examples in)
  Check ((Get-ChildItem "$dir\examples\projects" -Recurse -Filter *.pro -ErrorAction SilentlyContinue).Count -eq 28) "examples\projects: 28 .pro projects"
  Check ((Get-ChildItem "$dir\examples\projects\ccs" -Recurse -Filter .project -Force -ErrorAction SilentlyContinue).Count -eq 3) "examples\projects\ccs: the 3 CCS projects"
  Check ((Test-Path "$dir\examples\projects\showcase\solve3-cpp11\solve3-cpp11.pro") -and (Test-Path "$dir\examples\projects\shalimar-classic\factorial\factorial.pro")) "the showcase and the classic Shalimar set"
  Check ((Get-ChildItem "$dir\examples\programs" -File -ErrorAction SilentlyContinue).Count -eq 11) "examples\programs: 11 programs"
  Check ((Test-Path "$dir\examples\toolchain-demo\demo.ps1") -and -not (Test-Path "$dir\examples\toolchain-demo\out")) "examples\toolchain-demo, without a run's output"
  Check ((Get-ChildItem "$dir\lib" -Recurse -Filter *.lib -ErrorAction SilentlyContinue).Count -eq 8) "the 8 runtime libraries under lib\"
  Check (Test-Path "$dir\docs\GRIDE_Help.html") "the shared Help, docs\GRIDE_Help.html"
  Check (Test-Path "$dir\vcruntime140.dll") "the VC++ runtime beside GRIDE.exe"
  foreach ($o in $others) { Check (-not (Test-Path (Join-Path $base $folders[$o]))) "$($folders[$o]) not installed" }
  Check (Test-Path (Join-Path $menu $links[$c])) "Start menu: $($links[$c])"
  Check (Test-Path "$regKey\$c") "HKCU registry key"
  $name = try { [string](Get-MsiProp $inst "ProductInfo" @($prop.ProductCode, "InstalledProductName")) } catch { "" }
  $publisher = try { [string](Get-MsiProp $inst "ProductInfo" @($prop.ProductCode, "Publisher")) } catch { "" }
  Check ($name -eq $prop.ProductName -and $publisher -eq $prop.Manufacturer) "Programs and Features: '$name', publisher '$publisher'"
  $v = & "$dir\cpp11.exe" --version 2>&1 | Where-Object { "$_" -match '^Version ' } | Select-Object -First 1
  Check ($LASTEXITCODE -eq 0 -and "$v" -match "^Version $([regex]::Escape($Cpp11Version))\b") "cpp11 --version: $v"
  $p = Start-Process "$dir\GRIDE.exe" -PassThru
  $h = [IntPtr]::Zero
  for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.HasExited) { break }; $h = $p.MainWindowHandle }
  $r = [IntPtr]::Zero
  $answers = ($h -ne [IntPtr]::Zero) -and ([SetupWin]::SendMessageTimeout($h, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$r) -ne [IntPtr]::Zero)
  Check $answers "GRIDE.exe starts, shows '$($p.MainWindowTitle)' and answers"
  if (-not $p.HasExited) { [void][SetupWin]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force } }
  # **One settings file for the three**, %APPDATA%\GRIDE\settings.json. Black and Grey write their part there when
  # they close; White writes it only when a setting changes, and never at its former place again.
  $shared = Join-Path $env:APPDATA "GRIDE\settings.json"
  if ($c -eq "White") { Check (-not (Test-Path (Join-Path $env:USERPROFILE "GRIDE 1.0\settings.json"))) "no settings.json at White's former place" }
  else { Check ((Test-Path $shared) -and (Get-Item $shared).LastWriteTime -ge $p.StartTime) "$shared written by $c" }
  Remove-Item "$dir\c2s.exe" -Force
  $rc = Msiexec @("/fa", "`"$Msi`"", "/qn") "repair-$c.log"
  Check ($rc -eq 0 -and (Test-Path "$dir\c2s.exe")) "repair (/fa) puts a deleted c2s.exe back -> $rc"
  $rc = Msiexec @("/x", "`"$Msi`"", "/qn") "uninstall-$c.log"
  Check ($rc -eq 0) "msiexec /x -> $rc"
  Check (-not (Test-Path $dir)) "$dir removed"
  Check (-not (Test-Path (Join-Path $menu $links[$c]))) "the shortcut removed"
  Check (-not (Test-Path "$regKey\$c")) "the registry key removed"
}
# Each outlook takes in only its own former names, so the file is free of RIDE's once all three have run.
if (@("Black", "Grey", "White" | Where-Object { $Choices -notcontains $_ }).Count -eq 0) {
  "== settings, after the three"
  $shared = Join-Path $env:APPDATA "GRIDE\settings.json"
  $keys = if (Test-Path $shared) { [regex]::Matches((Get-Content $shared -Raw), '"([^"\\]+)"\s*:') | ForEach-Object { $_.Groups[1].Value } } else { @() }
  $ride = @($keys | Where-Object { $_ -match '(?i)(?<!g)ride' } | Sort-Object -Unique)
  Check ((Test-Path $shared) -and $ride.Count -eq 0) "$shared names no RIDE among its keys, at any depth$(if ($ride) { ': ' + ($ride -join ', ') })"
  Check (-not (Test-Path (Join-Path $env:APPDATA "GRIDE\gride.settings.json"))) "no former gride.settings.json beside it"
}
if ($script:fail -eq 0) { "all checks passed" } else { "$($script:fail) check(s) failed"; exit 1 }
