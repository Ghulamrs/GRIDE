# The installed RIDE.exe driven through its window: open a project, choose a configuration, then
# Build project, Run project, Emulate on vm6747 and Verify as asked, recording what the console
# pane and the status bar say after each. One JSON line per step goes to $Out.
# A window needs the logged-on desktop: run it there (run-all.ps1 is started by schtasks /IT).
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Project,
      [string]$Config = "Release", [Parameter(Mandatory)][string]$Out, [int]$Wait = 300,
      [string]$Steps = "build,run")
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$PC = [System.Windows.Automation.PropertyCondition]

function Record($o) { ($o | ConvertTo-Json -Depth 5 -Compress) | Add-Content -Path $Out -Encoding UTF8 }

function MenuItemIn($root, [string]$name) {
    $c = New-Object $PC($AE::ControlTypeProperty, $CT::MenuItem)
    foreach ($m in $root.FindAll($TS::Descendants, $c)) {
        if (($m.Current.Name -replace '&', '') -eq $name) { return $m }
    }
    return $null
}

# An open dropdown is a top-level window of RIDE's own, so a submenu's item is looked for there and
# not in the form, whose menu bar has a Debug of its own beside Configuration's.
function DropDownItem([int]$procId, [IntPtr]$mainHandle, [string]$name) {
    $c = New-Object $PC($AE::ProcessIdProperty, $procId)
    foreach ($w in $AE::RootElement.FindAll($TS::Children, $c)) {
        if ($w.Current.NativeWindowHandle -eq $mainHandle.ToInt32()) { continue }
        $m = MenuItemIn $w $name
        if ($m) { return $m }
    }
    return $null
}

function Invoke-Menu($main, [string[]]$path) {
    $item = MenuItemIn $main $path[0]
    if (-not $item) { return "no menu '$($path[0])'" }
    for ($i = 0; $i -lt $path.Count; $i++) {
        if ($i -gt 0) {
            # An expanded item holds its dropdown's items as children; failing that, RIDE's other windows.
            $parent = $item; $item = $null
            for ($t = 0; $t -lt 30 -and -not $item; $t++) {
                Start-Sleep -Milliseconds 150
                $item = MenuItemIn $parent $path[$i]
                if (-not $item) { $item = DropDownItem $script:proc.Id $script:proc.MainWindowHandle $path[$i] }
            }
            if (-not $item) { [System.Windows.Forms.SendKeys]::SendWait("{ESC}{ESC}{ESC}"); return "no item '$($path[$i])'" }
        }
        if ($i -lt $path.Count - 1) {
            try { $item.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand() }
            catch { return "could not open '$($path[$i])'" }
        } else {
            if (-not $item.Current.IsEnabled) { [System.Windows.Forms.SendKeys]::SendWait("{ESC}{ESC}{ESC}"); return "'$($path[$i])' is disabled" }
            # From another runspace: a modal dialog would otherwise hold this call until it closed.
            $ps = [PowerShell]::Create()
            $ps.AddScript({ param($e) $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }).AddArgument($item) | Out-Null
            $ps.BeginInvoke() | Out-Null
            Start-Sleep -Milliseconds 400
            return "invoked"
        }
    }
}

# The console is a read-only TextBox: an Edit with a Value, or on some builds a Pane whose Name
# holds the text. Both are read, and the status bar's text beside them.
function Panes($main) {
    $out = @()
    $c = New-Object System.Windows.Automation.OrCondition(@(
        (New-Object $PC($AE::ControlTypeProperty, $CT::Edit)), (New-Object $PC($AE::ControlTypeProperty, $CT::Document)),
        (New-Object $PC($AE::ControlTypeProperty, $CT::Pane))))
    foreach ($e in $main.FindAll($TS::Descendants, $c)) {
        $v = $null; $p = $null
        if ($e.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$p)) { $v = $p.Current.Value }
        elseif ($e.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$p)) { $v = $p.DocumentRange.GetText(50000) }
        if (-not $v -and $e.Current.ControlType -eq $CT::Pane -and $e.Current.Name.Length -gt 40) { $v = $e.Current.Name }
        if ($v) { $out += [ordered]@{ kind = $e.Current.ControlType.ProgrammaticName; id = $e.Current.AutomationId; text = $v } }
    }
    return $out
}

function Status($main) {
    $c = New-Object $PC($AE::ControlTypeProperty, $CT::StatusBar)
    $bar = $main.FindFirst($TS::Descendants, $c)
    if (-not $bar) { return "" }
    return (($bar.FindAll($TS::Children, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name }) -join " | ")
}

# Done when the panes have not changed for six seconds and Stop is off, or $Wait has gone by.
function Settle($main) {
    $last = ""; $same = 0
    for ($t = 0; $t -lt $Wait; $t++) {
        Start-Sleep -Seconds 1
        $now = (Panes $main | ConvertTo-Json -Depth 3 -Compress) + (Status $main)
        if ($now -eq $last) { $same++; if ($same -ge 6) { return $t } } else { $same = 0; $last = $now }
    }
    return -1
}

$script:proc = Start-Process -FilePath $Exe -ArgumentList ('"' + $Project + '"') -WorkingDirectory (Split-Path $Project) -PassThru
$main = $null
for ($i = 0; $i -lt 60 -and -not $main; $i++) {
    Start-Sleep -Milliseconds 500; $script:proc.Refresh()
    if ($script:proc.MainWindowHandle -ne [IntPtr]::Zero -and $script:proc.MainWindowTitle) { $main = $AE::FromHandle($script:proc.MainWindowHandle) }
}
if (-not $main) { Record ([ordered]@{ project = $Project; step = "start"; error = "no window" }); exit 1 }
Start-Sleep -Seconds 2
Record ([ordered]@{ project = $Project; step = "open"; title = $main.Current.Name; status = (Status $main) })
Record ([ordered]@{ project = $Project; step = "config"; config = $Config; how = (Invoke-Menu $main @("Build", "Configuration", $Config)) })
Start-Sleep -Seconds 1
$items = @{ build = "Build project"; run = "Run project"; emulate = "Emulate on vm6747"; verify = "Verify" }
foreach ($s in $Steps.Split(',')) {
    $how = Invoke-Menu $main @("Build", $items[$s])
    $secs = Settle $main
    Record ([ordered]@{ project = $Project; config = $Config; step = $s; how = $how; settled = $secs;
                        status = (Status $main); panes = @(Panes $main) })
}
try { $script:proc.Kill() } catch {}
