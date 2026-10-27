<#
  bench.ps1 - the runtime's benchmarks (docs\Runtime_Design.md 10.5): bench.c compiled once by c90 -O2 and GRIDE's
  masm, linked twice by GRIDE's link - against GRIDE's runtime (its libraries by path, LIB empty) and against
  Microsoft's static CRT (libcmt, libucrt, libvcruntime through Visual Studio's LIB) - and each test run -Runs
  times on each, the best kept. Also: hello world's start-to-exit time (200 runs) and size on each.
  The table is printed and, with -Record, added to results.md beside this script with the machine and the date.

    .\bench.ps1 [-Bin <toolchain folder>] [-Libs <GRIDE's runtime's folder>] [-Runs 5] [-Tests a,b] [-Record]

  Run it on a quiet machine: the numbers are wall-clock times.
#>
param([string]$Bin = "", [string]$Libs = "", [int]$Runs = 5, [string[]]$Tests = @(), [switch]$Record, [string]$Label = "",
      [string]$Prototype = "")
# -Prototype <folder>: also the prototype runtime (its grt.lib and kernel32.lib there), a third column, so a stage can
# show it is slower than the prototype on no test
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib' }
if ($Tests.Count -eq 0) {
    $Tests = @('printf-int', 'printf-float', 'printf-string', 'printf-stdout', 'sscanf-int', 'sscanf-float',
               'fwrite', 'fread', 'fgets', 'putc', 'getc',
               'memcpy-16', 'memcpy-256', 'memcpy-4096', 'memcpy-65536', 'memcpy-1048576',
               'memset-16', 'memset-256', 'memset-4096', 'memset-65536', 'memset-1048576',
               'memmove-256', 'memmove-65536', 'strlen', 'strcmp', 'strchr',
               'malloc-small', 'malloc-mixed', 'qsort', 'sqrt', 'sin', 'exp', 'log', 'pow')
}
$work = Join-Path $env:TEMP "gride-bench"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

function Tool([string]$name, [string[]]$a) {
    $o = & (Join-Path $Bin "$name.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$name failed: $o" }
}
# the objects, once
Tool c90 @('-S', "$here\bench.c", '-o', "$work\bench.s", '-arch', 'x86_64-windows', '-O2', '-DNDEBUG=1', '-masm=masm', '-nologo')
Tool masm @('-t', 'x64', "$work\bench.s", '-o', "$work\bench.obj")
[IO.File]::WriteAllText("$work\hello.c", "#include <stdio.h>`nint main(void) { printf(`"hello, world\n`"); return 0; }`n")
Tool c90 @('-S', "$work\hello.c", '-o', "$work\hello.s", '-arch', 'x86_64-windows', '-O2', '-DNDEBUG=1', '-masm=masm', '-nologo')
Tool masm @('-t', 'x64', "$work\hello.s", '-o', "$work\hello.obj")

# GRIDE's runtime: its libraries by path, nothing from LIB
$grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
$savedLib = $env:LIB
$env:LIB = ''
foreach ($p in 'bench', 'hello') { Tool link (@('/nologo', '/subsystem:console', "/out:$work\$p-grt.exe", "$work\$p.obj") + $grt) }
$runtimes = @('grt', 'ms')
if ($Prototype) {
    foreach ($p in 'bench', 'hello') { Tool link @('/nologo', '/subsystem:console', "/out:$work\$p-proto.exe", "$work\$p.obj", "$Prototype\grt.lib", "$Prototype\kernel32.lib") }
    $runtimes += 'proto'
}
# Microsoft's static CRT, through Visual Studio's and the Windows SDK's LIB
$vs = 'D:\Microsoft Visual Studio 2026'
$msvc = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
$sdk = Get-ChildItem $kits -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um\x64') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$env:LIB = (Join-Path $msvc.FullName 'lib\x64') + ';' + (Join-Path $sdk.FullName 'ucrt\x64') + ';' + (Join-Path $sdk.FullName 'um\x64')
$ms = @('libcmt.lib', 'libucrt.lib', 'libvcruntime.lib', 'kernel32.lib', 'legacy_stdio_definitions.lib')
foreach ($p in 'bench', 'hello') { Tool link (@('/nologo', '/subsystem:console', "/out:$work\$p-ms.exe", "$work\$p.obj") + $ms) }
$env:LIB = $savedLib

# one test once: its milliseconds and checksum from stderr, stdout to the null device
function Once([string]$exe, [string]$test) {
    $err = "$work\err.txt"
    $p = Start-Process -FilePath $exe -ArgumentList $test -WorkingDirectory $work -NoNewWindow -PassThru -Wait `
                       -RedirectStandardOutput 'NUL' -RedirectStandardError $err
    $line = (Get-Content $err | Select-Object -First 1)
    if ($line -notmatch '^\S+ (\d+) check (-?\d+)') { throw "$exe $test said: $line" }
    return @([int]$Matches[1], $Matches[2])
}
$rows = @()
foreach ($t in $Tests) {
    $best = @{}; $check = @{}
    foreach ($r in $runtimes) {
        $best[$r] = [int]::MaxValue
        for ($i = 0; $i -lt $Runs; $i++) {
            $x = Once "$work\bench-$r.exe" $t
            if ($x[0] -lt $best[$r]) { $best[$r] = $x[0] }
            $check[$r] = $x[1]
        }
    }
    $ratio = if ($best['ms'] -gt 0) { $best['grt'] / $best['ms'] } else { 0 }
    $rows += [pscustomobject]@{ Test = $t; GRIDE = $best['grt']; Microsoft = $best['ms']; Ratio = '{0:N2}' -f $ratio
                                Prototype = $(if ($Prototype) { $best['proto'] } else { '' })
                                Same = $(if ($check['grt'] -eq $check['ms']) { 'yes' } else { "NO ($($check['grt']) / $($check['ms']))" }) }
}
foreach ($r in $runtimes) {
    $w = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt 200; $i++) { $null = & "$work\hello-$r.exe" }
    $w.Stop()
    Set-Variable -Name "start_$r" -Value ($w.Elapsed.TotalMilliseconds / 200)
}
$rows += [pscustomobject]@{ Test = 'hello start-to-exit (ms, mean of 200)'; GRIDE = '{0:N2}' -f $start_grt; Microsoft = '{0:N2}' -f $start_ms
                            Ratio = '{0:N2}' -f ($start_grt / $start_ms); Same = ''; Prototype = $(if ($Prototype) { '{0:N2}' -f $start_proto } else { '' }) }
$sg = (Get-Item "$work\hello-grt.exe").Length; $sm = (Get-Item "$work\hello-ms.exe").Length
$rows += [pscustomobject]@{ Test = 'hello size (bytes)'; GRIDE = $sg; Microsoft = $sm; Ratio = '{0:N2}' -f ($sg / $sm); Same = ''
                            Prototype = $(if ($Prototype) { (Get-Item "$work\hello-proto.exe").Length } else { '' }) }
$rows | Format-Table -AutoSize | Out-String -Width 200

if ($Record) {
    $cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
    $md = @("", "## $(Get-Date -Format 'yyyy-MM-dd HH:mm') $Label", "", "Machine: $cpu. Best of $Runs runs, milliseconds; ratio = GRIDE / Microsoft (below 1 is faster).", "",
            "| Test | GRIDE's runtime | Microsoft's static CRT | Ratio | Same work | Prototype |", "|---|---:|---:|---:|---|---:|")
    foreach ($r in $rows) { $md += "| $($r.Test) | $($r.GRIDE) | $($r.Microsoft) | $($r.Ratio) | $($r.Same) | $($r.Prototype) |" }
    Add-Content -Path (Join-Path $here 'results.md') -Value $md -Encoding UTF8
    "recorded in $(Join-Path $here 'results.md')"
}
