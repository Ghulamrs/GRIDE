<#
  demo.ps1 - three examples that between them run all 11 programs of GRIDE 2.0's toolchain, as GRIDE.sln
  builds them (Build > Build Solution) beside GRIDE.exe in x64\<Configuration>\:

    1  sieve.c on Windows        c90 -> masm -> link -> sieve.exe, run
    2  shapes.cpp on the C6747   cpp11 -> asm6x -> lnk6x (rts6x.lib) -> shapes.out, run by sim6747
    3  sieve.c as Shalimar       c2s -> sieve.shl -> shalimar, then
                                   a  vm6747 runs the C6747 assembly straight (lib\shmrt-tms6747)
                                   b  asm6x -> lnk6x (shmrt6x.lib, rts6x.lib) -> sim6747
                                   c  masm -> link (shmrt-x86_64-windows.lib) -> a Windows program
                                 each prints what example 1 printed: the C and the Shalimar program agree

  The command lines are toolchain.json's (GRIDE's own). Nothing goes through cmd.exe. Run from PowerShell:

    .\demo.ps1 [-Configuration Release|Debug]
#>
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..')).Path
$bin  = Join-Path $root "x64\$Configuration"
$out  = Join-Path $here "out\$Configuration"
$rts  = Join-Path $bin 'lib\rts6x-tms6747'
$debug = $Configuration -eq 'Debug'

foreach ($exe in 'c90','cpp11','shalimar','c2s','masm','link','asm6x','lnk6x','sim6747','vm6747') {
    if (-not (Test-Path (Join-Path $bin "$exe.exe"))) { throw "no $exe.exe in $bin - build GRIDE.sln ($Configuration|x64) first" }
}
foreach ($lib in 'rts6x.lib','shmrt6x.lib') {
    if (-not (Test-Path (Join-Path $rts $lib))) { throw "no $lib in $rts - the rts6x project builds it" }
}
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

# Visual Studio's libraries for link (the CRT, kernel32), found as GRIDE finds them: vswhere, the v143 tools
# (14.44.*) when installed, else the newest; the newest Windows SDK
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$msvc = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending
$msvc = @($msvc | Where-Object Name -like '14.44.*') + @($msvc) | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
$sdk = Get-ChildItem $kits -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um\x64') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$env:LIB = (Join-Path $msvc.FullName 'lib\x64') + ';' + (Join-Path $sdk.FullName 'ucrt\x64') + ';' + (Join-Path $sdk.FullName 'um\x64')
$crt = if ($debug) { 'libcmtd.lib','libucrtd.lib','libvcruntimed.lib' } else { 'libcmt.lib','libucrt.lib','libvcruntime.lib' }
$crt += 'kernel32.lib','legacy_stdio_definitions.lib'

# one program run directly (no shell), its command line shown first, its output after; a non-zero exit stops the demo
function Step([string]$exe, [string[]]$arguments) {
    $line = ($arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_ + '"' } else { $_ } }) -join ' '
    Write-Host ("> {0} {1}" -f [IO.Path]::GetFileName($exe), $line.Replace($root + '\', '')) -ForegroundColor Cyan
    $psi = New-Object Diagnostics.ProcessStartInfo $exe, $line
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.WorkingDirectory = $out
    $p = [Diagnostics.Process]::Start($psi)
    $so = $p.StandardOutput.ReadToEndAsync(); $se = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(120000)) { $p.Kill(); throw "$([IO.Path]::GetFileName($exe)) gave no answer in two minutes" }
    $p.WaitForExit()
    $text = ($so.Result + $se.Result).TrimEnd()
    if ($text) { Write-Host $text }
    if ($p.ExitCode -ne 0) { throw "$([IO.Path]::GetFileName($exe)) exited with $($p.ExitCode)" }
    return $so.Result
}
function Tool([string]$name) { Join-Path $bin "$name.exe" }
function Title([string]$text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Yellow }
function Same([string]$a, [string]$b) {
    # line by line, the space Shalimar's ? may leave at a line's end not counted
    $x = ($a -split "`r?`n" | ForEach-Object { $_.TrimEnd() } | Where-Object { $_ -ne '' }) -join "`n"
    $y = ($b -split "`r?`n" | ForEach-Object { $_.TrimEnd() } | Where-Object { $_ -ne '' }) -join "`n"
    if ($x -eq $y) { Write-Host 'the same lines as example 1' -ForegroundColor Green } else { throw 'the output differs from example 1' }
}

$cflags = if ($debug) { '-g','-D_DEBUG=1' } else { '-O2','-DNDEBUG=1' }
# Windows through masm and link carries no line table (GRIDE's Debug build goes through clang and Microsoft's
# link.exe for CodeView instead), so the Windows steps leave -g out
$winflags = if ($debug) { @('-D_DEBUG=1') } else { $cflags }
$sieve = Join-Path $here 'sieve.c'
$shapes = Join-Path $here 'shapes.cpp'

# the linker command file GRIDE writes for a C6747 build (toolchain.json, linkerCommandFiles.ti-link)
@('--rom_model', '--stack_size=0x4000', '--heap_size=0x100000', 'MEMORY', '{', '    RAM : origin = 0xC0000000, length = 0x04000000', '}',
  'SECTIONS', '{', '    .text        > RAM', '    .const       > RAM', '    .data        > RAM', '    .bss         > RAM',
  '    .far         > RAM', '    .fardata     > RAM', '    .neardata    > RAM', '    .rodata      > RAM', '    .cinit       > RAM',
  '    .init_array  > RAM', '    .switch      > RAM', '    .cio         > RAM', '    .stack       > RAM', '    .sysmem      > RAM',
  '    .vm6747.eh   > RAM', '}') | Set-Content -Encoding ASCII (Join-Path $out 'ti-link.cmd')
$c6x = if ($debug) { 'rts6xd.lib' } else { 'rts6x.lib' }
$shm6x = if ($debug) { 'shmrt6xd.lib' } else { 'shmrt6x.lib' }

Title "1  sieve.c on Windows: c90 -> masm -> link"
Step (Tool c90) (@('-S', $sieve, '-o', "$out\sieve.s", '-arch', 'x86_64-windows') + $winflags) | Out-Null
Step (Tool masm) @('-t', 'x64', "$out\sieve.s", '-o', "$out\sieve.obj") | Out-Null
Step (Tool link) (@('/nologo', '/subsystem:console', "/out:$out\sieve.exe", "$out\sieve.obj") + $crt) | Out-Null
$expected = Step "$out\sieve.exe" @()

Title "2  shapes.cpp on the C6747: cpp11 -> asm6x -> lnk6x (RTS6x) -> sim6747"
Step (Tool cpp11) (@('-S', $shapes, '-o', "$out\shapes.s", '-arch', 'tms6747') + $cflags) | Out-Null
Step (Tool asm6x) @("$out\shapes.s", '-o', "$out\shapes.obj") | Out-Null
Step (Tool lnk6x) @('-mv6740', '--abi=eabi', '-i', $rts, "$out\ti-link.cmd", "$out\shapes.obj", '-l', $c6x, '-o', "$out\shapes.out") | Out-Null
Step (Tool sim6747) @('--run', '-c', "$out\shapes.out") | Out-Null

Title "3  sieve.c as Shalimar: c2s -> shalimar"
Step (Tool c2s) @('--to-shalimar', $sieve, '-o', "$out\sieve.shl") | Out-Null
Get-Content "$out\sieve.shl" | Write-Host -ForegroundColor DarkGray
$shflags = if ($debug) { @('-g') } else { @() }
Step (Tool shalimar) (@('-S', "$out\sieve.shl", '-o', "$out\sieve-shl.s", '--target=tms6747') + $shflags) | Out-Null

Title "3a the C6747 assembly run straight: vm6747"
$shmdir = Join-Path $bin $(if ($debug) { 'lib\shmrt-tms6747-debug' } else { 'lib\shmrt-tms6747' })
Same $expected (Step (Tool vm6747) @("$out\sieve-shl.s", $shmdir))

Title "3b linked for the C6747: asm6x -> lnk6x (shmrt6x, RTS6x) -> sim6747"
Step (Tool asm6x) @("$out\sieve-shl.s", '-o', "$out\sieve-shl.obj") | Out-Null
Step (Tool lnk6x) @('-mv6740', '--abi=eabi', '-i', $rts, "$out\ti-link.cmd", "$out\sieve-shl.obj", '-l', $shm6x, '-l', $c6x, '-o', "$out\sieve-shl.out") | Out-Null
Same $expected (Step (Tool sim6747) @('--run', '-c', "$out\sieve-shl.out"))

Title "3c linked for Windows: shalimar -> masm -> link (the Shalimar runtime for Windows)"
$shwin = if ($debug) { @('--debug') } else { @() }
$shmwin = Join-Path $bin $(if ($debug) { 'lib\shmrt-x86_64-windows-debug.lib' } else { 'lib\shmrt-x86_64-windows.lib' })
Step (Tool shalimar) (@('-S', "$out\sieve.shl", '-o', "$out\sieve-shl-win.s", '--target=x86_64-windows') + $shwin) | Out-Null
Step (Tool masm) @('-t', 'x64', "$out\sieve-shl-win.s", '-o', "$out\sieve-shl-win.obj") | Out-Null
Step (Tool link) (@('/nologo', '/subsystem:console', "/out:$out\sieve-shl.exe", "$out\sieve-shl-win.obj", $shmwin) + $crt) | Out-Null
Same $expected (Step "$out\sieve-shl.exe" @())

Title "done: c90, cpp11, shalimar, c2s, masm, link, asm6x, lnk6x, rts6x, sim6747 and vm6747 all ran ($Configuration)"
