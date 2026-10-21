// =============================================================================
//  Main.cpp - GRIDE entry point
//  GRIDE.exe [workspace.grws | project.pro | ccs-folder | file.c ...]
// =============================================================================
#include "pch.h"
#include "UI/MainForm.h"

using namespace Gride::UI;

// GRIDE's version, which Help > About GRIDE shows (and the Help's Versions list): the Black outlook, 1.5 - between
// White's 1.0 and Grey's 2.0
[assembly: System::Reflection::AssemblyVersionAttribute("1.5.0.0")];

namespace Gride {

    // last-chance error handler: show the error instead of crashing silently
    ref class AppErrors abstract sealed
    {
    public:
        static void OnThreadException(Object^ sender, System::Threading::ThreadExceptionEventArgs^ e)
        {
            MessageBox::Show(e->Exception->ToString(), "GRIDE - unexpected error", MessageBoxButtons::OK, MessageBoxIcon::Error);
        }
    };
}

[STAThreadAttribute]
int main(array<String^>^ args)
{
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);
    Application::SetUnhandledExceptionMode(UnhandledExceptionMode::CatchException);
    Application::ThreadException += gcnew System::Threading::ThreadExceptionEventHandler(&Gride::AppErrors::OnThreadException);
    Application::Run(gcnew MainForm(args));
    return 0;
}
