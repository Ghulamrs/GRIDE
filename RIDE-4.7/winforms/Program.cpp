
#include <cstdio>

#include <windows.h>

#include "MainForm.h"

#include "symbols.h"

using namespace System;
using namespace System::Windows::Forms;

// The window's own log, and the fault log beside it, live in %TEMP% - not in the working
// directory, which is wherever the shortcut said and used to leave RIDEGui.log in bin\ and
// examples\. (Through the environment: <windows.h>'s GetTempPath macro would rewrite the .NET method's name.)
static String^ LogPath(String^ leaf) {
    String^ temp = Environment::GetEnvironmentVariable("TEMP");
    if (temp == nullptr || temp->Length == 0) temp = Environment::GetEnvironmentVariable("TMP");
    if (temp == nullptr || temp->Length == 0) temp = ".";
    return System::IO::Path::Combine(temp, leaf);
}

// Kept to a megabyte: past that the old one is set aside as .old, once, and a new one begun.
static void Note(String^ what) {
    try {
        String^ log = LogPath(gcnew String(ride_product_name()) + ".log");
        System::IO::FileInfo^ now = gcnew System::IO::FileInfo(log);
        if (now->Exists && now->Length > 1024 * 1024) {
            System::IO::File::Delete(log + ".old");
            System::IO::File::Move(log, log + ".old");
        }
        System::IO::File::AppendAllText(log, DateTime::Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + what +
                                                 Environment::NewLine);
    } catch (Exception^) {

    }
}

static void OnUnhandled(Object^, UnhandledExceptionEventArgs^ e) {
    Note("unhandled: " + e->ExceptionObject->ToString());
}

// A handler that throws is logged and said in a few words, not WinForms' Continue/Quit dialog;
// the window carries on, as Continue would have.
ref struct Handlers {
    static void OnThreadException(Object^, System::Threading::ThreadExceptionEventArgs^ e) {
        Note("in a handler: " + e->Exception->ToString());
        MessageBox::Show(e->Exception->Message + "\r\n\r\nThe window carries on; " +
                             LogPath(gcnew String(ride_product_name()) + ".log") + " has the details.",
                         gcnew String(ride_product_name()), MessageBoxButtons::OK, MessageBoxIcon::Warning);
    }
};

// A file named on the command line - by a file association or a drop on the icon - is a file to
// open, and a directory or a project file is a project; which one argv[0] was used to be guessed.
static bool IsProject(String^ named) {
    if (System::IO::Directory::Exists(named)) return true;
    return String::Equals(System::IO::Path::GetExtension(named), gcnew String(ride_project_suffix()),
                          StringComparison::OrdinalIgnoreCase);
}

// A hidden console, so that a child _popen starts has one and opens no window of its own - and the
// null device for the standard handles themselves, which a child inherits: freopen_s moves only the
// CRT's streams, and a child left reading the hidden console waited on a keyboard nobody could reach.
static void QuietConsoleForChildren() {
    if (GetConsoleWindow() != NULL) return;
    if (!AllocConsole()) return;

    HWND console = GetConsoleWindow();
    if (console != NULL) ShowWindow(console, SW_HIDE);

    FILE* ignored = NULL;
    freopen_s(&ignored, "NUL", "r", stdin);
    freopen_s(&ignored, "NUL", "w", stdout);
    freopen_s(&ignored, "NUL", "w", stderr);

    SECURITY_ATTRIBUTES inherited = {sizeof inherited, NULL, TRUE};
    HANDLE reads = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited,
                               OPEN_EXISTING, 0, NULL);
    HANDLE writes = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited,
                                OPEN_EXISTING, 0, NULL);
    if (reads != INVALID_HANDLE_VALUE) SetStdHandle(STD_INPUT_HANDLE, reads);
    if (writes != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, writes);
        SetStdHandle(STD_ERROR_HANDLE, writes);
    }
}

[STAThreadAttribute]
int main(array<String^>^ arguments) {
    // **`--version` answers and exits before any window.** It is the smoke test build.bat runs on
    // the box, where an ssh session has no desktop: a native global with a destructor kills the
    // mixed-mode start-up before main (settings.cpp says how), so reaching this line and leaving is the whole test.
    if (arguments->Length == 1 && arguments[0] == "--version") {
        Console::WriteLine(gcnew String(ride_product_name()) + " " + gcnew String(ride_version()));
        return 0;
    }
    Note("main entered");

    {
        array<Byte>^ bytes = System::Text::Encoding::UTF8->GetBytes(LogPath(gcnew String(ride_product_name()) + "-fault.log") + "\0");
        pin_ptr<Byte> pinned = &bytes[0];
        ride_watch_for_faults(reinterpret_cast<const char*>(pinned));
    }
    Note("faults watched");
    // Room kept at the bottom of the stack so the fault handler can still write when it overflows.
    ULONG reserve = 32 * 1024;
    SetThreadStackGuarantee(&reserve);
    QuietConsoleForChildren();
    Note("console quiet");
    AppDomain::CurrentDomain->UnhandledException +=
        gcnew UnhandledExceptionEventHandler(OnUnhandled);
    Application::SetUnhandledExceptionMode(UnhandledExceptionMode::CatchException);
    Application::ThreadException +=
        gcnew System::Threading::ThreadExceptionEventHandler(&Handlers::OnThreadException);

    try {
        Note("starting, " + arguments->Length + " arguments, in " +
             System::IO::Directory::GetCurrentDirectory());
        editor::installPlatformDemangler();
        Application::EnableVisualStyles();
        Application::SetCompatibleTextRenderingDefault(false);

        String^ directory = nullptr;
        System::Collections::Generic::List<String^>^ named = gcnew System::Collections::Generic::List<String^>();
        for (int i = 0; i < arguments->Length; ++i) {
            if (directory == nullptr && named->Count == 0 && IsProject(arguments[i])) directory = arguments[i];
            else named->Add(arguments[i]);
        }
        array<String^>^ files = named->ToArray();

        Note("building the window");
        ridegui::MainForm^ window = gcnew ridegui::MainForm(directory, files);
        Note("window built, running");
        Application::Run(window);
        Note("closed cleanly");
    } catch (Exception^ problem) {
        Note("caught: " + problem->ToString());
        return 1;
    }
    return 0;
}
