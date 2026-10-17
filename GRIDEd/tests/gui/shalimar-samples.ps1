# shalimar-samples.ps1 - RIDE's Shalimar examples (samples\shalimar) through GRIDEd: the workspace rebuilt in Debug and
# Release for Windows and TMS320C6747, then every program it built run - the .exe directly, the .out on sim6747 - and
# its output compared with what the example prints. Exit code: the number of failures.
param([string]$Gride)
if ($Gride) { $global:GrideExe = [IO.Path]::GetFullPath($Gride) }
$global:S = Join-Path $env:TEMP 'gride-shalimar-samples'
. "$PSScriptRoot\lib\session.ps1"
$bin = Split-Path $GrideExe
$work = Join-Path $S 'shalimar'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $S | Out-Null
Copy-Item -Recurse (Join-Path $PSScriptRoot '..\..\samples\shalimar') $work

# what each prints: RIDE's "// expected:" lines where the example has them, otherwise its known output
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
$results = New-Object System.Collections.Generic.List[object]
function Note($what, $ok, $saw) { $results.Add([pscustomobject]@{ Check = $what; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Saw = "$saw".Trim() }) }

# the classic set (classic\, RIDE's programs\*.shm): what each prints is its own "// expected:" lines, in order -
# for table a sample of its rows, so they are matched as lines that appear in that order
$classic = @{}
foreach ($d in Get-ChildItem "$work\classic" -Directory) {
    $classic[$d.Name] = @(Get-Content "$($d.FullName)\$($d.Name).shl" | Where-Object { $_ -match '// expected:' } | ForEach-Object { ($_ -split '// expected:', 2)[1].Trim() })
}
function InOrder($lines, $want) {
    $k = 0
    foreach ($l in $lines) { if ($k -lt $want.Count -and $l -eq $want[$k]) { $k++ } }
    return $k -eq $want.Count
}

$sets = @(
    @{ Label = ''; Dir = $work; Ws = 'shalimar.grws'; Expect = $expect; Exact = $true },
    @{ Label = 'classic '; Dir = "$work\classic"; Ws = 'classic.grws'; Expect = $classic; Exact = $false }
)
foreach ($set in $sets) {
  foreach ($target in 'Windows', 'TMS320C6747') {
    foreach ($config in 'Debug', 'Release') {
        KillAll
        $grws = Join-Path $set.Dir $set.Ws
        $ws = Get-Content $grws -Raw
        $ws = $ws -replace '"activeConfig":\s*"[^"]*"', "`"activeConfig`": `"$config`"" -replace '"activeTarget":\s*"[^"]*"', "`"activeTarget`": `"$target`""
        [IO.File]::WriteAllText($grws, $ws, (New-Object Text.UTF8Encoding $false))
        # the workspace and its startup project's program file open, so the build is seen against its source
        $startup = ($ws | ConvertFrom-Json).startup
        $proFile = Join-Path $set.Dir "$startup\$startup.pro"
        $program = (Get-Content $proFile -Raw | ConvertFrom-Json).folders.Sources[0]
        Start-GrideWith @($grws, (Join-Path (Split-Path $proFile) $program))
        Menu 'Build', 'Rebuild Workspace'
        $o = Wait-Out '\d+ succeeded, \d+ failed' 300
        $summary = ($o -split "`r?`n" | Where-Object { $_ -match 'succeeded, \d+ failed' } | Select-Object -Last 1)
        Note "$($set.Label)build $config $target" ($summary -match "$($set.Expect.Count) succeeded, 0 failed") $summary
        KillAll
        foreach ($name in $set.Expect.Keys | Sort-Object) {
            $prog = Get-ChildItem "$($set.Dir)\$name\out\$config" -Recurse -File -ErrorAction SilentlyContinue |
                    Where-Object { $_.Extension -eq $(if ($target -eq 'Windows') { '.exe' } else { '.out' }) } | Select-Object -First 1
            if (-not $prog) { Note "$($set.Label)run $name $config $target" $false 'no program built'; continue }
            $said = if ($target -eq 'Windows') { & $prog.FullName 2>&1 } else { & "$bin\sim6747.exe" --run -c $prog.FullName 2>$null }
            $lines = @($said | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
            $want = @($set.Expect[$name] | ForEach-Object { $_.Trim() })
            $ok = if ($set.Exact) { ($lines -join "`n") -eq ($want -join "`n") } else { InOrder $lines $want }
            Note "$($set.Label)run $name $config $target" $ok $(if ($ok) { "$($lines.Count) lines as expected" } else { ($lines -join ' / ') })
        }
    }
  }
}
KillAll
$results | Format-Table -AutoSize -Wrap | Out-String -Width 220 | Write-Host
$failed = @($results | Where-Object Result -eq 'FAIL').Count
Write-Host ("{0} checks, {1} failed" -f $results.Count, $failed)
exit $failed
