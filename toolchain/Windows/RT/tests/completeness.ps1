<#
  completeness.ps1 - the catalogue's completeness test (docs\Runtime_Design.md 11.8): a C program that references
  every name catalogue.json says is done for x86_64-windows (a function by its name, a macro by its link name:
  _errno, _setjmp, _assert, __acrt_iob_func), linked against GRIDE's runtime alone - LIB empty, the six
  libraries by path - and run. Each name is declared as an extern object, so no header's view of it gets in the
  way: the link alone says whether the runtime defines it. That every name is declared by its header is the
  build's CatalogueCheck. An entry of kind "entry" is where a program starts and calls the program's own function, so
  it cannot sit beside main: each gets a small program that defines that function, the name without CRTStartup, and
  whose link, given no /entry and no /subsystem, must take that entry.

    .\completeness.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>]
#>
param([string]$Bin = "", [string]$Libs = "")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib\x86_64-windows'; if (-not (Test-Path (Join-Path $Libs 'grtc.lib'))) { $Libs = Join-Path $Bin 'lib' } }
$cat = Get-Content (Join-Path $here '..\catalogue.json') -Raw | ConvertFrom-Json
$done = @($cat.entries | Where-Object { $_.targets.'x86_64-windows'.status -eq 'done' })
$names = @($done | Where-Object { $_.kind -ne 'entry' } | ForEach-Object { $_.targets.'x86_64-windows'.link } | Sort-Object -Unique)
$entries = @($done | Where-Object { $_.kind -eq 'entry' } | ForEach-Object { $_.targets.'x86_64-windows'.link } | Sort-Object -Unique)
$work = Join-Path $env:TEMP 'gride-completeness'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
$c = @('/* completeness.c - written by completeness.ps1: every name the catalogue says is done, referenced */', '#include <stdio.h>')
$i = 0
foreach ($n in $names) { $c += "extern char $n;"; $i++ }
# the table filled in code: c90 1.x declares an extern object EXTERN only where code refers to it
$c += "static char *names[$($names.Count)];"
$c += 'int main(void)'
$c += '{'
$c += '    unsigned i, n = 0;'
$k = 0
foreach ($n in $names) { $c += "    names[$k] = &$n;"; $k++ }
$c += '    for (i = 0; i < sizeof names / sizeof names[0]; i++) n += names[i] != 0;'
$c += '    printf("%u names linked\n", n);'
$c += '    return 0;'
$c += '}'
[IO.File]::WriteAllLines("$work\completeness.c", $c)
function Tool([string]$name, [string[]]$a) {
    $o = & (Join-Path $Bin "$name.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$name failed:`n$($o -join "`n")" }
}
Tool c90 @('-S', "$work\completeness.c", '-o', "$work\completeness.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
Tool masm @('-t', 'x64', "$work\completeness.s", '-o', "$work\completeness.obj")
$grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
$saved = $env:LIB
$env:LIB = ''
try {
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\completeness.exe", "$work\completeness.obj") + $grt)
    foreach ($e in $entries) {
        $f = $e -replace 'CRTStartup$', ''
        [IO.File]::WriteAllText("$work\$e.c", "int $f(void) { return 42; }`n")
        Tool c90 @('-S', "$work\$e.c", '-o', "$work\$e.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
        Tool masm @('-t', 'x64', "$work\$e.s", '-o', "$work\$e.obj")
        Tool link (@('/nologo', "/out:$work\$e.exe", "$work\$e.obj", "/map:$work\$e.map") + $grt)
    }
} finally { $env:LIB = $saved }
$said = & "$work\completeness.exe"
"$said (of $($names.Count) in the catalogue done for x86_64-windows; $(@($cat.entries).Count) entries in all)"
if ($said -ne "$($names.Count) names linked") { throw "completeness: the program said '$said'" }
foreach ($e in $entries) {
    $p = Start-Process -FilePath "$work\$e.exe" -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 42) { throw "completeness: $e ended with $($p.ExitCode), not its function's 42" }
}
"$($entries.Count) entries started their programs (of $($entries.Count) in the catalogue done for x86_64-windows)"
