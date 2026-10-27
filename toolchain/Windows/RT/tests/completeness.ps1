<#
  completeness.ps1 - the catalogue's completeness test (docs\Runtime_Design.md 11.8): a C program that references
  every name catalogue.json says is done for x86_64-windows (a function by its name, a macro by its link name:
  _errno, _setjmp, _assert, __acrt_iob_func), linked against GRIDE's runtime alone - LIB empty, the six
  libraries by path - and run. Each name is declared as an extern object, so no header's view of it gets in the
  way: the link alone says whether the runtime defines it. That every name is declared by its header is the
  build's CatalogueCheck.

    .\completeness.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>]
#>
param([string]$Bin = "", [string]$Libs = "")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib' }
$cat = Get-Content (Join-Path $here '..\catalogue.json') -Raw | ConvertFrom-Json
$names = @($cat.entries | Where-Object { $_.targets.'x86_64-windows'.status -eq 'done' } | ForEach-Object { $_.targets.'x86_64-windows'.link } | Sort-Object -Unique)
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
$saved = $env:LIB
$env:LIB = ''
try {
    $grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\completeness.exe", "$work\completeness.obj") + $grt)
} finally { $env:LIB = $saved }
$said = & "$work\completeness.exe"
"$said (of $($names.Count) in the catalogue done for x86_64-windows; $(@($cat.entries).Count) entries in all)"
if ($said -ne "$($names.Count) names linked") { throw "completeness: the program said '$said'" }
