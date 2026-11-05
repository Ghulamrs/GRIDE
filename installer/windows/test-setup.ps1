# test-setup.ps1 - the Setup's health, per user (MSIINSTALLPERUSER=1, no elevation), once for each
# choice: msiexec /i with IDE=Black, IDE=Grey or IDE=White, then the chosen folder holds GRIDE.exe and
# the ten programs and the other two folders are absent, the Start menu shortcut and the registry values are there,
# GRIDE.exe starts and answers and cpp11 runs; the tree of headers and libraries (docs\Runtime_Design.md 5.3) is
# there folder by folder; GRIDE's runtime for Windows is in lib\x86_64-windows and a C program
# built and linked with it alone runs, and the programs need no VC++ runtime DLL; a deleted program comes back
# with msiexec /fa; msiexec /x takes the folder, the shortcut and the registry values away again.
#
#   test-setup.ps1 [-Msi <GRIDE-Setup-ver.msi>] [-Choices Black,Grey,White] [-Cpp11Version 1.46]
# Without -Cpp11Version the version expected is the one the sources say (C++Optimize\src\Version.h's CXX1_VERSION).
param([string]$Msi = "", [string[]]$Choices = @("Black", "Grey", "White"), [string]$Cpp11Version = "")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Msi-Com.ps1")
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Cpp11Version) {
  $m = Select-String -Path (Join-Path $root "toolchain\Compilers\C++Optimize\src\Version.h") -Pattern '#define\s+CXX1_VERSION\s+"([^"]+)"' | Select-Object -First 1
  if (-not $m) { throw "no CXX1_VERSION in C++Optimize\src\Version.h - pass -Cpp11Version" }
  $Cpp11Version = $m.Matches[0].Groups[1].Value
}
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
# the DLLs a program imports, read from its PE import directory
function Get-Imports([string]$exe) {
  $b = [IO.File]::ReadAllBytes($exe)
  $pe = [BitConverter]::ToInt32($b, 0x3c); $nsec = [BitConverter]::ToUInt16($b, $pe + 6)
  $optSize = [BitConverter]::ToUInt16($b, $pe + 20); $opt = $pe + 24
  $imp = [BitConverter]::ToUInt32($b, $opt + 120)
  $secs = @(for ($i = 0; $i -lt $nsec; $i++) { $s = $opt + $optSize + 40 * $i
    , @([BitConverter]::ToUInt32($b, $s + 12), [Math]::Max([BitConverter]::ToUInt32($b, $s + 8), [BitConverter]::ToUInt32($b, $s + 16)), [BitConverter]::ToUInt32($b, $s + 20)) })
  $off = { param($rva) foreach ($s in $secs) { if ($rva -ge $s[0] -and $rva -lt $s[0] + $s[1]) { return [int]($rva - $s[0] + $s[2]) } } }
  $p = & $off $imp
  while (($n = [BitConverter]::ToUInt32($b, $p + 12)) -ne 0) {
    $o = & $off $n; $e = $o; while ($b[$e] -ne 0) { $e++ }
    [Text.Encoding]::ASCII.GetString($b, $o, $e - $o)
    $p += 20
  }
}
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
  $missing = @("c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747", "gride-dbg" | Where-Object { -not (Test-Path "$dir\$_.exe") })
  Check ($missing.Count -eq 0) "the ten programs and gride-dbg beside it$(if ($missing) { ' - missing: ' + ($missing -join ', ') })"
  # Black and Grey read toolchain.json, White the settings.json it ships beside it; all three install the one
  # examples folder - cppparse's .pro (the one GRIDE format) and its Visual Studio solution, the projects, the programs
  if ($c -eq "White") { Check (Test-Path "$dir\settings.json") "settings.json" } else { Check (Test-Path "$dir\toolchain.json") "toolchain.json" }
  $cp = "$dir\examples\projects\cppparse"
  Check ((Test-Path "$cp\cppparse.pro") -and (Test-Path "$cp\cppparse.sln") -and (Test-Path "$cp\cppparse\cppparse.vcxproj")) "examples\projects\cppparse (its .pro, solution and project)"
  # 29 .pro projects - the 14 of RIDE's set and cppparse's line, cpp-sampleext (2026-10-27), the showcase's 3, the
  # classic Shalimar 11 - and the 3 CCS projects; the 11 single-file programs; the one toolchain demo
  Check ((Get-ChildItem "$dir\examples\projects" -Recurse -Filter *.pro -ErrorAction SilentlyContinue).Count -eq 29) "examples\projects: 29 .pro projects"
  Check ((Get-ChildItem "$dir\examples\projects\ccs" -Recurse -Filter .project -Force -ErrorAction SilentlyContinue).Count -eq 3) "examples\projects\ccs: the 3 CCS projects"
  Check ((Test-Path "$dir\examples\projects\showcase\solve3-cpp11\solve3-cpp11.pro") -and (Test-Path "$dir\examples\projects\shalimar-classic\factorial\factorial.pro")) "the showcase and the classic Shalimar set"
  Check ((Get-ChildItem "$dir\examples\programs" -File -ErrorAction SilentlyContinue).Count -eq 11) "examples\programs: 11 programs"
  Check ((Test-Path "$dir\examples\toolchain-demo\demo.ps1") -and -not (Test-Path "$dir\examples\toolchain-demo\out")) "examples\toolchain-demo, without a run's output"
  # GRIDE adds its runtime for Windows to the 8: five layered libraries, their kernel32.lib and the Shalimar
  # runtime over them - in the three outlooks since 2026-10-26 (Black's alone before) - and since 2026-10-30
  # the five layers' Debug variants a Debug link takes (grtstartd ... grtwind)
  $libs = 21
  Check ((Get-ChildItem "$dir\lib" -Recurse -Filter *.lib -ErrorAction SilentlyContinue).Count -eq $libs) "the $libs runtime libraries under lib\"
  $win = "$dir\lib\x86_64-windows"
  $grt = @("grtstart.lib", "grtcompat.lib", "grtcxx.lib", "grtc.lib", "grtwin.lib", "grtstartd.lib", "grtcompatd.lib", "grtcxxd.lib", "grtcd.lib", "grtwind.lib",
           "kernel32.lib", "shmrt.lib", "shmrtd.lib", "msvc\shmrt.lib", "msvc\shmrtd.lib" | Where-Object { -not (Test-Path "$win\$_") })
  Check ($grt.Count -eq 0) "lib\x86_64-windows: GRIDE's runtime (grtstart, grtcompat, grtcxx, grtc, grtwin and their Debug variants, kernel32.lib) and the Shalimar runtime over it and over Microsoft's (msvc\)$(if ($grt) { ' - missing: ' + ($grt -join ', ') })"
  $c6x = @("rts6x.lib", "rts6xd.lib", "printf6x.lib", "printf6xd.lib", "shmrt6x.lib", "shmrt6xd.lib", "shmrt\Runtime.s", "shmrt-debug\Debug.s" | Where-Object { -not (Test-Path "$dir\lib\tms6747\$_") })
  Check ($c6x.Count -eq 0) "lib\tms6747: RTS6x, printf6x and the Shalimar runtime, with its assembly in shmrt and shmrt-debug$(if ($c6x) { ' - missing: ' + ($c6x -join ', ') })"
  # the headers: one C set for c90 and cpp11 (include\c, with sys\), cpp11's C++ headers (include\c++), Windows' and
  # POSIX's (include\win) - and, for one release, the earlier lib\*.h and include\* an earlier installation's tools read
  $hc = @(Get-ChildItem "$dir\include\c" -Filter *.h -ErrorAction SilentlyContinue).Count
  $hs = @(Get-ChildItem "$dir\include\c\sys" -Filter *.h -ErrorAction SilentlyContinue).Count
  $hx = @(Get-ChildItem "$dir\include\c++" -File -ErrorAction SilentlyContinue).Count
  $hw = @(Get-ChildItem "$dir\include\win" -Filter *.h -ErrorAction SilentlyContinue).Count
  Check ($hc -eq 23 -and $hs -eq 2 -and $hx -ge 80 -and $hw -eq 4 -and (Test-Path "$dir\include\c++\cstddef")) "include\c: $hc C headers and sys\: $hs; include\c++: $hx; include\win: $hw"
  Check ((Test-Path "$dir\lib\stddef.h") -and (Test-Path "$dir\include\cstddef") -and (Test-Path "$dir\include\stddef.h")) "the earlier header folders, for one release: lib\*.h and include\*"
  Check (@(Get-ChildItem "$dir\share\ccs" -Filter *.tsv -ErrorAction SilentlyContinue).Count -eq 2) "share\ccs: TI's two CCS option tables"
  $list = (& "$dir\link.exe" /LIB /LIST "$win\grtstart.lib" 2>&1) + (& "$dir\link.exe" /LIB /LIST "$win\kernel32.lib" 2>&1)
  Check (($list -match '^\s+mainCRTStartup$').Count -eq 1 -and ($list -match '^\s+__imp_ExitProcess$').Count -eq 1) "link /LIB /LIST: grtstart.lib defines mainCRTStartup, kernel32.lib imports ExitProcess"
  # a C program built by the installed toolchain and linked with GRIDE's runtime alone: no LIB, no INCLUDE
  $t = Join-Path $logs "grt-$c"; if (Test-Path $t) { Remove-Item $t -Recurse -Force }; New-Item -ItemType Directory $t | Out-Null
  [IO.File]::WriteAllText("$t\hello.c", "#include <stdio.h>`nint main(void) { printf(""hello from %s\n"", ""GRIDE's runtime""); return 7; }`n")
  $savedLib = $env:LIB; $savedInc = $env:INCLUDE; $env:LIB = ""; $env:INCLUDE = ""
  & "$dir\c90.exe" -S "$t\hello.c" -o "$t\hello.s" -arch x86_64-windows -O2 -masm=masm -nologo 2>&1 | Out-Null
  & "$dir\masm.exe" -t x64 "$t\hello.s" -o "$t\hello.obj" 2>&1 | Out-Null
  & "$dir\link.exe" /nologo /subsystem:console "/out:$t\hello.exe" "$t\hello.obj" "$win\grtstart.lib" "$win\grtcompat.lib" "$win\grtcxx.lib" "$win\grtc.lib" "$win\grtwin.lib" "$win\kernel32.lib" 2>&1 | Out-Null
  $env:LIB = $savedLib; $env:INCLUDE = $savedInc
  $said = if (Test-Path "$t\hello.exe") { & "$t\hello.exe" } else { "" }
  $imports = if (Test-Path "$t\hello.exe") { @(Get-Imports "$t\hello.exe") } else { @() }
  Check ("$said" -eq "hello from GRIDE's runtime" -and $LASTEXITCODE -eq 7 -and ($imports -join ',') -eq "KERNEL32.dll") "a C program linked with GRIDE's runtime alone runs: '$said', exit $LASTEXITCODE, imports $($imports -join ', ')"
  # the programs carry the static VC++ runtime: only GRIDE.exe (C++/CLI) needs the DLLs beside it
  $dynamic = @("c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747", "gride-dbg" | Where-Object { (Test-Path "$dir\$_.exe") -and (@(Get-Imports "$dir\$_.exe") -match '^(?i)(vcruntime|msvcp|api-ms-win-crt)').Count -gt 0 })
  Check ($dynamic.Count -eq 0) "the programs need no VC++ runtime DLL$(if ($dynamic) { ' - these do: ' + ($dynamic -join ', ') })"
  # the runtime's sources gride-dbg shows on a step into it (docs\Runtime_Design.md 9.3)
  Check ((Test-Path "$dir\src\runtime\RT\c\fmt\Print.cpp") -and (Test-Path "$dir\src\runtime\RT\win\heap.c") -and (Test-Path "$dir\src\runtime\RTS6x\stdio\Formatter.cpp") -and -not (Test-Path "$dir\src\runtime\RT\tests")) "GRIDE's runtime's sources under src\runtime (RT and RTS6x), without its tests"
  Check (Test-Path "$dir\docs\GRIDE_Help.html") "the shared Help, docs\GRIDE_Help.html"
  Check (Test-Path "$dir\vcruntime140.dll") "the VC++ runtime beside GRIDE.exe"
  foreach ($o in $others) { Check (-not (Test-Path (Join-Path $base $folders[$o]))) "$($folders[$o]) not installed" }
  Check (Test-Path (Join-Path $menu $links[$c])) "Start menu: $($links[$c])"
  Check (Test-Path "$regKey\$c") "HKCU registry key"
  $name = try { [string](Get-MsiProp $inst "ProductInfo" @($prop.ProductCode, "InstalledProductName")) } catch { "" }
  $publisher = try { [string](Get-MsiProp $inst "ProductInfo" @($prop.ProductCode, "Publisher")) } catch { "" }
  Check ($name -eq $prop.ProductName -and $publisher -eq $prop.Manufacturer) "Programs and Features: '$name', publisher '$publisher'"
  # all of its output first: a Select-Object -First on the pipe stops it early, and the exit code is then -1
  $said = @(& "$dir\cpp11.exe" --version 2>&1)
  $rc = $LASTEXITCODE
  $v = $said | Where-Object { "$_" -match '^Version ' } | Select-Object -First 1
  Check ($rc -eq 0 -and "$v" -match "^Version $([regex]::Escape($Cpp11Version))\b") "cpp11 --version: $v (exit $rc)"
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
