# The census: what each outlook runs today when it builds an example project. See README.md beside this file.
#
#   census.ps1 -Prepare                                   build the recorder, make the recording copies of the outlooks
#   census.ps1 -Outlook Black -Project c-bank\c-bank.pro -Config Debug -Target x86_64-windows     one case
#   census.ps1 -All [-Outlook Grey] [-Only c-bank,cpp-shapes] [-Force]                            the baseline
#   census.ps1 -Renormalise                               the .txt files and summary.tsv again, from the raw logs
#   census.ps1 -RestoreSettings                           the user's GRIDE settings put back as they were
#
# Windows PowerShell 5.1. Nothing is written into the repository but the normalised results under -Baseline.
param(
    [string]$Outlook = "",                      # Black, Grey or White; with -All: empty means the three
    [string]$Project = "",                      # relative to <Root>\examples\projects: c-bank\c-bank.pro, ccs\Hello
    [ValidateSet("Debug", "Release")][string]$Config = "Debug",
    [ValidateSet("x86_64-windows", "tms6747")][string]$Target = "x86_64-windows",
    [switch]$All,
    [string[]]$Only = @(),                      # with -All: only the projects whose id matches one of these (wildcards)
    [switch]$Force,                             # with -All: run a case again although its result is there
    [switch]$Prepare,
    [switch]$Renormalise,
    [switch]$RestoreSettings,
    [switch]$Visible,                           # drive the outlook on the user's own desktop, not on a separate one
    [switch]$Inner,                             # (the script calling itself on the separate desktop)
    [string]$Root = (Join-Path $env:TEMP "gcensus"),
    [string]$Baseline = "",
    [string]$MenuItem = "",                      # the Build menu item to choose instead of the full build (a regular expression: "^Dry Run")
    [switch]$KeepCopy,                          # build in the working copy the run before left, not in a fresh one: a second build
    [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = "Stop"
# Loaded by name before the first Add-Type: a file called "Add-Type" on the PATH (one was found in system32 on the
# machine this was written on) is otherwise run in the cmdlet's place, silently, while this module is not yet loaded.
Import-Module Microsoft.PowerShell.Utility
if (-not $Baseline) { $Baseline = Join-Path $PSScriptRoot "baseline" }

$Tools = @("c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747")
$OutlookNames = @("Black", "Grey", "White")
$OutlookTitles = @{ Black = "Black (GRIDEd)"; Grey = "Grey (GRIDE 2.0)"; White = "White (GRIDE 1.0)" }
$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
# the projects whose results are wanted first; the rest follow in the order of their names
$First = @("c-bank", "cpp-shapes", "shl-primes", "showcase_average-c90", "showcase_solve3-cpp11", "showcase_solve3-shalimar",
           "cpp-table", "thirdparty-mathx_mathx-windows", "cpp-sampleext", "ccs_Hello", "cppparse")
$Skipped = @("thirdparty-mathx_mathx-macos")
$SettingsLive = @{ APPDATA_GRIDE = (Join-Path $env:APPDATA "GRIDE"); USERPROFILE_gride = (Join-Path $env:USERPROFILE ".gride") }

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
if (-not ("CensusWin" -as [type])) {
Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class CensusWin {
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, StringBuilder l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutN(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);

    // the visible top-level windows of a process, front to back
    public static IntPtr[] Windows(int pid) {
        var found = new List<IntPtr>();
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == (uint)pid && IsWindowVisible(h)) found.Add(h); return true; }, IntPtr.Zero);
        return found.ToArray();
    }
    public static IntPtr[] Children(IntPtr parent) {
        var found = new List<IntPtr>();
        EnumChildWindows(parent, (h, l) => { found.Add(h); return true; }, IntPtr.Zero);
        return found.ToArray();
    }
    public static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
    public static bool HasCaption(IntPtr h) { return ((long)GetWindowLongPtr(h, -16) & 0x00C00000L) == 0x00C00000L; }
    // a window's own text (WM_GETTEXT), which for an edit control is all it holds
    public static string Text(IntPtr h, int most) {
        IntPtr r;
        if (SendMessageTimeoutN(h, 0x000E, IntPtr.Zero, IntPtr.Zero, 2, 2000, out r) == IntPtr.Zero) return "";
        int n = Math.Min((int)r, most);
        var s = new StringBuilder(n + 2);
        if (SendMessageTimeoutW(h, 0x000D, (IntPtr)(n + 1), s, 2, 3000, out r) == IntPtr.Zero) return "";
        return s.ToString();
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct PROCESSENTRY32 { public uint dwSize, cntUsage, th32ProcessID; public IntPtr th32DefaultHeapID; public uint th32ModuleID, cntThreads, th32ParentProcessID; public int pcPriClassBase; public uint dwFlags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szExeFile; }
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool Process32FirstW(IntPtr snap, ref PROCESSENTRY32 e);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool Process32NextW(IntPtr snap, ref PROCESSENTRY32 e);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    // every process below `root`, as "pid|name"
    public static string[] Descendants(int root) {
        var parent = new Dictionary<uint, uint>(); var name = new Dictionary<uint, string>();
        IntPtr snap = CreateToolhelp32Snapshot(2, 0);
        if (snap == (IntPtr)(-1)) return new string[0];
        var e = new PROCESSENTRY32(); e.dwSize = (uint)Marshal.SizeOf(typeof(PROCESSENTRY32));
        if (Process32FirstW(snap, ref e)) do { parent[e.th32ProcessID] = e.th32ParentProcessID; name[e.th32ProcessID] = e.szExeFile; } while (Process32NextW(snap, ref e));
        CloseHandle(snap);
        var found = new List<string>(); var seen = new HashSet<uint>(); var wave = new List<uint>(); wave.Add((uint)root); seen.Add((uint)root);
        while (wave.Count > 0) {
            var next = new List<uint>();
            foreach (var kv in parent) if (wave.Contains(kv.Value) && seen.Add(kv.Key)) { next.Add(kv.Key); found.Add(kv.Key + "|" + name[kv.Key]); }
            wave = next;
        }
        return found.ToArray();
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO { public int cb; public string lpReserved; public string lpDesktop; public string lpTitle; public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError; }
    [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr devmode, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseDesktop(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, uint code);
    // a command line run on a desktop of its own, which nobody sees and which takes no keys from the user
    public static string RunOnDesktop(string desktop, string commandLine, int timeoutMs) {
        IntPtr d = CreateDesktopW(desktop, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);
        if (d == IntPtr.Zero) return "CreateDesktop failed: " + Marshal.GetLastWin32Error();
        try {
            var si = new STARTUPINFO(); si.cb = Marshal.SizeOf(typeof(STARTUPINFO)); si.lpDesktop = desktop;
            PROCESS_INFORMATION pi;
            if (!CreateProcessW(null, new StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, false, 0x08000000, IntPtr.Zero, null, ref si, out pi))
                return "CreateProcess failed: " + Marshal.GetLastWin32Error();
            string said;
            if (WaitForSingleObject(pi.hProcess, (uint)timeoutMs) != 0) { TerminateProcess(pi.hProcess, 99); said = "timed out"; }
            else { uint code; GetExitCodeProcess(pi.hProcess, out code); said = "exit " + code; }
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return said;
        } finally { CloseDesktop(d); }
    }
}
'@
}

# ------------------------------------------------------------------------------------------------ the cases
function Get-ProjectInfo([string]$rel) {
    $src = Join-Path (Join-Path $Root "examples\projects") $rel
    if (-not (Test-Path $src)) { throw "no such project: $src" }
    $info = @{ Rel = $rel; IsCcs = $false; Arch = ""; ProFile = "" }
    if (Test-Path $src -PathType Container) {
        if (-not (Test-Path (Join-Path $src ".project"))) { throw "$src is a folder with no .project: name the .pro" }
        $info.IsCcs = $true
        $info.Dir = (Resolve-Path $src).Path
        $info.Id = ($rel.Trim('\') -replace '[\\/]', '_')
        $m = [regex]::Match((Get-Content (Join-Path $src ".project") -Raw), '<name>([^<]*)</name>')
        $info.Name = $m.Groups[1].Value
        $info.Arch = "tms6747"
    } else {
        $info.Dir = Split-Path (Resolve-Path $src).Path
        $info.ProFile = Split-Path $src -Leaf
        $folder = (Split-Path $rel) -replace '[\\/]', '_'
        $base = [IO.Path]::GetFileNameWithoutExtension($src)
        # the folder names the project, with the .pro's own name after it where the folder holds more than one
        if (@(Get-ChildItem $info.Dir -Filter *.pro).Count -gt 1) { $info.Id = $folder + "_" + $base } else { $info.Id = $folder }
        $text = Get-Content $src -Raw
        $m = [regex]::Match($text, '"name"\s*:\s*"([^"]*)"')
        if ($m.Success) { $info.Name = $m.Groups[1].Value } else { $info.Name = $base }
        $m = [regex]::Match($text, '"arch"\s*:\s*"([^"]*)"')
        if ($m.Success) { $info.Arch = $m.Groups[1].Value }
    }
    $info.Leaf = Split-Path $info.Dir -Leaf
    return $info
}

function Get-Projects {
    $base = Join-Path $Root "examples\projects"
    $all = @()
    foreach ($f in Get-ChildItem $base -Recurse -Filter *.pro) { $all += Get-ProjectInfo $f.FullName.Substring($base.Length + 1) }
    foreach ($f in Get-ChildItem $base -Recurse -Force -Filter .project) { $all += Get-ProjectInfo (Split-Path $f.FullName).Substring($base.Length + 1) }
    $all = @($all | Where-Object { $Skipped -notcontains $_.Id })
    $ordered = @()
    foreach ($id in $First) { $ordered += @($all | Where-Object { $_.Id -eq $id }) }
    $ordered += @($all | Where-Object { $First -notcontains $_.Id } | Sort-Object { $_.Id })
    return $ordered
}

# the targets a project is built for: both, or the one its "arch" names
function Get-Targets($info) {
    if (-not $info.Arch) { return @("x86_64-windows", "tms6747") }
    if ($info.Arch -match '6747') { return @("tms6747") }
    if ($info.Arch -match 'windows') { return @("x86_64-windows") }
    return @()
}

# ------------------------------------------------------------------------------------------------ preparing
function Invoke-Prepare {
    foreach ($o in $OutlookNames) { if (-not (Test-Path (Join-Path $Root "base\$o\GRIDE.exe"))) { throw "no frozen copy of $o under $Root\base" } }
    $shim = Join-Path $Root "shim\shim.exe"
    & $MSBuild (Join-Path $PSScriptRoot "shim\shim.vcxproj") /nologo /v:m /p:Configuration=Release /p:Platform=x64 "/p:CensusOut=$Root\shim\" | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $shim)) { throw "the recorder did not build" }
    foreach ($o in $OutlookNames) {
        $dst = Join-Path $Root "bin\$o"
        if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
        Copy-Item (Join-Path $Root "base\$o") $dst -Recurse
        foreach ($t in $Tools) { Rename-Item (Join-Path $dst "$t.exe") "$t.real.exe"; Copy-Item $shim (Join-Path $dst "$t.exe") }
        "$o : $dst"
    }
    Backup-Settings
}

function Backup-Settings {
    $b = Join-Path $Root "backup"
    if (Test-Path $b) { "the settings were backed up before: $b (kept)"; return }
    New-Item -ItemType Directory -Force $b | Out-Null
    foreach ($k in $SettingsLive.Keys) { if (Test-Path $SettingsLive[$k]) { Copy-Item $SettingsLive[$k] (Join-Path $b $k) -Recurse } }
    "settings backed up to $b"
}

# the user's settings as they were before the first run: before every run, and at the very end
function Restore-Settings {
    $b = Join-Path $Root "backup"
    if (-not (Test-Path $b)) { throw "no backup of the settings under $b - run -Prepare first" }
    foreach ($k in $SettingsLive.Keys) {
        $live = $SettingsLive[$k]
        if (Test-Path $live) { Remove-Item $live -Recurse -Force }
        if (Test-Path (Join-Path $b $k)) { Copy-Item (Join-Path $b $k) $live -Recurse }
    }
}

function Stop-Strays {
    $prefix = (Join-Path $Root "bin") + "\"
    $n = 0
    foreach ($p in Get-Process) {
        $path = $null
        try { $path = $p.Path } catch { }
        if ($path -and $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { try { Stop-Process -Id $p.Id -Force; $n++ } catch { } }
    }
    return $n
}

# ------------------------------------------------------------------------------------------------ small JSON edits
# a top-level string key of a JSON text set, the rest of the text left as it is (a .pro may hold // comments)
function Set-JsonString([string]$text, [string]$key, [string]$value) {
    $pattern = '("' + [regex]::Escape($key) + '"\s*:\s*)"[^"]*"'
    $escaped = $value.Replace('\', '\\').Replace('"', '\"')
    if ([regex]::IsMatch($text, $pattern)) { return [regex]::Replace($text, $pattern, { param($m) $m.Groups[1].Value + '"' + $escaped + '"' }, 1) }
    $at = $text.IndexOf('{')
    return $text.Substring(0, $at + 1) + "`r`n  `"$key`": `"$escaped`"," + $text.Substring($at + 1)
}
function Write-Utf8([string]$path, [string]$text) { [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false))) }

# ------------------------------------------------------------------------------------------------ one run, inside
$script:DriveLog = $null
function Say([string]$line) {
    $stamp = (Get-Date).ToString("HH:mm:ss.fff")
    if ($script:DriveLog) { Add-Content -Path $script:DriveLog -Value "$stamp  $line" -Encoding UTF8 }
}

function Read-Shared([string]$path) {
    if (-not (Test-Path $path)) { return "" }
    $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try { $r = New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8); return $r.ReadToEnd() } finally { $fs.Dispose() }
}

function Get-ToolProcesses([int]$root) {
    $names = @()
    foreach ($t in $Tools) { $names += "$t.exe"; $names += "$t.real.exe" }
    return @([CensusWin]::Descendants($root) | Where-Object { $names -contains ($_.Split('|')[1].ToLowerInvariant()) })
}

$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$AnyElement = [System.Windows.Automation.Condition]::TrueCondition

# The boxes an outlook put up (a window of its own with a caption, other than the main one): what each says, and No /
# Cancel / OK pressed, in that order of preference, so that nothing waits on a person. Returns what was seen.
function Resolve-Dialogs([int]$procId, [IntPtr]$mainH) {
    $seen = @()
    foreach ($w in [CensusWin]::Windows($procId)) {
        if ($w -eq $mainH -or -not [CensusWin]::HasCaption($w)) { continue }
        $title = [CensusWin]::Text($w, 400)
        $texts = @(); $buttons = @{}
        foreach ($c in [CensusWin]::Children($w)) {
            $cls = [CensusWin]::Class($c); $t = [CensusWin]::Text($c, 2000)
            if ($cls -match 'BUTTON') { if ($t) { $buttons[($t -replace '&', '')] = $c } }
            elseif ($t) { $texts += $t }
        }
        $pressed = ""
        foreach ($b in @("No", "Cancel", "OK", "Close")) { if (-not $pressed -and $buttons.ContainsKey($b)) { [void][CensusWin]::PostMessage($buttons[$b], 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero); $pressed = $b } }
        if (-not $pressed) { [void][CensusWin]::PostMessage($w, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); $pressed = "(closed)" }
        $said = "'$title': " + (($texts -join ' / ') -replace '\s+', ' ') + "  [buttons: " + (($buttons.Keys | Sort-Object) -join ', ') + "; pressed: $pressed]"
        Say "dialog $said"
        $seen += $said
        Start-Sleep -Milliseconds 400
    }
    return $seen
}

# Menu > item, by keys posted to the open menu's own window (a click through UI Automation would not return while a
# box it opens is up, and the keys need neither the focus nor the user's keyboard). "" when done, else why not.
function Invoke-Menu([int]$procId, [IntPtr]$mainH, [string]$menuName, [string]$itemPattern) {
    # a menu left open by an earlier try is closed first
    foreach ($w in [CensusWin]::Windows($procId)) {
        if ($w -ne $mainH -and -not [CensusWin]::HasCaption($w)) {
            [void][CensusWin]::PostMessage($w, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero); [void][CensusWin]::PostMessage($w, 0x0101, [IntPtr]0x1B, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 300
        }
    }
    $main = $AE::FromHandle($mainH)
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::MenuItem)
    $top = $null; $pattern = $null
    foreach ($e in $main.FindAll($TS::Descendants, $cond)) {
        if ($top -or ($e.Current.Name -replace '&', '') -ne $menuName) { continue }
        $got = $null
        if ($e.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$got)) { $top = $e; $pattern = $got }
    }
    if (-not $top) { return "no $menuName menu" }
    $pattern.Expand()
    Start-Sleep -Milliseconds 500
    # Down walks over every item, the greyed ones too, and over no separator
    $downs = 0; $found = $null; $names = @()
    foreach ($e in $top.FindAll($TS::Children, $AnyElement)) {
        if ($e.Current.ControlType -ne $CT::MenuItem) { continue }
        $n = ($e.Current.Name -replace '&', '')
        $names += $n
        $downs++
        if ($n -match $itemPattern) { if ($e.Current.IsEnabled) { $found = $n } else { $found = "disabled" }; break }
    }
    if (-not $found -or $found -eq "disabled") {
        try { $pattern.Collapse() } catch { }
        if ($found) { return "$menuName > $itemPattern is disabled" }
        return "no item $itemPattern under $menuName (" + ($names -join ', ') + ")"
    }
    $popup = [IntPtr]::Zero
    foreach ($w in [CensusWin]::Windows($procId)) { if ($popup -eq [IntPtr]::Zero -and $w -ne $mainH -and -not [CensusWin]::HasCaption($w)) { $popup = $w } }
    if ($popup -eq [IntPtr]::Zero) { try { $pattern.Collapse() } catch { }; return "the open $menuName menu has no window" }
    for ($k = 0; $k -lt $downs; $k++) {
        [void][CensusWin]::PostMessage($popup, 0x0100, [IntPtr]0x28, [IntPtr]::Zero)
        [void][CensusWin]::PostMessage($popup, 0x0101, [IntPtr]0x28, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }
    [void][CensusWin]::PostMessage($popup, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)
    [void][CensusWin]::PostMessage($popup, 0x0101, [IntPtr]0x0D, [IntPtr]::Zero)
    Say "menu: $menuName > $found ($downs Down, Enter)"
    return ""
}

# what the outlook shows in its text panes (the Output pane among them) and its title
function Get-OutlookText([int]$procId, [IntPtr]$mainH) {
    $sb = New-Object Text.StringBuilder
    [void]$sb.AppendLine("title: " + [CensusWin]::Text($mainH, 400))
    foreach ($c in [CensusWin]::Children($mainH)) {
        $cls = [CensusWin]::Class($c)
        if ($cls -notmatch 'EDIT|RichEdit') { continue }
        $t = [CensusWin]::Text($c, 200000)
        if (-not $t) { continue }
        [void]$sb.AppendLine("---- $cls")
        [void]$sb.AppendLine($t)
    }
    return $sb.ToString()
}

function Invoke-Run([string]$o, $info, [string]$cfg, [string]$tgt) {
    $run = "$($info.Id).$cfg.$tgt"
    $bin = Join-Path $Root "bin\$o"
    $rawDir = Join-Path $Root "raw\$o"
    New-Item -ItemType Directory -Force $rawDir | Out-Null
    $log = Join-Path $rawDir "$run.jsonl"
    $script:DriveLog = Join-Path $rawDir "$run.drive.txt"
    foreach ($ext in @("jsonl", "drive.txt", "meta.json", "output.txt", "build.log", "files.txt")) { Remove-Item (Join-Path $rawDir "$run.$ext") -Force -ErrorAction SilentlyContinue }
    $meta = [ordered]@{ outlook = $o; project = $info.Id; projectPath = $info.Rel; projectName = $info.Name; configuration = $cfg; target = $tgt;
                        bin = $bin; proj = ""; temp = $env:TEMP.TrimEnd('\'); home = $env:USERPROFILE.TrimEnd('\'); gridePid = 0; argument = "";
                        started = (Get-Date).ToString("s"); trigger = ""; status = ""; seconds = 0; dialogs = @(); notes = @() }
    $p = $null
    try {
        if (-not (Test-Path (Join-Path $bin "GRIDE.exe"))) { throw "no $bin\GRIDE.exe - run -Prepare" }
        Restore-Settings
        # a fresh copy of the project, under its own folder name
        $wrun = Join-Path $Root "w\$o\$run"
        $proj = Join-Path $wrun $info.Leaf
        if (-not ($KeepCopy -and (Test-Path $proj))) {
            if (Test-Path $wrun) { Remove-Item $wrun -Recurse -Force }
            New-Item -ItemType Directory -Force $wrun | Out-Null
            Copy-Item $info.Dir $proj -Recurse
        }
        $meta.proj = $proj
        $pro = $null
        if (-not $info.IsCcs) { $pro = Join-Path $proj $info.ProFile }

        # ---- the configuration and the target, where each outlook keeps them
        $menu = "Build"; $item = ""
        if ($o -eq "Black") {
            # a workspace file beside the project, as Black itself would save one: activeConfig, activeTarget
            $ws = Join-Path $proj ($info.Name + ".grws")
            $isSolution = $info.ProFile -like "*.sln"       # a Visual Studio solution: Black opens it as it is
            if ($info.IsCcs) { $entry = '{ "path": ".", "kind": "ccs7", "readOnly": true }' } else { $entry = '{ "path": "' + $info.ProFile + '" }' }
            $blackTarget = "Windows"; if ($tgt -eq "tms6747") { $blackTarget = "TMS320C6747" }
            $text = "{`r`n  `"gride`": `"1.0`",`r`n  `"type`": `"workspace`",`r`n  `"name`": `"$($info.Name)`",`r`n  `"projects`": [ $entry ],`r`n" +
                    "  `"startup`": `"$($info.Name)`",`r`n  `"activeConfig`": `"$cfg`",`r`n  `"activeTarget`": `"$blackTarget`",`r`n  `"breakpoints`": { }`r`n}`r`n"
            if ($isSolution) { $arg = Join-Path $proj $info.ProFile } else { Write-Utf8 $ws $text; $arg = $ws }
            $item = '^Rebuild Workspace$'
        } elseif ($o -eq "Grey") {
            if ($info.IsCcs) {
                $arg = Join-Path $proj ".project"
                if ($cfg -ne "Debug") {
                    # an imported project's configuration is kept in the settings: CcsProjects, by its folder
                    $settings = Join-Path $SettingsLive.APPDATA_GRIDE "settings.json"
                    $text = [IO.File]::ReadAllText($settings)
                    if ($text -match '"CcsProjects"') { throw "the settings already hold CcsProjects: not edited by this script" }
                    $key = $proj.TrimEnd('\').Replace('\', '/').ToLowerInvariant()
                    $at = $text.IndexOf('{')
                    Write-Utf8 $settings ($text.Substring(0, $at + 1) + "`r`n  `"CcsProjects`": { `"$key`": { `"configuration`": `"$cfg`" } }," + $text.Substring($at + 1))
                }
            } else {
                # Debug for Windows is what a .pro that says neither means: such a file is left as it is
                $was = [IO.File]::ReadAllText($pro); $text = $was
                if ($cfg -ne "Debug" -or $text -match '"activeConfiguration"') { $text = Set-JsonString $text "activeConfiguration" $cfg }
                if ($tgt -eq "tms6747") { $text = Set-JsonString $text "arch" "tms6747" }
                if ($text -ne $was) { Write-Utf8 $pro $text }
                $arg = $pro
            }
            $item = '^Rebuild Project$'
        } else {
            # White: the configuration is the user's state, the target the project's "arch"
            $state = Join-Path $SettingsLive.USERPROFILE_gride "state.json"
            $text = "{ }"
            if (Test-Path $state) { $text = [IO.File]::ReadAllText($state) } else { New-Item -ItemType Directory -Force (Split-Path $state) | Out-Null }
            Write-Utf8 $state (Set-JsonString $text "config" $cfg.ToLowerInvariant())
            if ($info.IsCcs) { $arg = $proj }
            else {
                if ($tgt -eq "tms6747") { Write-Utf8 $pro (Set-JsonString ([IO.File]::ReadAllText($pro)) "arch" "tms6747") }
                $arg = $pro
            }
            $item = '^Build project$'
        }
        $meta.argument = $arg
        Say "$o  $run  argument $arg"

        # ---- the outlook
        $env:GRIDE_CENSUS_LOG = $log
        if ($Visible) { $p = Start-Process (Join-Path $bin "GRIDE.exe") -ArgumentList ('"' + $arg + '"') -WorkingDirectory $bin -PassThru -WindowStyle Minimized }
        else { $p = Start-Process (Join-Path $bin "GRIDE.exe") -ArgumentList ('"' + $arg + '"') -WorkingDirectory $bin -PassThru }
        $meta.gridePid = $p.Id
        $mainH = [IntPtr]::Zero
        for ($i = 0; $i -lt 600 -and $mainH -eq [IntPtr]::Zero; $i++) {
            Start-Sleep -Milliseconds 250
            $p.Refresh()
            if ($p.HasExited) { throw "the outlook left at once with $($p.ExitCode)" }
            $mainH = $p.MainWindowHandle
        }
        if ($mainH -eq [IntPtr]::Zero) { throw "no main window in 150 s" }
        Say "main window 0x$($mainH.ToString('X')), pid $($p.Id)"
        Start-Sleep -Milliseconds 1500
        $meta.dialogs += @(Resolve-Dialogs $p.Id $mainH)

        # ---- the build (or the item asked for: -Item)
        if ($MenuItem) { $item = $MenuItem }      # (PowerShell reads $Item and $item as one name: hence MenuItem)
        $t0 = Get-Date
        $why = "not tried"
        for ($attempt = 0; $attempt -lt 20 -and $why; $attempt++) {
            $why = Invoke-Menu $p.Id $mainH $menu $item
            if ($why) { Say "trigger: $why"; Start-Sleep -Milliseconds 1000; $meta.dialogs += @(Resolve-Dialogs $p.Id $mainH) }
        }
        if ($why) { throw "the build could not be started: $why" }
        $meta.trigger = "$menu > " + $item.Trim('^', '$')
        $t0 = Get-Date
        $lastChange = Get-Date; $lastSize = -1; $again = $false; $ended = $false
        while (-not $ended) {
            Start-Sleep -Milliseconds 300
            $p.Refresh()
            if ($p.HasExited) { throw "the outlook left during the build with $($p.ExitCode)" }
            $meta.dialogs += @(Resolve-Dialogs $p.Id $mainH)
            $text = Read-Shared $log
            $starts = ([regex]::Matches($text, '(?m)^\{"ev":"start"')).Count
            $ends = ([regex]::Matches($text, '(?m)^\{"ev":"end"')).Count
            $alive = @(Get-ToolProcesses $p.Id).Count
            if ($text.Length -ne $lastSize -or $alive -gt 0) { $lastSize = $text.Length; $lastChange = Get-Date }
            $quiet = ((Get-Date) - $lastChange).TotalSeconds
            $waited = ((Get-Date) - $t0).TotalSeconds
            if ($starts -gt 0 -and $starts -eq $ends -and $alive -eq 0 -and $quiet -ge 3) { $ended = $true; $meta.status = "build ended" }
            elseif ($starts -eq 0 -and $alive -eq 0 -and $waited -ge 30 -and -not $again) {
                # nothing started: asked once more (an outlook that is building ignores it)
                $again = $true
                $why = Invoke-Menu $p.Id $mainH $menu $item
                Say "nothing recorded in 30 s; asked again: '$why'"
            }
            elseif ($starts -eq 0 -and $alive -eq 0 -and $waited -ge 75) { $ended = $true; $meta.status = "no command was run" }
            elseif ($waited -ge $TimeoutSeconds) { $ended = $true; $meta.status = "timed out after $TimeoutSeconds s ($starts started, $ends ended, $alive alive)" }
        }
        $meta.seconds = [Math]::Round(((Get-Date) - $t0).TotalSeconds - 3, 1)
        Say "$($meta.status)"
        Start-Sleep -Milliseconds 500
        try { Write-Utf8 (Join-Path $rawDir "$run.output.txt") (Get-OutlookText $p.Id $mainH) } catch { Say "output text: $($_.Exception.Message)" }
        # Black's own log of the build, to set beside the recorder's
        foreach ($f in Get-ChildItem $proj -Recurse -Filter *.build.log -ErrorAction SilentlyContinue) { Copy-Item $f.FullName (Join-Path $rawDir "$run.build.log") -Force }
        # the files the outlook wrote for a tool and named on its command line (a linker command file, a response file)
        try {
            $since = [datetime]::Parse($meta.started).AddSeconds(-2); $kept = @{}; $sb = New-Object Text.StringBuilder
            foreach ($line in (Read-Shared $log) -split "`n") {
                if ($line -notmatch '^\{"ev":"start"') { continue }
                $r = $line | ConvertFrom-Json
                foreach ($m in [regex]::Matches($r.cmd, '"([^"]+)"|(\S+)')) {
                    $word = $m.Groups[1].Value + $m.Groups[2].Value
                    $response = $word.StartsWith("@")
                    $word = $word.TrimStart('@')
                    if (-not $response -and $word -notmatch '\.(cmd|rsp|lnk|opt)$') { continue }
                    $file = $word
                    if (-not [IO.Path]::IsPathRooted($file)) { $file = Join-Path $r.cwd $file }
                    if ($kept.ContainsKey($file.ToLowerInvariant()) -or -not (Test-Path $file -PathType Leaf)) { continue }
                    $fi = Get-Item $file
                    if ($fi.LastWriteTime -lt $since -or $fi.Length -gt 200000) { continue }
                    $kept[$file.ToLowerInvariant()] = 1
                    [void]$sb.AppendLine("==== " + $fi.FullName)
                    [void]$sb.AppendLine([IO.File]::ReadAllText($fi.FullName).TrimEnd())
                }
            }
            if ($kept.Count -gt 0) { Write-Utf8 (Join-Path $rawDir "$run.files.txt") $sb.ToString() }
        } catch { Say "written files: $($_.Exception.Message)" }
    } catch {
        if (-not $meta.status) { $meta.status = "failed: " + $_.Exception.Message }
        $meta.notes += $_.Exception.Message
        Say "ERROR $($_.Exception.Message)"
    } finally {
        Remove-Item Env:\GRIDE_CENSUS_LOG -ErrorAction SilentlyContinue
        if ($p -and -not $p.HasExited) {
            $p.Refresh()
            if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
                [void][CensusWin]::PostMessage($p.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
                for ($i = 0; $i -lt 40 -and -not $p.HasExited; $i++) {
                    Start-Sleep -Milliseconds 250
                    if ($i % 4 -eq 3) { $meta.dialogs += @(Resolve-Dialogs $p.Id $p.MainWindowHandle) }
                }
            }
            if (-not $p.HasExited) { try { Stop-Process -Id $p.Id -Force } catch { }; $meta.notes += "the outlook did not close on WM_CLOSE in 10 s and was ended"; Say "killed" }
        }
        $strays = Stop-Strays
        if ($strays -gt 0) { $meta.notes += "$strays process(es) of the recording copies were still running and were ended" }
        Write-Utf8 (Join-Path $rawDir "$run.meta.json") (($meta | ConvertTo-Json -Depth 4))
        Say "done"
    }
}

# ------------------------------------------------------------------------------------------------ normalising
function Get-PathPattern([string]$path) { return '(?i)' + (([regex]::Escape($path.TrimEnd('\'))) -replace '\\\\', '[\\/]+') }

function Convert-Run([string]$o, [string]$run) {
    $rawDir = Join-Path $Root "raw\$o"
    $metaFile = Join-Path $rawDir "$run.meta.json"
    if (-not (Test-Path $metaFile)) { return $null }
    $meta = Get-Content $metaFile -Raw | ConvertFrom-Json
    $starts = @(); $ends = @{}
    $log = Join-Path $rawDir "$run.jsonl"
    if (Test-Path $log) {
        foreach ($line in [IO.File]::ReadAllLines($log, [Text.Encoding]::UTF8)) {
            if (-not $line.Trim()) { continue }
            $r = $line | ConvertFrom-Json
            if ($r.ev -eq "start") { $starts += $r } elseif ($r.ev -eq "end") { $ends[[int]$r.seq] = $r }
        }
    }
    $starts = @($starts | Sort-Object { [int]$_.seq })
    # the tokens, the longest path first
    $tokens = @()
    if ($meta.proj) { $tokens += ,@((Get-PathPattern $meta.proj), "<proj>") }
    $tokens += ,@((Get-PathPattern $meta.bin), "<bin>")
    $tokens += ,@((Get-PathPattern $meta.temp), "<temp>")
    $tokens += ,@((Get-PathPattern $meta.home), "<home>")
    $pids = @{}
    if ($meta.gridePid) { $pids["$($meta.gridePid)"] = 1 }
    foreach ($r in $starts) { $pids["$($r.pid)"] = 1; $pids["$($r.ppid)"] = 1 }
    foreach ($e in $ends.Values) { if ($e.child) { $pids["$($e.child)"] = 1 } }
    $pidPattern = ""
    $known = @($pids.Keys | Where-Object { $_ -match '^[0-9]{2,}$' } | Sort-Object { -$_.Length })
    if ($known.Count -gt 0) { $pidPattern = '(?<![0-9])(' + ($known -join '|') + ')(?![0-9])' }
    $tidy = {
        param([string]$s)
        foreach ($t in $tokens) { $s = [regex]::Replace($s, $t[0], $t[1]) }
        if (-not $pidPattern) { return $s }
        # a process id inside a name under the temp folder
        return [regex]::Replace($s, '<temp>[^\s"]*', { param($m) [regex]::Replace($m.Value, $pidPattern, '<pid>') })
    }
    $childOf = @{}
    foreach ($e in $ends.Values) { if ($e.child) { $childOf["$($e.child)"] = [int]$e.seq } }
    $kinds = @{ c90 = "compile"; cpp11 = "compile"; shalimar = "compile"; masm = "assemble"; asm6x = "assemble"; link = "link"; lnk6x = "link" }
    $count = @{ compile = 0; assemble = 0; link = 0; other = 0 }
    $lastLink = $null; $lastExit = ""
    $body = New-Object Text.StringBuilder
    foreach ($r in $starts) {
        $e = $ends[[int]$r.seq]
        $exit = "(not ended)"
        if ($e) { $exit = "$($e.exit)" }
        $kind = "other"
        if ($kinds.ContainsKey($r.tool) -and $r.cmd -notmatch '\s--?version\b') { $kind = $kinds[$r.tool] }
        $count[$kind]++
        if ($kind -eq "link") { $lastLink = $exit }
        $lastExit = $exit
        $by = ""
        if ($childOf.ContainsKey("$($r.ppid)")) { $by = "   (started by [$($childOf["$($r.ppid)"])])" }
        [void]$body.AppendLine("")
        [void]$body.AppendLine("[$($r.seq)] $($r.tool)   $kind   exit $exit$by")
        [void]$body.AppendLine("    cwd: " + (& $tidy $r.cwd))
        [void]$body.AppendLine("    cmd: " + (& $tidy $r.cmd))
        $names = @($r.env.PSObject.Properties | Where-Object { $_.Name -ne "GRIDE_CENSUS_LOG" } | Sort-Object Name)
        if ($names.Count -eq 0) { [void]$body.AppendLine("    env: (none of the recorded variables is set)") }
        foreach ($v in $names) { [void]$body.AppendLine("    env: $($v.Name)=" + (& $tidy ([string]$v.Value))) }
    }
    # the files the outlook wrote for a tool
    $filesFile = Join-Path $rawDir "$run.files.txt"
    if (Test-Path $filesFile) {
        foreach ($line in [IO.File]::ReadAllLines($filesFile, [Text.Encoding]::UTF8)) {
            if ($line.StartsWith("==== ")) { [void]$body.AppendLine(""); [void]$body.AppendLine("file written for a tool: " + (& $tidy $line.Substring(5))) }
            else { [void]$body.AppendLine("    | " + (& $tidy $line)) }
        }
    }
    # the outlook's own account of its command lines, where it gives one, set against the recorder's
    $own = $null; $where = ""
    $ownLog = Join-Path $rawDir "$run.build.log"; $ownPane = Join-Path $rawDir "$run.output.txt"
    if (Test-Path $ownLog) { $own = @([IO.File]::ReadAllLines($ownLog, [Text.Encoding]::UTF8) | Where-Object { $_.StartsWith("> ") }); $where = "$o's own build log" }
    elseif ($o -eq "Grey" -and (Test-Path $ownPane)) { $own = @([IO.File]::ReadAllLines($ownPane, [Text.Encoding]::UTF8) | Where-Object { $_.StartsWith("> ") }); $where = "$o's Output pane" }
    $check = ""
    if ($own -ne $null) {
        $left = @{}
        foreach ($l in $own) { $k = $l.Substring(2).TrimEnd(); if ($left.ContainsKey($k)) { $left[$k]++ } else { $left[$k] = 1 } }
        $unlogged = 0
        foreach ($r in $starts) { $k = ([string]$r.cmd).TrimEnd(); if ($left.ContainsKey($k) -and $left[$k] -gt 0) { $left[$k]-- } else { $unlogged++ } }
        $unrecorded = 0
        foreach ($v in $left.Values) { $unrecorded += $v }
        if ($unlogged -eq 0 -and $unrecorded -eq 0) { $check = "$where names the same $($starts.Count) command line(s)" }
        else { $check = "DIFFERS from ${where}: $unlogged recorded command line(s) are not in it, $unrecorded of its command line(s) were not recorded" }
    }
    $said = "none was run"
    if ($lastLink -ne $null) { if ($lastLink -eq "0") { $said = "succeeded (exit 0)" } else { $said = "FAILED (exit $lastLink)" } }
    $head = New-Object Text.StringBuilder
    [void]$head.AppendLine("outlook:        $($OutlookTitles[$o])")
    [void]$head.AppendLine("project:        $($meta.project)   (examples\projects\$($meta.projectPath))")
    [void]$head.AppendLine("configuration:  $($meta.configuration)")
    [void]$head.AppendLine("target:         $($meta.target)")
    [void]$head.AppendLine("commands:       $($starts.Count)   (compile $($count.compile), assemble $($count.assemble), link $($count.link), other $($count.other))")
    [void]$head.AppendLine("last link:      $said")
    [void]$head.AppendLine("driver:         $($meta.trigger); $($meta.status)")
    if ($check) { [void]$head.AppendLine("cross-check:    $check") }
    foreach ($d in @($meta.dialogs)) { if ($d) { [void]$head.AppendLine("dialog:         " + (& $tidy ([string]$d))) } }
    foreach ($n in @($meta.notes)) { if ($n) { [void]$head.AppendLine("note:           " + (& $tidy ([string]$n))) } }
    $outDir = Join-Path $Baseline $o
    New-Item -ItemType Directory -Force $outDir | Out-Null
    Write-Utf8 (Join-Path $outDir "$run.txt") ($head.ToString() + $body.ToString())
    # the time the tools took: from the first start to the last end
    $seconds = 0
    if ($starts.Count -gt 0) {
        $first = [datetime]::Parse($starts[0].t); $last = $first
        foreach ($r in $starts) { $e = $ends[[int]$r.seq]; if ($e) { $end = [datetime]::Parse($r.t).AddMilliseconds([double]$e.ms); if ($end -gt $last) { $last = $end } } }
        $seconds = [Math]::Round(($last - $first).TotalSeconds, 1)
    }
    return [pscustomobject]@{ outlook = $o; project = $meta.project; configuration = $meta.configuration; target = $meta.target; commands = $starts.Count;
                              compile = $count.compile; assemble = $count.assemble; link = $count.link; lastExit = $lastExit; seconds = $seconds; lastLink = $said; status = $meta.status; check = $check }
}

function Write-Summary {
    $rows = @()
    $order = @{}; $i = 0
    foreach ($info in Get-Projects) { $order[$info.Id] = $i++ }
    foreach ($o in $OutlookNames) {
        $rawDir = Join-Path $Root "raw\$o"
        if (-not (Test-Path $rawDir)) { continue }
        $these = @()
        foreach ($m in Get-ChildItem $rawDir -Filter *.meta.json) { $row = Convert-Run $o ($m.Name -replace '\.meta\.json$', ''); if ($row) { $these += $row } }
        $rows += @($these | Sort-Object { $order[$_.project] }, configuration, @{ Expression = { $_.target }; Descending = $true })
    }
    $lines = @("outlook`tproject`tconfiguration`ttarget`tcommands`tcompile steps`tassemble steps`tlink steps`tlast exit code`tseconds")
    foreach ($r in $rows) { $lines += "$($r.outlook)`t$($r.project)`t$($r.configuration)`t$($r.target)`t$($r.commands)`t$($r.compile)`t$($r.assemble)`t$($r.link)`t$($r.lastExit)`t$($r.seconds)" }
    New-Item -ItemType Directory -Force $Baseline | Out-Null
    [IO.File]::WriteAllLines((Join-Path $Baseline "summary.tsv"), $lines, (New-Object Text.UTF8Encoding($false)))
    return $rows
}

# ------------------------------------------------------------------------------------------------ one run, outside
function Start-Case([string]$o, $info, [string]$cfg, [string]$tgt) {
    $run = "$($info.Id).$cfg.$tgt"
    # nothing of an earlier run of this case is left to be taken for this one's
    if (Test-Path (Join-Path $Root "raw\$o")) { foreach ($f in Get-ChildItem (Join-Path $Root "raw\$o") -Filter "$run.*") { Remove-Item $f.FullName -Force } }
    Remove-Item (Join-Path (Join-Path $Baseline $o) "$run.txt") -Force -ErrorAction SilentlyContinue
    if ($Visible) { Invoke-Run $o $info $cfg $tgt }
    else {
        $q = { param([string]$s) "'" + $s.Replace("'", "''") + "'" }
        $said = Join-Path $Root "raw\$o\$run.inner.txt"
        New-Item -ItemType Directory -Force (Split-Path $said) | Out-Null
        $call = "& " + (& $q $PSCommandPath) + " -Inner -Outlook $o -Project " + (& $q $info.Rel) + " -Config $cfg -Target $tgt -Root " + (& $q $Root) + " -TimeoutSeconds $TimeoutSeconds" + $(if ($MenuItem) { " -MenuItem " + (& $q $MenuItem) } else { "" }) + $(if ($KeepCopy) { " -KeepCopy" } else { "" }) + " *> " + (& $q $said)
        $cl = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "' + $call + '"'
        $said = [CensusWin]::RunOnDesktop("GrideCensus", $cl, ($TimeoutSeconds + 240) * 1000)
        if ($said -ne "exit 0") {
            $strays = Stop-Strays
            $metaFile = Join-Path $Root "raw\$o\$run.meta.json"
            if (-not (Test-Path $metaFile)) {
                New-Item -ItemType Directory -Force (Split-Path $metaFile) | Out-Null
                $meta = [ordered]@{ outlook = $o; project = $info.Id; projectPath = $info.Rel; projectName = $info.Name; configuration = $cfg; target = $tgt; bin = (Join-Path $Root "bin\$o");
                                    proj = (Join-Path $Root "w\$o\$run\$($info.Leaf)"); temp = $env:TEMP.TrimEnd('\'); home = $env:USERPROFILE.TrimEnd('\'); gridePid = 0; argument = ""; started = "";
                                    trigger = ""; status = "the driver itself failed: $said"; seconds = 0; dialogs = @(); notes = @("$strays process(es) ended afterwards") }
                Write-Utf8 $metaFile ($meta | ConvertTo-Json -Depth 4)
            }
        }
    }
    return Convert-Run $o $run
}

# ------------------------------------------------------------------------------------------------ main
if ($Inner) {
    try { Invoke-Run $Outlook (Get-ProjectInfo $Project) $Config $Target; exit 0 }
    catch { try { Add-Content (Join-Path $Root "inner-errors.txt") ("$(Get-Date -Format s) $Outlook $Project $Config $Target : " + $_.Exception.ToString()) } catch { }; exit 3 }
}
if ($Prepare) { Invoke-Prepare; return }
if ($RestoreSettings) { Restore-Settings; "the settings are as they were before the first run"; return }
if ($Renormalise) { $rows = Write-Summary; "$(@($rows).Count) runs written again under $Baseline"; return }
if (-not (Test-Path (Join-Path $Root "backup"))) { throw "run -Prepare first" }
if (@(Get-Process GRIDE -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith((Join-Path $Root "bin"), [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) { throw "a recording copy of an outlook is running already: one at a time" }

if ($All) {
    $outlooks = $OutlookNames
    if ($Outlook) { $outlooks = @($Outlook) }
    # project by project, so that the projects wanted first are done in the three outlooks first
    foreach ($info in Get-Projects) {
        if ($Only.Count -gt 0 -and @($Only | Where-Object { $info.Id -like $_ }).Count -eq 0) { continue }
        foreach ($o in $outlooks) {
            foreach ($tgt in Get-Targets $info) {
                foreach ($cfg in @("Debug", "Release")) {
                    $run = "$($info.Id).$cfg.$tgt"
                    if (-not $Force -and (Test-Path (Join-Path $Root "raw\$o\$run.meta.json"))) { continue }
                    $row = Start-Case $o $info $cfg $tgt
                    "{0,-6} {1,-34} {2,-8} {3,-15} commands {4,3}  link: {5,-22} {6,6} s  {7}" -f $o, $info.Id, $cfg, $tgt, $row.commands, $row.lastLink, $row.seconds, $row.status
                }
            }
        }
    }
    [void](Write-Summary)
    "summary: " + (Join-Path $Baseline "summary.tsv")
    return
}
if (-not $Outlook -or -not $Project) { throw "name -Outlook and -Project, or say -All" }
if ($OutlookNames -notcontains $Outlook) { throw "the outlooks are Black, Grey and White" }
$info = Get-ProjectInfo $Project
if ((Get-Targets $info) -notcontains $Target) { throw "$($info.Id) is not built for $Target (its arch is '$($info.Arch)')" }
$row = Start-Case $Outlook $info $Config $Target
$row | Format-List | Out-String
Get-Content (Join-Path (Join-Path $Baseline $Outlook) "$($info.Id).$Config.$Target.txt")
[void](Write-Summary)
