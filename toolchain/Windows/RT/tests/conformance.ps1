<#
  conformance.ps1 - cpp11's conformance suite on GRIDE's runtime (docs\Runtime_Design.md 16, stage 2's exit test):
  every valid case of cpp11's tests\conformance\cases.tsv compiled by cpp11, assembled by GRIDE's masm and linked by
  GRIDE's link with GRIDE's runtime alone (LIB empty, the six libraries by path), run, and judged as the suite's own
  runner judges it on Microsoft's CRT: "pass" when the output and exit code are cl's recorded ones (check "run": the
  exit code alone). Each verdict is set beside the suite's baseline.tsv, the same cases on Microsoft's CRT; a case
  whose verdict differs between the two runtimes is listed.

    .\conformance.ps1 [-Filter <regex on set/group/case>] [-Levels -O0,-O2] [-Bin <toolchain>] [-Libs <folder>]
#>
param([string]$Filter = "", [string]$Levels = "-O0,-O2", [string]$Bin = "", [string]$Libs = "", [int]$Shards = 6,
      [int]$CompileSecs = 60, [int]$RunSecs = 20)
$ErrorActionPreference = 'Stop'
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib\x86_64-windows'; if (-not (Test-Path (Join-Path $Libs 'grtc.lib'))) { $Libs = Join-Path $Bin 'lib' } }
$suite = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\Compilers\C++Optimize\tests\conformance')).Path
$Out = Join-Path $env:TEMP 'grt-conformance'
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { "`"$(Join-Path $Libs "$_.lib")`"" }

$cases = @(Import-Csv (Join-Path $suite 'cases.tsv') -Delimiter "`t" | Where-Object { $_.kind -eq 'pos' -and (-not $Filter -or ("$($_.set)/$($_.group)/$($_.case)" -match $Filter)) })
$baseline = @{}
foreach ($b in (Import-Csv (Join-Path $suite 'baseline.tsv') -Delimiter "`t")) { $baseline["$($b.set)/$($b.group)/$($b.case)"] = $b }
"$($cases.Count) valid cases, $Bin"

$work = {
    param($mine, $suite, $Out, $Bin, $grt, $LevelsIn, $CompileSecs, $RunSecs)
    $env:LIB = ''
    $empty = Join-Path $Out "empty.stdin"; if (-not (Test-Path $empty)) { try { [IO.File]::WriteAllText($empty, "") } catch {} }
    function run($exe, $argList, $log, $secs) {
        $more = @{}; if (@($argList).Count -gt 0) { $more.ArgumentList = $argList }
        try {
            $p = Start-Process $exe @more -NoNewWindow -PassThru -WorkingDirectory $Out -RedirectStandardInput $empty -RedirectStandardOutput "$log.o" -RedirectStandardError "$log.e"
        } catch { return @{ rc = -998; text = "could not start: $_" } }
        $h = $p.Handle
        $timedOut = $false
        if (-not $p.WaitForExit($secs * 1000)) { $timedOut = $true; try { Stop-Process -Id $p.Id -Force } catch {}; Start-Sleep -Milliseconds 200 }
        else { $p.WaitForExit() }
        $text = ((Get-Content "$log.o", "$log.e" -Raw -ErrorAction SilentlyContinue) -join "")
        if ($null -eq $text) { $text = "" }
        Remove-Item "$log.o", "$log.e" -ErrorAction SilentlyContinue
        return @{ rc = $(if ($timedOut) { -999 } else { $p.ExitCode }); text = $text }
    }
    foreach ($c in $mine) {
        $dir = Join-Path $suite "pos\$($c.set)\$($c.group)"
        $tus = @(Join-Path $dir "$($c.case).cpp"); $part = Join-Path $dir "$($c.case).part.cpp"; if (Test-Path $part) { $tus += $part }
        $base = Join-Path $Out "$($c.set).$($c.group).$($c.case)"
        $row = [ordered]@{ set = $c.set; group = $c.group; case = $c.case }
        foreach ($lv in @($LevelsIn -split ',')) {
            $l = $lv.TrimStart('-')
            $objs = @(); $bad = $null; $t = 0
            foreach ($tu in $tus) {
                $t++; $s = "$base.$l.$t.s"; $o = "$base.$l.$t.obj"
                $r = run "$Bin\cpp11.exe" @("-nologo", "-S", "`"$tu`"", "-o", "`"$s`"", "-arch", "x86_64-windows", $lv) "$base.$l.$t.build" $CompileSecs
                if ($r.rc -eq -999) { $bad = "timeout"; break }
                if ($r.rc -lt 0 -or $r.rc -gt 1) { $bad = "crash"; break }
                if ($r.rc -ne 0 -or -not (Test-Path $s)) { $bad = "refused"; break }
                $r = run "$Bin\masm.exe" @("-t", "x64", "`"$s`"", "-o", "`"$o`"") "$base.$l.$t.asm" $CompileSecs
                Remove-Item $s -ErrorAction SilentlyContinue
                if (-not (Test-Path $o)) { $bad = "asm-fail"; break }
                $objs += "`"$o`""
            }
            if ($bad) { $row.$l = $bad; continue }
            $exe = "$base.$l.exe"
            $r = run "$Bin\link.exe" (@("/nologo", "/subsystem:console", "`"/out:$exe`"") + $objs + $grt) "$base.$l.link" $CompileSecs
            Remove-Item ($objs -replace '"', '') -ErrorAction SilentlyContinue
            if (-not (Test-Path $exe)) {
                $row.$l = "link-fail"
                $row."$l`_said" = (($r.text -split "`r?`n") | Where-Object { $_ -match 'error|unresolved' } | Select-Object -First 1)
                continue
            }
            $rr = run $exe @() "$base.$l.run" $RunSecs
            Remove-Item $exe -ErrorAction SilentlyContinue
            $want = [IO.File]::ReadAllText((Join-Path $dir "$($c.case).expected"))
            $okRc = ("$($rr.rc)" -eq "$($c.expect_rc)")
            if ($rr.rc -eq -999) { $row.$l = "timeout" }
            elseif ($c.check -eq "run") { $row.$l = $(if ($okRc) { "pass" } else { "differs" }) }
            elseif ($rr.text -ceq $want -and $okRc) { $row.$l = "pass" }
            else { $row.$l = "differs"; [IO.File]::WriteAllText("$base.$l.out", "exit $($rr.rc)`n" + $rr.text) }
        }
        [IO.File]::WriteAllText("$base.row.json", ($row | ConvertTo-Json -Compress))
    }
}
$jobs = @()
for ($k = 0; $k -lt $Shards; $k++) {
    $mine = @(); for ($i = $k; $i -lt $cases.Count; $i += $Shards) { $mine += $cases[$i] }
    if ($mine.Count -eq 0) { continue }
    $jobs += Start-Job -ScriptBlock $work -ArgumentList $mine, $suite, $Out, $Bin, $grt, $Levels, $CompileSecs, $RunSecs
}
$jobs | Wait-Job | Out-Null
$jobs | Receive-Job -ErrorAction SilentlyContinue | Out-Null
$jobs | Remove-Job

$lvls = @(@($Levels -split ',') | ForEach-Object { $_.TrimStart('-') })
$lines = @("set`tgroup`tcase`t" + (($lvls | ForEach-Object { "grt $_`tmsvc $_" }) -join "`t"))
$differ = @()
$count = @{}
foreach ($c in $cases) {
    $f = Join-Path $Out "$($c.set).$($c.group).$($c.case).row.json"
    $r = if (Test-Path $f) { Get-Content $f -Raw | ConvertFrom-Json } else { $null }
    $b = $baseline["$($c.set)/$($c.group)/$($c.case)"]
    $cells = @()
    foreach ($l in $lvls) {
        $g = if ($r) { $r.$l } else { 'missing' }
        $m = if ($b) { $b.$l } else { '' }
        $cells += "$g`t$m"
        $key = "$l $g"; $count[$key] = 1 + [int]$count[$key]
        if ($m -and $g -ne $m) { $differ += "$l $($c.set)/$($c.group)/$($c.case): grt $g, msvc $m  $(if ($r) { $r."$l`_said" })" }
    }
    $lines += "$($c.set)`t$($c.group)`t$($c.case)`t" + ($cells -join "`t")
}
$lines | Set-Content (Join-Path $Out 'results.tsv')
foreach ($l in $lvls) { "$l`: " + ((@($count.Keys | Where-Object { $_ -like "$l *" } | Sort-Object) | ForEach-Object { "$($_.Substring($l.Length + 1))=$($count[$_])" }) -join ", ") }
"verdicts that differ from Microsoft's CRT: $($differ.Count)"
$differ | ForEach-Object { "  $_" }
"results: $(Join-Path $Out 'results.tsv')"
if ($differ.Count -gt 0) { exit 1 }
