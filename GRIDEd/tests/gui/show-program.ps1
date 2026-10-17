# show-program.ps1 - one Shalimar example in GRIDEd, as a person would try it: the classic workspace opens with the
# program in the editor, the program is on screen for a few seconds, then Start Without Debugging (Ctrl+F5) builds and
# runs it, and what it printed is compared with its "// expected:" lines (in order: table's are a sample of its rows).
#   powershell -ExecutionPolicy Bypass -File show-program.ps1 -Name factorial [-Target Windows|TMS320C6747] [-Look 3]
param([Parameter(Mandatory)][string]$Name, [string]$Target = 'Windows', [int]$Look = 3)
$global:S = Join-Path $env:TEMP 'gride-show'
. "$PSScriptRoot\lib\session.ps1"
$work = Join-Path $S 'classic'
if (-not (Test-Path $work)) { New-Item -ItemType Directory -Force $S | Out-Null; Copy-Item -Recurse (Join-Path $PSScriptRoot '..\..\samples\shalimar\classic') $work }
$src = Join-Path $work "$Name\$Name.shl"
if (-not (Test-Path $src)) { "no example $Name"; exit 1 }

$grws = Join-Path $work 'classic.grws'
$ws = (Get-Content $grws -Raw) -replace '"startup":\s*"[^"]*"', "`"startup`": `"$Name`"" -replace '"activeTarget":\s*"[^"]*"', "`"activeTarget`": `"$Target`""
[IO.File]::WriteAllText($grws, $ws, (New-Object Text.UTF8Encoding $false))

KillAll
Start-GrideWith @($grws, $src)
Start-Sleep -Seconds $Look                       # the program body on screen before anything runs
Get-Output 'Build' | Out-Null
Focus-Gride; [System.Windows.Forms.SendKeys]::SendWait('^{F5}')
$o = Wait-Out 'returned \d+|status ?= ?\d+|exited|exit code \d+|error\(s\)' 120
Start-Sleep -Seconds 1
$build = Get-Output 'Build'
$want = @(Get-Content $src | Where-Object { $_ -match '// expected:' } | ForEach-Object { ($_ -split '// expected:', 2)[1].Trim() })
$lines = @($build -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
$k = 0; foreach ($l in $lines) { if ($k -lt $want.Count -and $l -eq $want[$k]) { $k++ } }
$summary = ($lines | Where-Object { $_ -match 'succeeded|failed|returned|status' }) -join ' | '
"{0} on {1}: {2}  ({3} of {4} expected lines seen)" -f $Name, $Target, $(if ($k -eq $want.Count) { 'PASS' } else { 'FAIL' }), $k, $want.Count
"   " + $summary
if ($k -ne $want.Count) { "   output: " + (($lines | Select-Object -Last 15) -join ' / ') }
