# setup-ui.ps1 - dot-sourced by build-setup.ps1: the dialogs of GRIDE's Setup. Screen 1 welcomes,
# screen 2 chooses the outlook - Black, Grey or, last, White - The Simple, each with the toolchain
# beside it - and installs that one alone. The screens name only GRIDE and the outlooks (Black is
# GRIDEd, Grey GRIDE 2.0, White GRIDE 1.0, never shown); then the progress and the closing page.
# Installed, it offers Repair and Remove. /qb and /qn show none of it: a quiet install takes IDE=Black
# (the default), IDE=Grey or IDE=White on the command line.
# Adapted from GRIDE-Installer's msi-ui.ps1.
Set-StrictMode -Version 2

# Dialog attributes: Visible 1, Modal 2, KeepModeless 16, Error 65536.
# Control attributes: Visible 1, Enabled 2; Text: NoPrefix 131072; ProgressBar: Progress95 65536.
function Add-SetupUi([string]$productName) {
    Row "Property" @("DefaultUIFont", "DlgFont8")
    Row "Property" @("ErrorDialog", "ErrorDlg")
    foreach ($s in @(@("DlgFont8", "Tahoma", 8, $null, 0), @("DlgFontBold8", "Tahoma", 8, $null, 1), @("DlgTitleFont", "Tahoma", 9, $null, 1))) { Row "TextStyle" $s }
    foreach ($a in @(@("InstallFiles", "Copying new files", "File: [1],  Directory: [9],  Size: [6]"), @("RemoveFiles", "Removing files", "File: [1], Directory: [9]"),
            @("CreateShortcuts", "Creating shortcuts", "Shortcut: [1]"), @("RemoveShortcuts", "Removing shortcuts", "Shortcut: [1]"),
            @("WriteRegistryValues", "Writing system registry values", "Key: [1], Name: [2], Value: [3]"), @("RemoveRegistryValues", "Removing system registry values", "Key: [1], Name: [2]"),
            @("CreateFolders", "Creating folders", "Folder: [1]"), @("RemoveFolders", "Removing folders", "Folder: [1]"),
            @("RegisterProduct", "Registering product", "[1]"), @("RegisterUser", "Registering user", "[1]"), @("PublishFeatures", "Publishing product features", "Feature: [1]"),
            @("PublishProduct", "Publishing product information", $null), @("UnpublishFeatures", "Unpublishing product features", "Feature: [1]"),
            @("ProcessComponents", "Updating component registration", $null), @("InstallValidate", "Validating install", $null), @("InstallInitialize", "Preparing to install", $null),
            @("InstallFinalize", "Finishing", $null), @("CostInitialize", "Computing space requirements", $null), @("FileCost", "Computing space requirements", $null),
            @("CostFinalize", "Computing space requirements", $null), @("RemoveExistingProducts", "Removing the earlier version", "Application: [1], Command line: [2]"),
            @("AppSearch", "Searching for installed applications", "Property: [1], Signature: [2]"), @("LaunchConditions", "Evaluating launch conditions", $null),
            @("FindRelatedProducts", "Searching for related applications", "Found application: [1]"))) { Row "ActionText" $a }
    foreach ($u in @(@("bytes", "bytes"), @("GB", "GB"), @("KB", "KB"), @("MB", "MB"), @("ScriptInProgress", "Gathering the information needed to install"),
            @("TimeRemaining", "Time remaining: {[1] minutes }{[2] seconds}"))) { Row "UIText" $u }

    $name = "[ProductName]"
    $bottom = @("BottomLine", "Line", 0, 234, 374, 0, 1, $null, $null, $null, $null)
    # key, title, control-first, default, cancel, controls (Control, Type, X, Y, W, H, Attributes, Property, Text, Next, Help), events (Control, Event, Argument, Condition, Ordering)
    $dialogs = @(
        @{ Key = "WelcomeDlg"; Attr = 3; Title = "$name Setup"; First = "Next"; Default = "Next"; Cancel = "Cancel"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}Welcome to the $name Setup", $null, $null),
               @("Description", "Text", 20, 60, 330, 120, 131075, $null, "GRIDE is an IDE for RIDE's toolchain - C90, C++11 and Shalimar for Windows and the TMS320C6747. It comes in three outlooks: Black, Grey and White - The Simple. On the next page you choose one; Setup installs GRIDE in that outlook with the toolchain beside it and a Start menu entry. Programs and Features removes it again.", $null, $null),
               $bottom,
               @("Next", "PushButton", 236, 243, 56, 17, 3, $null, "&Next >", "Cancel", $null),
               @("Cancel", "PushButton", 304, 243, 56, 17, 3, $null, "Cancel", "Next", $null))
           Events = @(@("Next", "NewDialog", "ChooseDlg", "1", 1), @("Cancel", "SpawnDialog", "CancelDlg", "1", 1)) },
        @{ Key = "ChooseDlg"; Attr = 3; Title = "$name Setup"; First = "IdeChoice"; Default = "Install"; Cancel = "Cancel"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}Choose GRIDE's outlook", $null, $null),
               @("Description", "Text", 20, 40, 330, 30, 131075, $null, "GRIDE builds for Windows and the TMS320C6747 with the same eleven tools in every outlook. Choose one:", $null, $null),
               @("IdeChoice", "RadioButtonGroup", 20, 75, 330, 120, 3, "IDE", $null, "Back", $null),
               @("Folder", "Text", 20, 205, 330, 20, 131075, $null, "It goes into [INSTALLDIR], in a folder of its own.", $null, $null),
               $bottom,
               @("Back", "PushButton", 168, 243, 56, 17, 3, $null, "< &Back", "Install", $null),
               @("Install", "PushButton", 236, 243, 56, 17, 3, $null, "&Install", "Cancel", $null),
               @("Cancel", "PushButton", 304, 243, 56, 17, 3, $null, "Cancel", "IdeChoice", $null))
           Events = @(@("Back", "NewDialog", "WelcomeDlg", "1", 1),
                      @("Install", "Remove", "Grey", "IDE=`"Black`"", 1), @("Install", "Remove", "White", "IDE=`"Black`"", 2), @("Install", "AddLocal", "Black", "IDE=`"Black`"", 3),
                      @("Install", "Remove", "Black", "IDE=`"Grey`"", 4), @("Install", "Remove", "White", "IDE=`"Grey`"", 5), @("Install", "AddLocal", "Grey", "IDE=`"Grey`"", 6),
                      @("Install", "Remove", "Black", "IDE=`"White`"", 7), @("Install", "Remove", "Grey", "IDE=`"White`"", 8), @("Install", "AddLocal", "White", "IDE=`"White`"", 9),
                      @("Install", "EndDialog", "Return", "1", 10), @("Cancel", "SpawnDialog", "CancelDlg", "1", 1)) },
        @{ Key = "MaintenanceDlg"; Attr = 3; Title = "$name Setup"; First = "Repair"; Default = "Repair"; Cancel = "Cancel"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}$name is installed", $null, $null),
               @("Description", "Text", 20, 60, 330, 100, 131075, $null, "Repair puts every file of GRIDE back as it was installed. Remove takes GRIDE off this computer.", $null, $null),
               $bottom,
               @("Repair", "PushButton", 168, 243, 56, 17, 3, $null, "Re&pair", "Remove", $null),
               @("Remove", "PushButton", 236, 243, 56, 17, 3, $null, "&Remove", "Cancel", $null),
               @("Cancel", "PushButton", 304, 243, 56, 17, 3, $null, "Cancel", "Repair", $null))
           Events = @(@("Repair", "[REINSTALL]", "ALL", "1", 1), @("Repair", "EndDialog", "Return", "1", 2),
                      @("Remove", "[REMOVE]", "ALL", "1", 1), @("Remove", "EndDialog", "Return", "1", 2), @("Cancel", "SpawnDialog", "CancelDlg", "1", 1)) },
        @{ Key = "ProgressDlg"; Attr = 1; Title = "$name Setup"; First = "Cancel"; Default = "Cancel"; Cancel = "Cancel"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}Installing $name", $null, $null),
               @("Text", "Text", 20, 50, 330, 30, 131075, $null, "Please wait while the IDE and its toolchain are installed. This may take a minute.", $null, $null),
               @("ActionText", "Text", 20, 100, 330, 20, 131075, $null, $null, $null, $null),
               @("ProgressBar", "ProgressBar", 20, 120, 330, 10, 65537, $null, "Progress done", $null, $null),
               $bottom,
               @("Cancel", "PushButton", 304, 243, 56, 17, 3, $null, "Cancel", "Cancel", $null))
           Events = @(, @("Cancel", "SpawnDialog", "CancelDlg", "1", 1)) },
        @{ Key = "ExitDlg"; Attr = 3; Title = "$name Setup"; First = "Finish"; Default = "Finish"; Cancel = "Finish"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}$name Setup is complete", $null, $null),
               @("Description", "Text", 20, 60, 330, 100, 131075, $null, "Click Finish to close Setup.", $null, $null),
               $bottom,
               @("Finish", "PushButton", 304, 243, 56, 17, 3, $null, "&Finish", "Finish", $null))
           Events = @(, @("Finish", "EndDialog", "Return", "1", 1)) },
        @{ Key = "FatalErrorDlg"; Attr = 3; Title = "$name Setup"; First = "Finish"; Default = "Finish"; Cancel = "Finish"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}Setup ended early", $null, $null),
               @("Description", "Text", 20, 60, 330, 100, 131075, $null, "$name Setup ended prematurely because of an error. Your system has not been modified. Run Setup again when the cause is fixed; a log written with msiexec /l*v says what happened.", $null, $null),
               $bottom,
               @("Finish", "PushButton", 304, 243, 56, 17, 3, $null, "&Finish", "Finish", $null))
           Events = @(, @("Finish", "EndDialog", "Exit", "1", 1)) },
        @{ Key = "UserExitDlg"; Attr = 3; Title = "$name Setup"; First = "Finish"; Default = "Finish"; Cancel = "Finish"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}Setup was interrupted", $null, $null),
               @("Description", "Text", 20, 60, 330, 100, 131075, $null, "$name Setup was interrupted. Your system has not been modified. Run Setup again to install it.", $null, $null),
               $bottom,
               @("Finish", "PushButton", 304, 243, 56, 17, 3, $null, "&Finish", "Finish", $null))
           Events = @(, @("Finish", "EndDialog", "Exit", "1", 1)) },
        @{ Key = "CancelDlg"; Attr = 3; Title = "$name Setup"; First = "No"; Default = "No"; Cancel = "No"; Width = 260; Height = 85
           Controls = @(
               @("Text", "Text", 20, 15, 220, 30, 131075, $null, "Are you sure you want to cancel the installation of ${name}?", $null, $null),
               @("Yes", "PushButton", 72, 57, 56, 17, 3, $null, "&Yes", "No", $null),
               @("No", "PushButton", 132, 57, 56, 17, 3, $null, "&No", "Yes", $null))
           Events = @(@("Yes", "EndDialog", "Exit", "1", 1), @("No", "EndDialog", "Return", "1", 1)) },
        @{ Key = "ErrorDlg"; Attr = 65543; Title = "$name Setup"; First = "ErrorText"; Default = $null; Cancel = $null; Width = 270; Height = 105
           Controls = @(
               @("ErrorText", "Text", 20, 15, 230, 60, 131075, $null, "Information text", $null, $null),
               @("Y", "PushButton", 100, 80, 56, 17, 3, $null, "&Yes", $null, $null),
               @("N", "PushButton", 100, 80, 56, 17, 3, $null, "&No", $null, $null),
               @("A", "PushButton", 100, 80, 56, 17, 3, $null, "&Abort", $null, $null),
               @("C", "PushButton", 100, 80, 56, 17, 3, $null, "&Cancel", $null, $null),
               @("I", "PushButton", 100, 80, 56, 17, 3, $null, "&Ignore", $null, $null),
               @("O", "PushButton", 100, 80, 56, 17, 3, $null, "&OK", $null, $null),
               @("R", "PushButton", 100, 80, 56, 17, 3, $null, "&Retry", $null, $null))
           Events = @(@("Y", "EndDialog", "ErrorYes", "1", 1), @("N", "EndDialog", "ErrorNo", "1", 1), @("A", "EndDialog", "ErrorAbort", "1", 1),
                      @("C", "EndDialog", "ErrorCancel", "1", 1), @("I", "EndDialog", "ErrorIgnore", "1", 1), @("O", "EndDialog", "ErrorOk", "1", 1), @("R", "EndDialog", "ErrorRetry", "1", 1)) },
        @{ Key = "FilesInUse"; Attr = 19; Title = "$name Setup"; First = "Retry"; Default = "Retry"; Cancel = "Exit"
           Controls = @(
               @("Title", "Text", 20, 15, 330, 24, 131075, $null, "{\DlgTitleFont}Files in use", $null, $null),
               @("Text", "Text", 20, 50, 330, 30, 131075, $null, "The following programs are using files that Setup must update. Close them and click Retry, or click Exit to stop.", $null, $null),
               @("List", "ListBox", 20, 87, 330, 130, 7, "FileInUseProcess", $null, $null, $null),
               $bottom,
               @("Exit", "PushButton", 166, 243, 56, 17, 3, $null, "E&xit", "Ignore", $null),
               @("Ignore", "PushButton", 235, 243, 56, 17, 3, $null, "&Ignore", "Retry", $null),
               @("Retry", "PushButton", 304, 243, 56, 17, 3, $null, "&Retry", "Exit", $null))
           Events = @(@("Exit", "EndDialog", "Exit", "1", 1), @("Ignore", "EndDialog", "Ignore", "1", 1), @("Retry", "EndDialog", "Retry", "1", 1)) }
    )
    foreach ($d in $dialogs) {
        $w = if ($d.ContainsKey("Width")) { $d.Width } else { 370 }
        $h = if ($d.ContainsKey("Height")) { $d.Height } else { 270 }
        Row "Dialog" @($d.Key, 50, 50, $w, $h, $d.Attr, $d.Title, $d.First, $d.Default, $d.Cancel)
        foreach ($c in $d.Controls) { Row "Control" (@($d.Key) + $c) }
        foreach ($e in $d.Events) { Row "ControlEvent" (@($d.Key) + $e) }
    }
    # The three choices of screen 2, White last (the radio buttons of the property IDE, Text at most 64 characters)
    Row "RadioButton" @("IDE", 1, "Black", 10, 5, 310, 30, "&Black outlook - GRIDE", $null)
    Row "RadioButton" @("IDE", 2, "Grey", 10, 45, 310, 30, "&Grey outlook - GRIDE", $null)
    Row "RadioButton" @("IDE", 3, "White", 10, 85, 310, 30, "&White outlook - The Simple", $null)
    Row "EventMapping" @("ProgressDlg", "ProgressBar", "SetProgress", "Progress")
    Row "EventMapping" @("ProgressDlg", "ActionText", "ActionText", "Text")
    Row "InstallUISequence" @("WelcomeDlg", "NOT Installed", 1200)
    Row "InstallUISequence" @("MaintenanceDlg", "Installed", 1210)
    Row "InstallUISequence" @("ProgressDlg", $null, 1280)
    Row "InstallUISequence" @("ExitDlg", $null, -1)
    Row "InstallUISequence" @("UserExitDlg", $null, -2)
    Row "InstallUISequence" @("FatalErrorDlg", $null, -3)
}
