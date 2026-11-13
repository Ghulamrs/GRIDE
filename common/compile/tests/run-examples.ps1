# Builds the example projects from the block's plans - gride-plan.exe -build, which runs the steps as an outlook
# would - and runs what was built. Where Black's program of the same build is at hand (the census's working
# copies under %TEMP%\gcensus\w\Black), the two programs' output is compared.
#
#   run-examples.ps1 -Tools <folder with the tools and toolchain.json> [-Only c-bank,cpp-*] [-Patterns]
#
# -Patterns rewrites each copy's .pro with its name lists as patterns first (compile-tests' rule), so the same
# builds are made from projects that list no file by name.
param(
    [Parameter(Mandatory = $true)][string]$Tools,
    [string]$Examples = (Join-Path $env:TEMP "gcensus\examples\projects"),
    [string]$Black = (Join-Path $env:TEMP "gcensus\w\Black"),
    [string]$Work = (Join-Path $env:TEMP "gride-plan-examples"),
    [string[]]$Only = @(),
    [switch]$Patterns
)
Import-Module Microsoft.PowerShell.Utility
$ErrorActionPreference = "Stop"
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })      # -File hands a list over as one word
$plan = Join-Path $PSScriptRoot "..\x64\Release\gride-plan.exe"
$sim = Join-Path $Tools "sim6747.exe"
if (-not (Test-Path $plan)) { throw "gride-plan.exe is not built: $plan" }
if (Test-Path $Work) { Remove-Item $Work -Recurse -Force }
New-Item -ItemType Directory $Work | Out-Null

function Run-Program([string]$program, [string]$folder, [string[]]$arguments) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    if ($program -like "*.out") { $psi.FileName = $sim; $psi.Arguments = "--run -c `"$program`"" }
    else { $psi.FileName = $program; $psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join " " }
    $psi.WorkingDirectory = $folder
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    $p = [Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $out = $p.StandardOutput.ReadToEndAsync()
    $err = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(60000)) { try { $p.Kill() } catch { }; return @{ Code = -999; Text = "(did not end in 60 s)" } }
    # the simulator's own count of cycles and packets is not the program's output: it moves with the order of the
    # objects in the link and with the length of a path the program holds (__FILE__)
    $text = (($out.Result + $err.Result) -split "`r?`n" | Where-Object { $_ -notmatch '^CYCLES count=' }) -join "`n"
    # nor is the folder the program was run in, which one of the examples prints
    $text = $text.Replace($folder, "<folder>").Replace($folder.Replace('\', '/'), "<folder>")
    return @{ Code = $p.ExitCode; Text = $text }
}

$rows = @()
$pros = Get-ChildItem $Examples -Recurse -Filter *.pro | Where-Object { $_.FullName -notmatch '\\(obj|out|build)\\' } | Sort-Object FullName
foreach ($pro in $pros) {
    $json = Get-Content $pro.FullName -Raw | ConvertFrom-Json
    $rel = $pro.DirectoryName.Substring($Examples.Length).TrimStart('\')
    $id = ($rel -replace '\\', '_')
    if ($pros | Where-Object { $_.DirectoryName -eq $pro.DirectoryName -and $_.Name -ne $pro.Name }) { $id = $id + "_" + $pro.BaseName }
    if ($Only.Count -gt 0 -and -not ($Only | Where-Object { $id -like $_ })) { continue }
    $targets = @("x86_64-windows", "tms6747")
    if ($json.arch) { $targets = @($json.arch) | Where-Object { $_ -in "x86_64-windows", "tms6747" } }
    $arguments = @()
    if ($json.build -and $json.build.args) { $arguments = @($json.build.args) }
    foreach ($target in $targets) {
        foreach ($config in "Debug", "Release") {
            $case = "$id.$config.$target"
            # in a folder of the project's own name, as the census has it: a program may print its folder's name
            New-Item -ItemType Directory (Join-Path $Work $case) | Out-Null
            $copy = Join-Path (Join-Path $Work $case) $pro.Directory.Name
            Copy-Item $pro.DirectoryName $copy -Recurse
            foreach ($d in "obj", "out", "build") { $x = Join-Path $copy $d; if (Test-Path $x) { Remove-Item $x -Recurse -Force } }
            $file = Join-Path $copy $pro.Name
            if ($Patterns) {
                $tests = Join-Path $PSScriptRoot "..\x64\Release\compile-tests.exe"
                & $tests rewrite $file (Join-Path $Tools "toolchain.json") | Out-Null
            }
            $log = & $plan $file -config $config -target $target -tools $Tools -no-settings -rebuild -build 2>&1
            $built = $LASTEXITCODE
            $programLine = & $plan $file -config $config -target $target -tools $Tools -no-settings -steps | Where-Object { $_ -like "program`t*" }
            $program = if ($programLine) { ($programLine -split "`t")[1] } else { "" }
            $row = [ordered]@{ Case = $case; Build = $(if ($built -eq 0) { "built" } else { "FAILED" }); Run = ""; Black = "" }
            if ($built -eq 0 -and $program -and (Test-Path $program)) {
                $mine = Run-Program $program $copy $arguments
                $row.Run = "exit " + $mine.Code
                $theirs = Join-Path (Join-Path $Black $case) ($pro.Directory.Name + $program.Substring($copy.Length))
                if (Test-Path $theirs) {
                    $their = Run-Program $theirs (Join-Path (Join-Path $Black $case) $pro.Directory.Name) $arguments
                    $row.Black = if ($their.Code -eq $mine.Code -and $their.Text -eq $mine.Text) { "same output" } else { "OUTPUT DIFFERS (Black exit " + $their.Code + ")" }
                } else { $row.Black = "(Black built none)" }
            } elseif ($built -ne 0) {
                $row.Run = (($log | Select-Object -Last 4) -join " | ")
                if ($row.Run.Length -gt 150) { $row.Run = $row.Run.Substring(0, 150) }
            }
            $rows += New-Object psobject -Property $row
        }
    }
}
$rows | Format-Table Case, Build, Run, Black -AutoSize | Out-String -Width 250
"{0} builds: {1} built, {2} failed; {3} programs with the same output as Black's, {4} that differ, {5} Black has no program for" -f `
    $rows.Count, @($rows | Where-Object Build -eq "built").Count, @($rows | Where-Object Build -eq "FAILED").Count,
    @($rows | Where-Object Black -eq "same output").Count, @($rows | Where-Object Black -like "OUTPUT*").Count, @($rows | Where-Object Black -like "(Black*").Count
