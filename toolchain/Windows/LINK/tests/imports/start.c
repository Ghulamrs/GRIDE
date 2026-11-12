/* start.c - no runtime, so what imports.ps1 sees is what its import libraries gave: each call must reach its DLL.
   c90 lists a program's externals by name, so AnyPopup is asked for first: inside a library of two DLLs that, not
   the DLL's name, puts USER32.dll before KERNEL32.dll */
int AnyPopup(void);
void *GetDesktopWindow(void);
unsigned GetConsoleCP(void);
unsigned GetCurrentProcessId(void);
unsigned GetTickCount(void);
void ExitProcess(unsigned);

void start(void)
{
    AnyPopup();
    GetConsoleCP();
    GetTickCount();
    ExitProcess(GetCurrentProcessId() != 0 && GetDesktopWindow() != 0 ? 42 : 1);
}
