# msi-schema.ps1 - dot-sourced by build-msi.ps1: the Windows Installer tables the package uses, as
# the Windows Installer SDK's schema defines them. Each column: name, type (s = string, l = localizable
# string, i2 / i4 = integer, v = stream; S / L / I = nullable; the number is the width, 0 unbounded),
# then the _Validation row for it: key table and column it refers to, category, set and description.
# CREATE TABLE and the _Validation table are both written from this list, so they cannot disagree.
Set-StrictMode -Version 2

function Col([string]$name, [string]$type, [string]$category = "", [string]$keyTable = "", $keyColumn = $null, $min = $null, $max = $null, [string]$set = "", [string]$description = "") {
    [pscustomobject]@{ Name = $name; Type = $type; Category = $category; KeyTable = $keyTable; KeyColumn = $keyColumn; Min = $min; Max = $max; Set = $set; Description = $description }
}

$MsiSchema = [ordered]@{
    "_Validation" = @{ Keys = 2; Columns = @(
        (Col "Table" "s32" "Identifier" "" $null $null $null "" "Name of table"),
        (Col "Column" "s32" "Identifier" "" $null $null $null "" "Name of column"),
        (Col "Nullable" "s4" "" "" $null $null $null "Y;N;@" "Whether the column is nullable"),
        (Col "MinValue" "I4" "" "" $null -2147483647 2147483647 "" "Minimum value allowed"),
        (Col "MaxValue" "I4" "" "" $null -2147483647 2147483647 "" "Maximum value allowed"),
        (Col "KeyTable" "S255" "Identifier" "" $null $null $null "" "For foreign key, Name of table to which data must link"),
        (Col "KeyColumn" "I2" "" "" $null 1 32 "" "Column to which foreign key connects"),
        (Col "Category" "S32" "" "" $null $null $null "Text;Formatted;Template;Condition;Guid;Path;Version;Language;Identifier;Binary;UpperCase;LowerCase;Filename;Paths;AnyPath;WildCardFilename;RegPath;KeyFormatted;CustomSource;Property;Cabinet;Shortcut;URL;DefaultDir;Integer;DoubleInteger;TimeDate;FormattedSDDLText" "String category"),
        (Col "Set" "S255" "Text" "" $null $null $null "" "Set of values that are permitted"),
        (Col "Description" "S255" "Text" "" $null $null $null "" "Description of column")) }
    "Property" = @{ Keys = 1; Columns = @(
        (Col "Property" "s72" "Identifier" "" $null $null $null "" "Name of property, uppercase if settable by launcher or loader."),
        (Col "Value" "l0" "Text" "" $null $null $null "" "String value for property.  Never null or empty.")) }
    "Directory" = @{ Keys = 1; Columns = @(
        (Col "Directory" "s72" "Identifier" "" $null $null $null "" "Unique identifier for directory entry, primary key. If a property by this name is defined, it contains the full path to the directory."),
        (Col "Directory_Parent" "S72" "Identifier" "Directory" 1 $null $null "" "Reference to the entry in this table specifying the default parent directory. A record parented to itself or with a Null parent represents a root of the install tree."),
        (Col "DefaultDir" "l255" "DefaultDir" "" $null $null $null "" "The default sub-path under parent's path.")) }
    "Component" = @{ Keys = 1; Columns = @(
        (Col "Component" "s72" "Identifier" "" $null $null $null "" "Primary key used to identify a particular component record."),
        (Col "ComponentId" "S38" "Guid" "" $null $null $null "" "A string GUID unique to this component, version, and language."),
        (Col "Directory_" "s72" "Identifier" "Directory" 1 $null $null "" "Required key of a Directory table record. This is actually a property name whose value contains the actual path, set either by the AppSearch action or with the default setting obtained from the Directory table."),
        (Col "Attributes" "i2" "" "" $null 0 32767 "" "Remote execution option, one of irsEnum"),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "A conditional statement that will disable this component if the specified condition evaluates to the 'True' state. If a component is disabled, it will not be installed, regardless of the 'Action' state associated with the component."),
        (Col "KeyPath" "S72" "Identifier" "File;Registry;ODBCDataSource" 1 $null $null "" "Either the primary key into the File table, Registry table, or ODBCDataSource table. This extract path is stored when the component is installed, and is used to detect the presence of the component and to return the path to it.")) }
    "Feature" = @{ Keys = 1; Columns = @(
        (Col "Feature" "s38" "Identifier" "" $null $null $null "" "Primary key used to identify a particular feature record."),
        (Col "Feature_Parent" "S38" "Identifier" "Feature" 1 $null $null "" "Optional key of a parent record in the same table. If the parent is not selected, then the record will not be installed. Null indicates a root item."),
        (Col "Title" "L64" "Text" "" $null $null $null "" "Short text identifying a visible feature item."),
        (Col "Description" "L255" "Text" "" $null $null $null "" "Longer descriptive text describing a visible feature item."),
        (Col "Display" "I2" "" "" $null 0 32767 "" "Numeric sort order, used to force a specific display ordering."),
        (Col "Level" "i2" "" "" $null 0 32767 "" "The install level at which record will be initially selected. An install level of 0 will disable an item and prevent its display."),
        (Col "Directory_" "S72" "UpperCase" "Directory" 1 $null $null "" "The name of the Directory that can be configured by the UI. A non-null value will enable the browse button."),
        (Col "Attributes" "i2" "" "" $null $null $null "0;1;2;4;5;6;8;9;10;16;17;18;20;21;22;24;25;26;32;33;34;36;37;38;48;49;50;52;53;54" "Feature attributes")) }
    "FeatureComponents" = @{ Keys = 2; Columns = @(
        (Col "Feature_" "s38" "Identifier" "Feature" 1 $null $null "" "Foreign key into Feature table."),
        (Col "Component_" "s72" "Identifier" "Component" 1 $null $null "" "Foreign key into Component table.")) }
    "File" = @{ Keys = 1; Columns = @(
        (Col "File" "s72" "Identifier" "" $null $null $null "" "Primary key, non-localized token, must match identifier in cabinet.  For uncompressed files, this field is ignored."),
        (Col "Component_" "s72" "Identifier" "Component" 1 $null $null "" "Foreign key referencing Component that controls the file."),
        (Col "FileName" "l255" "Filename" "" $null $null $null "" "File name used for installation, may be localized.  This may contain a 'short name|long name' pair."),
        (Col "FileSize" "i4" "" "" $null 0 2147483647 "" "Size of file in bytes (long integer)."),
        (Col "Version" "S72" "Version" "File" 1 $null $null "" "Version string for versioned files;  Blank for unversioned files."),
        (Col "Language" "S20" "Language" "" $null $null $null "" "List of decimal language Ids, comma-separated if more than one."),
        (Col "Attributes" "I2" "" "" $null 0 32767 "" "Integer containing bit flags representing file attributes (with the decimal value of each bit position in parentheses)"),
        (Col "Sequence" "i2" "" "" $null 1 32767 "" "Sequence with respect to the media images; order must track cabinet order.")) }
    "Media" = @{ Keys = 1; Columns = @(
        (Col "DiskId" "i2" "" "" $null 1 32767 "" "Primary key, integer to determine sort order for table."),
        (Col "LastSequence" "i2" "" "" $null 0 32767 "" "File sequence number for the last file for this media."),
        (Col "DiskPrompt" "L64" "Text" "" $null $null $null "" "Disk name: the visible text actually printed on the disk.  This will be used to prompt the user when this disk needs to be inserted."),
        (Col "Cabinet" "S255" "Cabinet" "" $null $null $null "" "If some or all of the files stored on the media are compressed in a cabinet, the name of that cabinet."),
        (Col "VolumeLabel" "S32" "Text" "" $null $null $null "" "The label attributed to the volume."),
        (Col "Source" "S72" "Property" "" $null $null $null "" "The property defining the location of the cabinet file.")) }
    "MsiFileHash" = @{ Keys = 1; Columns = @(
        (Col "File_" "s72" "Identifier" "File" 1 $null $null "" "Primary key, foreign key into File table referencing file with this hash"),
        (Col "Options" "i2" "" "" $null 0 32767 "" "Various options and attributes for this hash."),
        (Col "HashPart1" "i4" "" "" $null $null $null "" "Size of file in bytes (long integer)."),
        (Col "HashPart2" "i4" "" "" $null $null $null "" "Size of file in bytes (long integer)."),
        (Col "HashPart3" "i4" "" "" $null $null $null "" "Size of file in bytes (long integer)."),
        (Col "HashPart4" "i4" "" "" $null $null $null "" "Size of file in bytes (long integer).")) }
    "Upgrade" = @{ Keys = 5; Columns = @(
        (Col "UpgradeCode" "s38" "Guid" "" $null $null $null "" "The UpgradeCode GUID belonging to the products in this set."),
        (Col "VersionMin" "S20" "Text" "" $null $null $null "" "The minimum ProductVersion of the products in this set.  The set may or may not include products with this particular version."),
        (Col "VersionMax" "S20" "Text" "" $null $null $null "" "The maximum ProductVersion of the products in this set.  The set may or may not include products with this particular version."),
        (Col "Language" "S255" "Language" "" $null $null $null "" "A comma-separated list of languages for either products in this set or products not in this set."),
        (Col "Attributes" "i4" "" "" $null 0 2147483647 "" "The attributes of this product set."),
        (Col "Remove" "S255" "Formatted" "" $null $null $null "" "The list of features to remove when uninstalling a product from this set.  The default is 'ALL'."),
        (Col "ActionProperty" "s72" "UpperCase" "" $null $null $null "" "The property to set when a product in this set is found.")) }
    "LaunchCondition" = @{ Keys = 1; Columns = @(
        (Col "Condition" "s255" "Condition" "" $null $null $null "" "Expression which must evaluate to TRUE in order for install to commence."),
        (Col "Description" "l255" "Formatted" "" $null $null $null "" "Localizable text to display when condition fails and install must abort.")) }
    # Empty, and needed: AppSearch reads it for every signature, a registry value's included (error 2228 without it).
    "Signature" = @{ Keys = 1; Columns = @(
        (Col "Signature" "s72" "Identifier" "" $null $null $null "" "The table key. The Signature represents a unique file signature."),
        (Col "FileName" "s255" "Text" "" $null $null $null "" "The name of the file. This may contain a 'short name|long name' pair."),
        (Col "MinVersion" "S20" "Text" "" $null $null $null "" "The minimum version of the file."),
        (Col "MaxVersion" "S20" "Text" "" $null $null $null "" "The maximum version of the file."),
        (Col "MinSize" "I4" "" "" $null 0 2147483647 "" "The minimum size of the file."),
        (Col "MaxSize" "I4" "" "" $null 0 2147483647 "" "The maximum size of the file."),
        (Col "MinDate" "I4" "" "" $null 0 2147483647 "" "The minimum creation date of the file."),
        (Col "MaxDate" "I4" "" "" $null 0 2147483647 "" "The maximum creation date of the file."),
        (Col "Languages" "S255" "Language" "" $null $null $null "" "The languages supported by the file.")) }
    "AppSearch" = @{ Keys = 2; Columns = @(
        (Col "Property" "s72" "Identifier" "" $null $null $null "" "The property associated with a Signature"),
        (Col "Signature_" "s72" "Identifier" "Signature;RegLocator;IniLocator;CompLocator;DrLocator" 1 $null $null "" "The Signature_ represents a unique file signature and is also the foreign key in the Signature,  RegLocator, IniLocator, CompLocator and the DrLocator tables.")) }
    "RegLocator" = @{ Keys = 1; Columns = @(
        (Col "Signature_" "s72" "Identifier" "" $null $null $null "" "The table key. The Signature_ represents a unique file signature and is also the foreign key in the Signature table. If the type is 0, the registry values refers a directory, and _Signature is not a foreign key."),
        (Col "Root" "i2" "" "" $null 0 3 "" "The predefined root key for the registry value, one of rrkEnum."),
        (Col "Key" "l255" "RegPath" "" $null $null $null "" "The key for the registry value."),
        (Col "Name" "L255" "Formatted" "" $null $null $null "" "The registry value name."),
        (Col "Type" "I2" "" "" $null 0 18 "" "An integer value that determines if the registry value is a filename or a directory location or to be used as is w/o interpretation.")) }
    "Shortcut" = @{ Keys = 1; Columns = @(
        (Col "Shortcut" "s72" "Identifier" "" $null $null $null "" "Primary key, non-localized token."),
        (Col "Directory_" "s72" "Identifier" "Directory" 1 $null $null "" "Foreign key into the Directory table denoting the directory where the shortcut file is created."),
        (Col "Name" "l128" "Filename" "" $null $null $null "" "The name of the shortcut to be created."),
        (Col "Component_" "s72" "Identifier" "Component" 1 $null $null "" "Foreign key into the Component table denoting the component whose selection gates the the shortcut creation/deletion."),
        (Col "Target" "s72" "Shortcut" "" $null $null $null "" "The shortcut target. This is usually a property that is expanded to a file or a folder that the shortcut points to."),
        (Col "Arguments" "S255" "Formatted" "" $null $null $null "" "The command-line arguments for the shortcut."),
        (Col "Description" "L255" "Text" "" $null $null $null "" "The description for the shortcut."),
        (Col "Hotkey" "I2" "" "" $null 0 32767 "" "The hotkey for the shortcut. It has the virtual-key code for the key in the low-order byte, and the modifier flags in the high-order byte."),
        (Col "Icon_" "S72" "Identifier" "Icon" 1 $null $null "" "Foreign key into the File table denoting the external icon file for the shortcut."),
        (Col "IconIndex" "I2" "" "" $null -32767 32767 "" "The icon index for the shortcut."),
        (Col "ShowCmd" "I2" "" "" $null $null $null "1;3;7" "The show command for the application window.The following values may be used."),
        (Col "WkDir" "S72" "Identifier" "" $null $null $null "" "Name of property defining location of working directory.")) }
    "Registry" = @{ Keys = 1; Columns = @(
        (Col "Registry" "s72" "Identifier" "" $null $null $null "" "Primary key, non-localized token."),
        (Col "Root" "i2" "" "" $null -1 3 "" "The predefined root key for the registry value, one of rrkEnum."),
        (Col "Key" "l255" "RegPath" "" $null $null $null "" "The key for the registry value."),
        (Col "Name" "L255" "Formatted" "" $null $null $null "" "The registry value name."),
        (Col "Value" "L0" "Formatted" "" $null $null $null "" "The registry value."),
        (Col "Component_" "s72" "Identifier" "Component" 1 $null $null "" "Foreign key into the Component table referencing component that controls the installing of the registry value.")) }
    "RemoveFile" = @{ Keys = 1; Columns = @(
        (Col "FileKey" "s72" "Identifier" "" $null $null $null "" "Primary key used to identify a particular file entry"),
        (Col "Component_" "s72" "Identifier" "Component" 1 $null $null "" "Foreign key referencing Component that controls the file to be removed."),
        (Col "FileName" "L255" "WildCardFilename" "" $null $null $null "" "Name of the file to be removed."),
        (Col "DirProperty" "s72" "Identifier" "" $null $null $null "" "Name of a property whose value is assumed to resolve to the full pathname to the folder of the file to be removed."),
        (Col "InstallMode" "i2" "" "" $null $null $null "1;2;3" "Installation option, one of iimEnum.")) }
    "CreateFolder" = @{ Keys = 2; Columns = @(
        (Col "Directory_" "s72" "Identifier" "Directory" 1 $null $null "" "Primary key, could be foreign key into the Directory table."),
        (Col "Component_" "s72" "Identifier" "Component" 1 $null $null "" "Foreign key into the Component table.")) }
    "CustomAction" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Primary key, name of action, normally appears in sequence table unless private use."),
        (Col "Type" "i2" "" "" $null 1 32767 "" "The numeric custom action type, consisting of source location, code type, entry, option flags."),
        (Col "Source" "S72" "CustomSource" "" $null $null $null "" "The table reference of the source of the code."),
        (Col "Target" "S255" "Formatted" "" $null $null $null "" "Excecution parameter, depends on the type of custom action")) }
    "Dialog" = @{ Keys = 1; Columns = @(
        (Col "Dialog" "s72" "Identifier" "" $null $null $null "" "Name of the dialog."),
        (Col "HCentering" "i2" "" "" $null 0 100 "" "Horizontal position of the dialog on a 0-100 scale. 0 means left end, 100 means right end of the screen, 50 center."),
        (Col "VCentering" "i2" "" "" $null 0 100 "" "Vertical position of the dialog on a 0-100 scale. 0 means top end, 100 means bottom end of the screen, 50 center."),
        (Col "Width" "i2" "" "" $null 0 32767 "" "Width of the bounding rectangle of the dialog."),
        (Col "Height" "i2" "" "" $null 0 32767 "" "Height of the bounding rectangle of the dialog."),
        (Col "Attributes" "I4" "" "" $null 0 2147483647 "" "A 32-bit word that specifies the attribute flags to be applied to this dialog."),
        (Col "Title" "L128" "Formatted" "" $null $null $null "" "A text string specifying the title to be displayed in the title bar of the dialog's window."),
        (Col "Control_First" "s50" "Identifier" "Control" 2 $null $null "" "Defines the control that has the focus when the dialog is created."),
        (Col "Control_Default" "S50" "Identifier" "Control" 2 $null $null "" "Defines the default control. Hitting return is equivalent to pushing this button."),
        (Col "Control_Cancel" "S50" "Identifier" "Control" 2 $null $null "" "Defines the cancel control. Hitting escape or clicking on the close icon on the dialog is equivalent to pushing this button.")) }
    "Control" = @{ Keys = 2; Columns = @(
        (Col "Dialog_" "s72" "Identifier" "Dialog" 1 $null $null "" "External key to the Dialog table, name of the dialog."),
        (Col "Control" "s50" "Identifier" "" $null $null $null "" "Name of the control. This name must be unique within a dialog, but can repeat on different dialogs."),
        (Col "Type" "s20" "Identifier" "" $null $null $null "" "The type of the control."),
        (Col "X" "i2" "" "" $null 0 32767 "" "Horizontal coordinate of the upper left corner of the bounding rectangle of the control."),
        (Col "Y" "i2" "" "" $null 0 32767 "" "Vertical coordinate of the upper left corner of the bounding rectangle of the control."),
        (Col "Width" "i2" "" "" $null 0 32767 "" "Width of the bounding rectangle of the control."),
        (Col "Height" "i2" "" "" $null 0 32767 "" "Height of the bounding rectangle of the control."),
        (Col "Attributes" "I4" "" "" $null 0 2147483647 "" "A 32-bit word that specifies the attribute flags to be applied to this control."),
        (Col "Property" "S50" "Identifier" "" $null $null $null "" "The name of a defined property to be linked to this control."),
        (Col "Text" "L0" "Formatted" "" $null $null $null "" "A string used to set the initial text contained within a control (if appropriate)."),
        (Col "Control_Next" "S50" "Identifier" "Control" 2 $null $null "" "The name of an other control on the same dialog. This link defines the tab order of the controls. The links have to form one or more cycles!"),
        (Col "Help" "L50" "Text" "" $null $null $null "" "The help strings used with the button. The text is optional.")) }
    "ControlEvent" = @{ Keys = 5; Columns = @(
        (Col "Dialog_" "s72" "Identifier" "Dialog" 1 $null $null "" "A foreign key to the Dialog table, name of the dialog."),
        (Col "Control_" "s50" "Identifier" "Control" 2 $null $null "" "A foreign key to the Control table, name of the control"),
        (Col "Event" "s50" "Formatted" "" $null $null $null "" "An identifier that specifies the type of the event that should take place when the user interacts with control specified by the first two entries."),
        (Col "Argument" "s255" "Formatted" "" $null $null $null "" "A value to be used as a modifier when triggering a particular event."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "A standard conditional statement that specifies under which conditions the event should be triggered."),
        (Col "Ordering" "I2" "" "" $null 0 2147483647 "" "An integer used to order several events tied to the same control. Can be left blank.")) }
    "EventMapping" = @{ Keys = 3; Columns = @(
        (Col "Dialog_" "s72" "Identifier" "Dialog" 1 $null $null "" "A foreign key to the Dialog table, name of the Dialog."),
        (Col "Control_" "s50" "Identifier" "Control" 2 $null $null "" "A foreign key to the Control table, name of the control."),
        (Col "Event" "s50" "Identifier" "" $null $null $null "" "An identifier that specifies the type of the event that the control subscribes to."),
        (Col "Attribute" "s50" "Identifier" "" $null $null $null "" "The name of the control attribute, that is set when this event is received.")) }
    "RadioButton" = @{ Keys = 2; Columns = @(
        (Col "Property" "s72" "Identifier" "" $null $null $null "" "A named property to be tied to this radio button. All the buttons tied to the same property become part of the same group."),
        (Col "Order" "i2" "" "" $null 1 32767 "" "A positive integer used to determine the ordering of the items within one list."),
        (Col "Value" "s64" "Formatted" "" $null $null $null "" "The value string associated with this button. Selecting the button will set the associated property to this value."),
        (Col "X" "i2" "" "" $null 0 32767 "" "The horizontal coordinate of the upper left corner of the bounding rectangle of the radio button."),
        (Col "Y" "i2" "" "" $null 0 32767 "" "The vertical coordinate of the upper left corner of the bounding rectangle of the radio button."),
        (Col "Width" "i2" "" "" $null 0 32767 "" "The width of the button."),
        (Col "Height" "i2" "" "" $null 0 32767 "" "The height of the button."),
        (Col "Text" "L64" "Text" "" $null $null $null "" "The visible title to be assigned to the radio button."),
        (Col "Help" "L50" "Text" "" $null $null $null "" "The help strings used with the button. The text is optional.")) }
    "Condition" = @{ Keys = 2; Columns = @(
        (Col "Feature_" "s38" "Identifier" "Feature" 1 $null $null "" "Reference to a Feature entry in Feature table."),
        (Col "Level" "i2" "" "" $null 0 32767 "" "New selection Level to set in Feature table if Condition evaluates to TRUE."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "Expression evaluated to determine if Level in the Feature table is to change.")) }
    "TextStyle" = @{ Keys = 1; Columns = @(
        (Col "TextStyle" "s72" "Identifier" "" $null $null $null "" "Name of the style. The primary key of this table. This name is embedded in the texts to indicate a style change."),
        (Col "FaceName" "s32" "Text" "" $null $null $null "" "A string indicating the name of the font used. Required. The string must be at most 31 characters long."),
        (Col "Size" "i2" "" "" $null 0 32767 "" "The size of the font used. This size is given in our units (1/12 of the system font height). Assuming that the system font is set to 12 point size, this is equivalent to the point size."),
        (Col "Color" "I4" "" "" $null 0 16777215 "" "A long integer indicating the color of the string in the RGB format (Red, Green, Blue each 0-255, RGB = R + 256*G + 256^2*B)."),
        (Col "StyleBits" "I2" "" "" $null 0 15 "" "A combination of style bits.")) }
    "ActionText" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Name of action to be described."),
        (Col "Description" "L0" "Text" "" $null $null $null "" "Localized description displayed in progress dialog and log when action is executing."),
        (Col "Template" "L0" "Template" "" $null $null $null "" "Optional localized format template used to format action data records for display during action execution.")) }
    "UIText" = @{ Keys = 1; Columns = @(
        (Col "Key" "s72" "Identifier" "" $null $null $null "" "A unique key that identifies the particular string."),
        (Col "Text" "L255" "Text" "" $null $null $null "" "The localized version of the string.")) }
    "InstallExecuteSequence" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Name of action to invoke, either built-in or custom."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "Optional expression which skips the action if evaluates to expFalse.If the expression syntax is invalid, the engine will terminate, returning iesBadActionData."),
        (Col "Sequence" "I2" "" "" $null -4 32767 "" "Number that determines the sort order in which the actions are to be executed.  Leave blank to suppress action.")) }
    "InstallUISequence" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Name of action to invoke, either built-in or custom."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "Optional expression which skips the action if evaluates to expFalse.If the expression syntax is invalid, the engine will terminate, returning iesBadActionData."),
        (Col "Sequence" "I2" "" "" $null -4 32767 "" "Number that determines the sort order in which the actions are to be executed.  Leave blank to suppress action.")) }
    "AdminExecuteSequence" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Name of action to invoke, either built-in or custom."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "Optional expression which skips the action if evaluates to expFalse.If the expression syntax is invalid, the engine will terminate, returning iesBadActionData."),
        (Col "Sequence" "I2" "" "" $null -4 32767 "" "Number that determines the sort order in which the actions are to be executed.  Leave blank to suppress action.")) }
    "AdminUISequence" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Name of action to invoke, either built-in or custom."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "Optional expression which skips the action if evaluates to expFalse.If the expression syntax is invalid, the engine will terminate, returning iesBadActionData."),
        (Col "Sequence" "I2" "" "" $null -4 32767 "" "Number that determines the sort order in which the actions are to be executed.  Leave blank to suppress action.")) }
    "AdvtExecuteSequence" = @{ Keys = 1; Columns = @(
        (Col "Action" "s72" "Identifier" "" $null $null $null "" "Name of action to invoke, either built-in or custom."),
        (Col "Condition" "S255" "Condition" "" $null $null $null "" "Optional expression which skips the action if evaluates to expFalse.If the expression syntax is invalid, the engine will terminate, returning iesBadActionData."),
        (Col "Sequence" "I2" "" "" $null -4 32767 "" "Number that determines the sort order in which the actions are to be executed.  Leave blank to suppress action.")) }
}

# The SQL that creates a table of the schema: `CREATE TABLE `Name` (`Col` CHAR(72) NOT NULL, ... PRIMARY KEY `Col`)`.
function Get-CreateTableSql([string]$table) {
    $t = $MsiSchema[$table]
    $defs = @()
    foreach ($c in $t.Columns) {
        $kind = $c.Type.Substring(0, 1)
        $width = [int]$c.Type.Substring(1)
        $nullable = ($kind -ceq $kind.ToUpperInvariant())
        $sql = switch ($kind.ToLowerInvariant()) {
            "s" { if ($width -eq 0) { "LONGCHAR" } else { "CHAR($width)" } }
            "l" { if ($width -eq 0) { "LONGCHAR" } else { "CHAR($width)" } }
            "i" { if ($width -eq 2) { "SHORT" } else { "LONG" } }
            "v" { "OBJECT" }
        }
        if (-not $nullable) { $sql += " NOT NULL" }
        if ($kind.ToLowerInvariant() -eq "l") { $sql += " LOCALIZABLE" }
        $defs += "``$($c.Name)`` $sql"
    }
    $keys = @($t.Columns[0..($t.Keys - 1)] | ForEach-Object { "``$($_.Name)``" }) -join ", "
    "CREATE TABLE ``$table`` (" + ($defs -join ", ") + " PRIMARY KEY " + $keys + ")"
}

# The _Validation rows of a table: Table, Column, Nullable, MinValue, MaxValue, KeyTable, KeyColumn, Category, Set, Description
function Get-ValidationRows([string]$table) {
    $t = $MsiSchema[$table]
    $i = 0
    foreach ($c in $t.Columns) {
        $i++
        $kind = $c.Type.Substring(0, 1)
        $nullable = if ($kind -ceq $kind.ToUpperInvariant()) { "Y" } else { "N" }
        , @($table, $c.Name, $nullable, $c.Min, $c.Max, $c.KeyTable, $c.KeyColumn, $c.Category, $c.Set, $c.Description)
    }
}
