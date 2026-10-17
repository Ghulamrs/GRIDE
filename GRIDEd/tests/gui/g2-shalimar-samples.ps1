# g2-shalimar-samples.ps1 - RIDE's Shalimar examples as GRIDE 2.0 projects (..\GRIDE\samples\shalimar): each one rebuilt
# in GRIDE 2.0 for Windows and TMS6747 (Debug), its program then run - the .exe directly, the .out on sim6747 - and the
# output compared with what the example prints. Exit code: the number of failures.
param([string]$Gride = (Join-Path $PSScriptRoot '..\..\..\GRIDE 2.0\x64\Release\GRIDE.exe'))
$global:GrideExe = $Gride
$global:S = Join-Path $env:TEMP 'gride2-shalimar-samples'
. "$PSScriptRoot\lib\session.ps1"
$bin = Split-Path $GrideExe
$work = Join-Path $S 'shalimar'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $S | Out-Null
Copy-Item -Recurse (Join-Path (Split-Path $bin -Parent | Split-Path -Parent) 'samples\shalimar') $work

$expect = @{
    'hello'      = @('Hello from Shalimar')
    'gcd'        = @('gcd of 48 18', 'is 6')
    'table'      = @('1 2 3 4 5', '2 4 6 8 10', '3 6 9 12 15', '4 8 12 16 20', '5 10 15 20 25')
    'primes'     = @('primes below 60', '2', '3', '5', '7', '11', '13', '17', '19', '23', '29', '31', '37', '41', '43', '47', '53', '59', 'done')
    'rotmat'     = @('yaw 90', '0.000000   1.000000   0.000000', '-1.000000   0.000000   0.000000', '0.000000   0.000000   1.000000',
                     'roll 90', '1.000000   0.000000   0.000000', '0.000000   0.000000   1.000000', '0.000000  -1.000000   0.000000',
                     'roll pitch yaw 90', '0.000000   0.000000   1.000000', '-0.000000  -1.000000   0.000000', '1.000000  -0.000000   0.000000')
    'shl-matrix' = @('trace a: 3.0000000', 'trace a*a: 3.0000000', 'row 0: 1.0000000 4.0000000 6.0000000')
    'shl-primes' = @('twins 3 5', 'twins 5 7', 'twins 11 13', 'twins 17 19', 'twins 29 31', 'twins 41 43', 'primes below 50: 15', 'gcd 1071 462: 21')
}
# GRIDE 2.0's Output pane is a plain EDIT control, read while hidden too
function AllText { (All-Windows | Where-Object { $_.Cls -match '\.EDIT\.' } | ForEach-Object { [W32]::Text($_.H) }) -join "`n" }
function WaitFor($pattern, $seconds) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        Start-Sleep -Milliseconds 300
        $t = AllText; if ($t -match $pattern) { return ($t -split "`r?`n" | Where-Object { $_ -match $pattern } | Select-Object -Last 1) }
    }
    "[TIMEOUT $pattern]"
}
$results = New-Object System.Collections.Generic.List[object]
function Note($what, $ok, $saw) { $results.Add([pscustomobject]@{ Check = $what; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Saw = "$saw".Trim() }) }

# the classic set (classic\, RIDE's programs\*.shm): its own "// expected:" lines, matched in order (table's are a sample)
$classic = @{}
foreach ($d in Get-ChildItem "$work\classic" -Directory) {
    $classic["classic\" + $d.Name] = @(Get-Content "$($d.FullName)\$($d.Name).shl" | Where-Object { $_ -match '// expected:' } | ForEach-Object { ($_ -split '// expected:', 2)[1].Trim() })
}
function InOrder($lines, $want) {
    $k = 0
    foreach ($l in $lines) { if ($k -lt $want.Count -and $l -eq $want[$k]) { $k++ } }
    return $k -eq $want.Count
}
foreach ($c in $classic.Keys) { $expect[$c] = $classic[$c] }

foreach ($target in 'Windows', 'TMS6747') {
    foreach ($name in $expect.Keys | Sort-Object) {
        $pro = "$work\$name\$(Split-Path $name -Leaf).pro"
        $j = [IO.File]::ReadAllText($pro) -replace '"target":\s*"[^"]*"', "`"target`": `"$target`""
        [IO.File]::WriteAllText($pro, $j, (New-Object Text.UTF8Encoding $false))
        KillAll
        # the project and its program file open, so the build is seen against its source
        $program = ([IO.File]::ReadAllText($pro) | ConvertFrom-Json).sourceFiles[0]
        Start-GrideWith @($pro, (Join-Path (Split-Path $pro) $program))
        Menu 'Build', 'Rebuild Project'
        $b = WaitFor '=+ \w+: \d+ succeeded, \d+ failed' 120
        Note "build $name $target" ($b -match '1 succeeded, 0 failed') $b
        KillAll
        $prog = Get-ChildItem "$work\$name\build" -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -eq $(if ($target -eq 'Windows') { '.exe' } else { '.out' }) } | Select-Object -First 1
        if (-not $prog) { Note "run $name $target" $false 'no program built'; continue }
        $said = if ($target -eq 'Windows') { & $prog.FullName 2>&1 } else { & "$bin\sim6747.exe" --run -c $prog.FullName 2>$null }
        $lines = @($said | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
        $want = @($expect[$name] | ForEach-Object { $_.Trim() })
        $ok = if ($name -like 'classic\*') { InOrder $lines $want } else { ($lines -join "`n") -eq ($want -join "`n") }
        Note "run $name $target" $ok $(if ($ok) { "$($lines.Count) lines as expected" } else { ($lines -join ' / ') })
    }
}
KillAll
$results | Format-Table -AutoSize -Wrap | Out-String -Width 220 | Write-Host
$failed = @($results | Where-Object Result -eq 'FAIL').Count
Write-Host ("{0} checks, {1} failed" -f $results.Count, $failed)
exit $failed
