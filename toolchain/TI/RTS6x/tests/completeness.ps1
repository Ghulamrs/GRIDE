<#
  completeness.ps1 - every name the runtime's catalogue (toolchain\Windows\RT\catalogue.json) says is done for tms6747,
  referenced by one C program linked with rts6x.lib alone and run by sim6747: the C6747's half of the Windows
  completeness test (toolchain\Windows\RT\tests\completeness.ps1), which the build's CatalogueCheck does not cover.

    .\completeness.ps1 [-Bin <toolchain folder>]
#>
param([string]$Bin = "")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..\..\..')).Path
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $repo 'GRIDEd\x64\Release')).Path }
$cat = Get-Content (Join-Path $repo 'toolchain\Windows\RT\catalogue.json') -Raw | ConvertFrom-Json
$done = @($cat.entries | Where-Object { $_.targets.tms6747.status -eq 'done' -and $_.kind -ne 'entry' })
$names = @($done | ForEach-Object { $_.targets.tms6747.link } | Sort-Object -Unique)
$work = Join-Path $env:TEMP 'rts6x-completeness'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
$c = @('/* completeness.c - written by completeness.ps1: every name the catalogue says is done for tms6747, referenced */', 'int printf(const char *, ...);')
foreach ($n in $names) { $c += "extern char $n;" }
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
@('--rom_model', '--stack_size=0x100000', '--heap_size=0x1000000', 'MEMORY', '{', '    RAM : origin = 0xC0000000, length = 0x04000000', '}',
  'SECTIONS', '{', '    .text > RAM', '    .const > RAM', '    .data > RAM', '    .bss > RAM', '    .far > RAM', '    .fardata > RAM',
  '    .neardata > RAM', '    .rodata > RAM', '    .cinit > RAM', '    .init_array > RAM', '    .switch > RAM', '    .cio > RAM',
  '    .stack > RAM', '    .sysmem > RAM', '    .vm6747.eh > RAM', '}') | Set-Content -Encoding ASCII "$work\ti-link.cmd"
function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
}
Tool c90 @('-S', "$work\completeness.c", '-o', "$work\completeness.s", '-arch', 'tms6747', '-O2', '-nologo')
Tool asm6x @("$work\completeness.s", '-o', "$work\completeness.obj")
Tool lnk6x @('-mv6740', '--abi=eabi', '-i', (Join-Path $Bin 'lib\tms6747'), "$work\ti-link.cmd", "$work\completeness.obj", '-l', 'rts6x.lib', '-o', "$work\completeness.out")
$ErrorActionPreference = 'Continue'
$said = @(& (Join-Path $Bin 'sim6747.exe') --run -c "$work\completeness.out" 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -notmatch '^CYCLES' })
$ErrorActionPreference = 'Stop'
"$($said -join ' ') (of $($names.Count) in the catalogue done for tms6747; $(@($cat.entries).Count) entries in all)"
if (($said -join ' ') -ne "$($names.Count) names linked") { throw "completeness: the program said '$($said -join ' ')'" }
