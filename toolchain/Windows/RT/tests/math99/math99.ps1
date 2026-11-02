<#
  math99.ps1 - the C99 <math.h> functions of stage 2 against the UCRT (docs\Runtime_Design.md 16): math99.cpp built
  once by cpp11, linked with GRIDE's runtime alone (LIB empty, the six libraries by path) and, where Visual Studio's
  and the SDK's libraries are found, with Microsoft's static CRT; each run's transcript set beside math99.expected,
  which the UCRT's run recorded.
    - The functions that work on bits or round to an integer (copysign ... remquo, fma, nan) must give the UCRT's
      line exactly: value, NaN and errno.
    - The others (exp2 ... lgamma) are computed to within half an ulp; a line within 1 ulp of the UCRT's, errno the
      same, passes. A line further from it passes only when math99.judged names it with this very result: lines
      where a 110-digit reference showed the UCRT off by more than an ulp and GRIDE's runtime within half of one
      (name, argument, GRIDE's result, the UCRT's, then each one's error in ulps, "nan" against a zero).

    .\math99.ps1 [-Opt -O2|-O0] [-Bin <toolchain folder>] [-Libs <the runtime's folder>]
#>
param([string]$Opt = '-O2', [string]$Bin = "", [string]$Libs = "")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib' }
$work = Join-Path $env:TEMP 'gride-math99'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
}
Tool cpp11 @('-S', "$here\math99.cpp", '-o', "$work\math99.s", '-arch', 'x86_64-windows', $Opt, '-masm=masm', '-nologo')
Tool masm @('-t', 'x64', "$work\math99.s", '-o', "$work\math99.obj")
$saved = $env:LIB
try {
    $env:LIB = ''
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\grt.exe", "$work\math99.obj") +
               (@('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }))
    $vs = Get-ChildItem 'D:\Microsoft Visual Studio 2026\VC\Tools\MSVC', "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
          Where-Object { Test-Path (Join-Path $_.FullName 'lib\x64\libcmt.lib') } | Select-Object -First 1
    $kit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Lib" -Directory -ErrorAction SilentlyContinue |
           Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\x64\libucrt.lib') } | Sort-Object Name -Descending | Select-Object -First 1
    $ms = $vs -and $kit
    if ($ms) {
        $env:LIB = @((Join-Path $vs.FullName 'lib\x64'), (Join-Path $kit.FullName 'ucrt\x64'), (Join-Path $kit.FullName 'um\x64')) -join ';'
        Tool link @('/nologo', '/subsystem:console', "/out:$work\msvc.exe", "$work\math99.obj", 'libcmt.lib', 'libucrt.lib',
                    'libvcruntime.lib', 'kernel32.lib', 'legacy_stdio_definitions.lib')
    }
} finally { $env:LIB = $saved }

$expected = [IO.File]::ReadAllLines("$here\math99.expected")
$judged = @{}
foreach ($l in [IO.File]::ReadAllLines("$here\math99.judged")) { $p = $l.Split(' '); $judged["$($p[0]) $($p[1])"] = $p[2] }
$exact = @('logb', 'ilogb', 'rint', 'nearbyint', 'lrint', 'llrint', 'lround', 'llround', 'scalbn', 'scalbn2', 'scalbln',
           'copysign', 'fmax', 'fmin', 'fdim', 'nextafter', 'nexttoward', 'remainder', 'remquo', 'fma1', 'fma2', 'fma3', 'nan')

function Ordered([string]$hex) {
    $u = [Convert]::ToUInt64($hex, 16)
    $m = [decimal]($u -band 0x7FFFFFFFFFFFFFFF)
    if ($u -band 0x8000000000000000) { return -$m }
    return $m
}

$faults = 0
foreach ($k in @('grt') + $(if ($ms) { @('msvc') } else { @() })) {
    & "$work\$k.exe" > "$work\$k.out"
    $got = @([IO.File]::ReadAllLines("$work\$k.out")) + "exit $LASTEXITCODE"
    $tally = [ordered]@{}
    $wrong = @()
    for ($i = 0; $i -lt [Math]::Max($got.Count, $expected.Count); $i++) {
        $g = $got[$i]; $e = $expected[$i]
        $name = if ($e) { $e.Split(' ')[0] } else { 'missing' }
        if (-not $tally.Contains($name)) { $tally[$name] = @(0, 0, 0) }
        if ($g -ceq $e) { $tally[$name][0]++; continue }
        $ok = $false
        if ($k -eq 'grt' -and $g -and $e -and $exact -notcontains $name) {
            $pg = $g.Split(' '); $pe = $e.Split(' ')
            $at = [Array]::IndexOf($pg, '=')
            if ($at -gt 0 -and $pe[$at] -eq '=') {
                $d = [Math]::Abs((Ordered $pg[$at + 1]) - (Ordered $pe[$at + 1]))
                if ($d -le 1 -and $pg[$at + 2] -eq $pe[$at + 2]) { $tally[$name][1]++; $ok = $true }
                elseif ($judged["$name $($pg[1])"] -eq $pg[$at + 1]) { $tally[$name][2]++; $ok = $true }
            }
        }
        if (-not $ok) { $wrong += "  line $($i + 1): expected '$e'`n  line $($i + 1): got      '$g'" }
    }
    "$k`t   same  1 ulp  judged"
    foreach ($n in $tally.Keys) { "{0,-12} {1,6} {2,6} {3,6}" -f $n, $tally[$n][0], $tally[$n][1], $tally[$n][2] }
    if ($wrong.Count) { $faults++; "$k`: $($wrong.Count) lines wrong"; $wrong | Select-Object -First 20 }
    else { "$k`: every line passes" }
}
if ($faults) { throw "math99: $faults run(s) differ" }
