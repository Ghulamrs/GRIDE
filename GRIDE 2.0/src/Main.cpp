// ============================================================================
//  GRIDE 2.0 - entry point
//  All classes are header-only and compiled in this single translation unit.
// ============================================================================
#include "UI/MainForm.h"

using namespace System;
using namespace System::Windows::Forms;

namespace Gride
{
    ref class CrashHandler abstract sealed
    {
    public:
        static void OnThreadException(Object^ sender, System::Threading::ThreadExceptionEventArgs^ e)
        {
            MessageBox::Show(
                String::Format("An unexpected error occurred. Please save your work.\n\n{0}", e->Exception->ToString()),
                AppInfo::Name, MessageBoxButtons::OK, MessageBoxIcon::Error);
        }
    };
}

[STAThread]
int main(array<String^>^ args)
{
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);
    Application::SetUnhandledExceptionMode(UnhandledExceptionMode::CatchException);
    Application::ThreadException += gcnew System::Threading::ThreadExceptionEventHandler(&Gride::CrashHandler::OnThreadException);
    Application::Run(gcnew Gride::MainForm(args));
    return 0;
}
