# The installed RIDE's window, qualified: the samples c-demo, cpp-shapes and shl-primes, copied out
# of <install>\projects, each built and run for the host and for tms6747 in Debug and Release - on
# tms6747 also Emulate on vm6747 and Verify. Each step is judged from the console, and a program's
# lines must be the same on every runner. Results: $Work\results.jsonl, summary.txt, done.
# Run on the logged-on desktop (run-on-box.sh starts it with schtasks /IT).
param([string]$Exe = "", [string]$Work = "$env:TEMP\ride-gui",
      [string[]]$Samples = @("c-demo", "cpp-shapes", "shl-primes"), [string[]]$Configs = @("Debug", "Release"))
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Work | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue "$Work\done", "$Work\error.txt"
# Whatever stops the round is written down and the round marked done, so the caller is not left waiting.
trap { ($_ | Out-String) | Set-Content "$Work\error.txt" -Encoding UTF8; "error" | Set-Content "$Work\done" -Encoding ASCII; exit 1 }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Exe) {
    # The newest RIDE installed: C:\Program Files\RIDE <version>\bin\RIDE.exe.
    $Exe = Get-ChildItem "$env:ProgramFiles\RIDE *\bin\RIDE.exe" | Sort-Object { [version](($_.Directory.Parent.Name -replace '^RIDE ', '') + '.0') } |
           Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $Exe -or -not (Test-Path $Exe)) { throw "no installed RIDE.exe" }
$install = Split-Path (Split-Path $Exe)

Get-Process RIDE -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe } | Stop-Process -Force
Start-Sleep -Seconds 1
foreach ($old in "$Work\work", "$Work\results.jsonl", "$Work\summary.txt") { if (Test-Path $old) { Remove-Item -Recurse -Force $old } }
$results = "$Work\results.jsonl"

# A copy per target: the host's as installed, tms6747's with "arch" set in its .pro.
$runs = @()
foreach ($s in $Samples) {
    foreach ($arch in @("host", "tms6747")) {
        $dir = "$Work\work\$s-$arch"
        Copy-Item -Recurse "$install\projects\$s" $dir
        $pro = Get-ChildItem "$dir\*.pro" | Select-Object -First 1
        if ($arch -eq "tms6747") {
            # A .pro may hold // comments, which ConvertFrom-Json refuses: the line goes in as text.
            $text = [IO.File]::ReadAllText($pro.FullName)
            $at = $text.IndexOf('{')
            [IO.File]::WriteAllText($pro.FullName, $text.Substring(0, $at + 1) + "`r`n  `"arch`": `"tms6747`"," + $text.Substring($at + 1))
        }
        $runs += [pscustomobject]@{ sample = $s; arch = $arch; pro = $pro.FullName }
    }
}

foreach ($r in $runs) {
    foreach ($c in $Configs) {
        $steps = if ($r.arch -eq "tms6747") { "build,run,emulate,verify" } else { "build,run" }
        & "$here\ride-window.ps1" -Exe $Exe -Project $r.pro -Config $c -Out $results -Steps $steps
    }
}

# The console is the pane whose text holds a "$ " command line; the longest such is taken.
function ConsoleOf($rec) {
    $best = ""
    foreach ($p in $rec.panes) { if ($p.text -match '(?m)^\$ ' -and $p.text.Length -gt $best.Length) { $best = $p.text } }
    return $best
}
# What the program printed: the lines between its command and "[program returned".
function ProgramLines([string]$text) {
    $lines = @(); $in = $false
    foreach ($l in ($text -split "`r?`n")) {
        if ($l -match '^\$ ') { $in = $true; $lines = @(); continue }
        if ($l -match '^\[program returned') { break }
        if ($in -and $l -notmatch '^\[') { $lines += $l.TrimEnd() }
    }
    return ($lines -join "`n").Trim()
}

$records = Get-Content $results -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json }
$summary = @(); $fails = 0; $outputs = @{}
foreach ($rec in $records) {
    if ($rec.error) { $summary += "FAIL $($rec.project) $($rec.step): $($rec.error)"; $fails++; continue }
    if ($rec.step -in @("open", "config")) {
        if ($rec.step -eq "config" -and $rec.how -ne "invoked") { $summary += "FAIL $($rec.project) config $($rec.config): $($rec.how)"; $fails++ }
        continue
    }
    $name = "{0} {1} {2}" -f (Split-Path (Split-Path $rec.project) -Leaf), $rec.config, $rec.step
    $text = ConsoleOf $rec
    $ok = $false; $why = ""
    if ($rec.how -ne "invoked") { $why = $rec.how }
    elseif ($rec.settled -lt 0) { $why = "did not settle" }
    else {
        switch ($rec.step) {
            "build"   { $ok = $text -match 'Build succeeded: .*, 0 errors'; $why = "no 'Build succeeded ... 0 errors'" }
            "run"     { $ok = $text -match '\[program returned 0\]'; $why = "no '[program returned 0]'" }
            "emulate" { $ok = ($text -match '(?m)^\$ vm6747 ') -and ($text -match '\[program returned 0\]'); $why = "no '`$ vm6747' with '[program returned 0]'" }
            "verify"  { $ok = $text -match '\[verify\] the two agree, line for line'; $why = "the two do not agree" }
        }
    }
    if ($ok -and $rec.step -in @("run", "emulate")) {
        $sample = (Split-Path (Split-Path $rec.project) -Leaf) -replace '-(host|tms6747)$', ''
        $lines = ProgramLines $text
        if (-not $outputs.ContainsKey($sample)) { $outputs[$sample] = @{ from = $name; lines = $lines } }
        elseif ($outputs[$sample].lines -ne $lines) { $ok = $false; $why = "prints other lines than $($outputs[$sample].from)" }
    }
    if ($ok) { $summary += "pass $name ($($rec.settled)s)" }
    else {
        $fails++
        $tail = (($text -split "`r?`n") | Where-Object { $_ } | Select-Object -Last 4) -join " / "
        $summary += "FAIL $name - $why; status: $($rec.status); console ends: $tail"
    }
}
$summary += "RIDE window test of $Exe : $($summary.Count - $fails) passed, $fails failed"
$summary | Set-Content "$Work\summary.txt" -Encoding UTF8
"$fails" | Set-Content "$Work\done" -Encoding ASCII
