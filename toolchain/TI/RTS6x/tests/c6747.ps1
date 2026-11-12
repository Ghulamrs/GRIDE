<#
  c6747.ps1 - RTS6x's tests on the C6747, run by sim6747 (docs\Runtime_Design.md 11; the runtimes review's probes,
  section 16). Each source in tests.list (paths from the repository's root) is compiled for tms6747 - c90 for a .c,
  cpp11 for a .cpp - at each level, assembled by asm6x, linked by lnk6x with rts6x.lib (rts6xd.lib under -DebugRuntime)
  and the linker command file GRIDE writes (a 1 MB stack, a 16 MB heap: toolchain.json's ti-link), and run by
  `sim6747 --run -c`. Its standard output and "exit <status>" are compared with <name>.c6747.expected beside the
  source, or <name>.expected (the UCRT's transcript a Windows probe records) where the C6747 prints the same.

    .\c6747.ps1 [-Bin <toolchain folder>] [-Levels -O0,-O2] [-Filter <regex>] [-DebugRuntime]
#>
param([string]$Bin = "", [string]$Levels = "-O0,-O2", [string]$Filter = "", [switch]$DebugRuntime)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..\..\..')).Path
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $repo 'GRIDEd\x64\Release')).Path }
$lib = Join-Path $Bin 'lib\tms6747'
$rts = if ($DebugRuntime) { 'rts6xd.lib' } else { 'rts6x.lib' }
$work = Join-Path $env:TEMP 'rts6x-c6747'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
@('--rom_model', '--stack_size=0x100000', '--heap_size=0x1000000', 'MEMORY', '{', '    RAM : origin = 0xC0000000, length = 0x04000000', '}',
  'SECTIONS', '{', '    .text        > RAM', '    .const       > RAM', '    .data        > RAM', '    .bss         > RAM',
  '    .far         > RAM', '    .fardata     > RAM', '    .neardata    > RAM', '    .rodata      > RAM', '    .cinit       > RAM',
  '    .init_array  > RAM', '    .switch      > RAM', '    .cio         > RAM', '    .stack       > RAM', '    .sysmem      > RAM',
  '    .vm6747.eh   > RAM', '}') | Set-Content -Encoding ASCII (Join-Path $work 'ti-link.cmd')

function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
}

$sources = @(Get-Content (Join-Path $here 'tests.list') | Where-Object { $_.Trim() -ne '' -and -not $_.StartsWith('#') } |
             Where-Object { -not $Filter -or $_ -match $Filter })
$failed = 0; $ran = 0
foreach ($rel in $sources) {
    $src = Join-Path $repo $rel
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $expectedFile = [IO.Path]::ChangeExtension($src, '.c6747.expected')
    if (-not (Test-Path $expectedFile)) { $expectedFile = [IO.Path]::ChangeExtension($src, '.expected') }
    $want = ([IO.File]::ReadAllText($expectedFile)).Replace("`r`n", "`n")
    $compiler = if ([IO.Path]::GetExtension($src) -eq '.c') { 'c90' } else { 'cpp11' }
    foreach ($o in $Levels.Split(',')) {
        $ran++
        $tag = "$name$o"
        try {
            $flags = @('-S', $src, '-o', "$work\$tag.s", '-arch', 'tms6747', $o, '-nologo')
            if ($DebugRuntime) { $flags += '-g' }
            if ($DebugRuntime -and $o -ne '-O0') { $ran--; "$name ${o}: skipped (-g with -O is refused for tms6747)"; continue }
            Tool $compiler $flags
            Tool asm6x @("$work\$tag.s", '-o', "$work\$tag.obj")
            Tool lnk6x @('-mv6740', '--abi=eabi', '-i', $lib, "$work\ti-link.cmd", "$work\$tag.obj", '-l', $rts, '-o', "$work\$tag.out")
            $t = [Diagnostics.Stopwatch]::StartNew()
            # sim6747 says its CYCLES line on stderr, which is not a failure here
            $ErrorActionPreference = 'Continue'
            $out = @(& (Join-Path $Bin 'sim6747.exe') --run -c "$work\$tag.out" 2>&1 | ForEach-Object { "$_" })
            $ErrorActionPreference = 'Stop'
            $status = 'none'
            $lines = @()
            foreach ($l in $out) {
                if ($l -match '^CYCLES .*status=(\d+)') { $status = $Matches[1]; continue }
                $lines += $l
            }
            $said = (($lines + "exit $status") -join "`n") + "`n"
            if ($said -ceq $want) { "{0} {1}: the same as {2} ({3:N1} s)" -f $name, $o, [IO.Path]::GetFileName($expectedFile), $t.Elapsed.TotalSeconds }
            else {
                $failed++
                "$name ${o}: DIFFERS from $([IO.Path]::GetFileName($expectedFile))"
                [IO.File]::WriteAllText("$work\$tag.said", $said)
                Compare-Object ($want -split "`n") ($said -split "`n") | Select-Object -First 8 | ForEach-Object { "    $($_.SideIndicator) $($_.InputObject)" }
            }
        } catch {
            $failed++
            "$name ${o}: $($_.Exception.Message.Split("`n")[0..3] -join ' | ')"
        }
    }
}
"$($ran - $failed) of $ran runs as expected ($rts)"
exit $(if ($failed -gt 0) { 1 } else { 0 })
