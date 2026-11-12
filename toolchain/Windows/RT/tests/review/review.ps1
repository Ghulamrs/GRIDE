<#
  review.ps1 - the runtimes review's probes that found defects (docs\reviews\runtimes critical review - Fable 5.1.md,
  section 5), kept as tests: each source in review.list, run as its kind says.
    ucrt   ..\probe.ps1 at -O2, at -O0 and on the debug libraries: grt and Microsoft's CRT against the transcript
           the UCRT's run recorded (<name>.expected)
    grt    GRIDE's runtime alone, Release and Debug, against <name>.expected - where the UCRT ends the program
           (an invalid fopen mode) and GRIDE's runtimes answer, the recorded deviation
    debug  the Debug runtime alone (the Debug heap's timing)
  The C6747's half is toolchain\TI\RTS6x\tests\c6747.ps1, whose tests.list names the same sources where they run there.

    .\review.ps1 [-Bin <toolchain folder>]
#>
param([string]$Bin = "")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
$Libs = Join-Path $Bin 'lib\x86_64-windows'
$probe = Join-Path $here '..\probe.ps1'
$work = Join-Path $env:TEMP 'grt-review'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
}

$failed = 0; $runs = 0
foreach ($line in Get-Content (Join-Path $here 'review.list')) {
    if ($line.Trim() -eq '' -or $line.StartsWith('#')) { continue }
    $kind, $file = $line.Split(' ', 2)
    $src = Join-Path $here $file
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    if ($kind -eq 'ucrt') {
        foreach ($way in @(@{ Opt = '-O2' }, @{ Opt = '-O0' }, @{ DebugRuntime = $true })) {
            $runs++
            $label = if ($way.ContainsKey('Opt')) { $way.Opt } else { 'debug libraries' }
            try {
                $said = @(& $probe $src @way -Bin $Bin -Libs $Libs 2>&1 | Where-Object { "$_" -match 'the same as|differ|not linked' })
                $said | ForEach-Object { "  $_ ($label)" }
                if ($said.Count -eq 0 -or @($said | Where-Object { "$_" -notmatch 'the same as' }).Count -gt 0) { $failed++ }
            } catch { $failed++; "  $name ${label}: $($_.Exception.Message.Split("`n")[0])" }
        }
        continue
    }
    $want = ([IO.File]::ReadAllText([IO.Path]::ChangeExtension($src, '.expected'))).Replace("`r`n", "`n")
    $variants = if ($kind -eq 'debug') { @('d') } else { @('', 'd') }
    $compiler = if ([IO.Path]::GetExtension($src) -eq '.c') { 'c90' } else { 'cpp11' }
    foreach ($d in $variants) {
        $runs++
        $tag = "$name$d"
        try {
            Tool $compiler @('-S', $src, '-o', "$work\$tag.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
            Tool masm @('-t', 'x64', "$work\$tag.s", '-o', "$work\$tag.obj")
            $grt = @(@('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin') | ForEach-Object { Join-Path $Libs "$_$d.lib" }) + (Join-Path $Libs 'kernel32.lib')
            $saved = $env:LIB; $env:LIB = ''
            try { Tool link (@('/nologo', "/out:$work\$tag.exe", "$work\$tag.obj") + $grt) } finally { $env:LIB = $saved }
            $p = Start-Process -FilePath "$work\$tag.exe" -WorkingDirectory $work -NoNewWindow -Wait -PassThru `
                               -RedirectStandardOutput "$work\$tag.out" -RedirectStandardError "$work\$tag.err"
            $said = ((@(Get-Content "$work\$tag.out") + @(Get-Content "$work\$tag.err" | ForEach-Object { "stderr: $_" }) + "exit $($p.ExitCode)") -join "`n") + "`n"
            $which = if ($d) { 'grt Debug' } else { 'grt Release' }
            if ($said -ceq $want) { "  $name ${which}: the same as $name.expected" }
            else { $failed++; "  $name ${which}: DIFFERS from $name.expected"; [IO.File]::WriteAllText("$work\$tag.said", $said) }
        } catch { $failed++; "  $name ${d}: $($_.Exception.Message.Split("`n")[0])" }
    }
}
"review: $($runs - $failed) of $runs runs as expected"
exit $(if ($failed -gt 0) { 1 } else { 0 })
