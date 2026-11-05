#include "notbad.h"
#include <commctrl.h>
#include <objbase.h>
#include "crashlog.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE   g_hInstance     = NULL;
HWND        g_hWndMain      = NULL;
HWND        g_hWndEdit      = NULL;
HWND        g_hWndStatus    = NULL;
HWND        g_hWndTabs      = NULL;
HWND        g_hWndTree      = NULL;
HWND        g_hWndLineNumbers = NULL;
HFONT       g_hFont         = NULL;
HFONT       g_hFontStatus   = NULL;
HFONT       g_hFontMenu     = NULL;
HACCEL      g_hAccel        = NULL;
HICON       g_hIcon         = NULL;
HICON       g_hIconSmall    = NULL;
HMENU       g_hMenu         = NULL;
HWND        g_hFindDialog   = NULL;
AppSettings g_settings      = {};

std::vector<Document*> g_documents;
Document*              g_pActiveDoc   = NULL;
int                    g_iActiveDoc   = -1;
BOOL                   g_bUpdatingTabs = FALSE;

void LoadStringResource(UINT id, LPWSTR buffer, int cchBuffer)
{
    LPCWSTR lpTranslated;

    if (buffer == NULL || cchBuffer <= 0)
        return;

    buffer[0] = L'\0';

    // Traducao do idioma carregado, se houver e couber no buffer; senao o
    // texto em ingles do proprio notbad.rc.
    lpTranslated = LangGetStringById(id);
    if (lpTranslated != NULL && lstrlenW(lpTranslated) < cchBuffer)
    {
        StringCchCopyW(buffer, (size_t)cchBuffer, lpTranslated);
        return;
    }

    LoadStringW(g_hInstance, id, buffer, cchBuffer);
}

BOOL InitApplication(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {};

    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = NotbadWndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_NOTBAD));
    wc.hIconSm       = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NOTBAD_TITLE),
                                        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = NOTBAD_CLASS;

    if (wc.hIcon == NULL)
        wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (wc.hIconSm == NULL)
        wc.hIconSm = (HICON)LoadImageW(NULL, IDI_APPLICATION, IMAGE_ICON, 16, 16, 0);

    g_hIcon      = wc.hIcon;
    g_hIconSmall = wc.hIconSm;

    if (RegisterClassExW(&wc) == 0)
        return FALSE;

    WNDCLASSEXW wcLine = {};
    wcLine.cbSize        = sizeof(wcLine);
    wcLine.style         = CS_HREDRAW | CS_VREDRAW;
    wcLine.lpfnWndProc   = LineNumbersWndProc;
    wcLine.hInstance     = hInstance;
    wcLine.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wcLine.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcLine.lpszClassName = NOTBAD_LINENUMBERS_CLASS;

    if (RegisterClassExW(&wcLine) == 0)
        return FALSE;

    g_hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR));

    return TRUE;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, LPWSTR lpCmdLine)
{
    WCHAR szPath[MAX_FILE_PATH] = {};
    LPCWSTR lpFile = NULL;

    g_hInstance = hInstance;

    SettingsLoad();
    SettingsLoadFont();
    LangInit();

    if (lpCmdLine != NULL && lpCmdLine[0] != L'\0')
    {
        while (*lpCmdLine == L' ' || *lpCmdLine == L'\t')
            ++lpCmdLine;

        if (*lpCmdLine != L'\0')
        {
            StringCchCopyW(szPath, ARRAYSIZE(szPath), lpCmdLine);
            PathUnquoteSpacesW(szPath);
            lpFile = szPath;
        }
    }

    if (!CreateNotbadWindow(hInstance, nCmdShow, lpFile))
        return FALSE;

    return TRUE;
}

int RunMessageLoop(void)
{
    MSG msg = {};

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (g_hFindDialog != NULL && IsDialogMessageW(g_hFindDialog, &msg))
            continue;

        if (g_hAccel != NULL && TranslateAcceleratorW(g_hWndMain, g_hAccel, &msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    int argc = 0;
    LPWSTR *argv;
    LPWSTR lpFile = NULL;

    (void)hPrevInstance;

    CrashLogInstall();

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 1;

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES |
                ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES |
                ICC_UPDOWN_CLASS | ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    SetProcessDPIAware();

    if (!InitApplication(hInstance))
    {
        CoUninitialize();
        return 1;
    }

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != NULL)
    {
        if (argc > 1)
            lpFile = argv[1];
    }

    if (!InitInstance(hInstance, nCmdShow, lpFile))
    {
        if (argv != NULL)
            LocalFree(argv);
        CoUninitialize();
        return 1;
    }

    if (argv != NULL)
        LocalFree(argv);

    int result = RunMessageLoop();

    CoUninitialize();
    return result;
}
