#include "notbad.h"
#include <commctrl.h>
#include <richedit.h>

static PAGESETUPDLGW g_pageSetup;
static BOOL          g_pageSetupInit = FALSE;
static HBITMAP       g_hAboutLogo = NULL;

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR szVersion[64];
        WCHAR szFormat[64];

        LangApplyDialog(hDlg, IDD_ABOUT);

        LoadStringResource(IDS_ABOUT_VERSION, szFormat, ARRAYSIZE(szFormat));
        StringCchPrintfW(szVersion, ARRAYSIZE(szVersion), szFormat, NOTBAD_VERSION);
        SetDlgItemTextW(hDlg, IDC_ABOUT_VERSION, szVersion);

        if (g_hAboutLogo != NULL)
        {
            DeleteObject(g_hAboutLogo);
            g_hAboutLogo = NULL;
        }
        g_hAboutLogo = LoadBitmapW(g_hInstance, MAKEINTRESOURCEW(IDB_ABOUT_LOGO));
        if (g_hAboutLogo != NULL)
        {
            SendDlgItemMessageW(hDlg, IDC_ABOUT_IMAGE, STM_SETIMAGE,
                                (WPARAM)IMAGE_BITMAP, (LPARAM)g_hAboutLogo);
        }
        return TRUE;
    }

    case WM_DESTROY:
        if (g_hAboutLogo != NULL)
        {
            DeleteObject(g_hAboutLogo);
            g_hAboutLogo = NULL;
        }
        return TRUE;

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;

        if (pnmh != NULL && pnmh->idFrom == IDC_ABOUT_LINK &&
            (pnmh->code == NM_CLICK || pnmh->code == NM_RETURN))
        {
            PNMLINK pLink = (PNMLINK)lParam;
            LPCWSTR pszUrl = pLink->item.szUrl;

            if (pszUrl[0] == L'\0')
                pszUrl = L"https://github.com/Aless-MaulSmoke/Notbad";

            ShellExecuteW(hDlg, L"open", pszUrl, NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
        case IDC_ABOUT_OK:
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

void ShowAboutDialog(HWND hWnd)
{
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUT), hWnd,
                    AboutDlgProc, 0);
}

struct IndentDlgData
{
    int convert;    // 0 = nenhuma, 1 = tabs -> espacos, 2 = espacos -> tabs
};

static INT_PTR CALLBACK IndentDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hEdit = GetDlgItem(hDlg, IDC_INDENT_SIZE);
        HWND hSpin;

        LangApplyDialog(hDlg, IDD_INDENT);

        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);

        CheckDlgButton(hDlg, IDC_INDENT_AUTO,
                       g_settings.autoIndent ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(hDlg, IDC_INDENT_SPACES, IDC_INDENT_TABS,
                         g_settings.indentWithTabs ? IDC_INDENT_TABS : IDC_INDENT_SPACES);
        CheckRadioButton(hDlg, IDC_INDENT_NONE, IDC_INDENT_SP2TABS, IDC_INDENT_NONE);

        SendMessageW(hEdit, EM_LIMITTEXT, 2, 0);
        hSpin = CreateWindowExW(0, UPDOWN_CLASSW, NULL,
                                WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT |
                                UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                0, 0, 0, 0, hDlg,
                                (HMENU)(INT_PTR)IDC_INDENT_SPIN, g_hInstance, NULL);
        if (hSpin != NULL)
        {
            SendMessageW(hSpin, UDM_SETBUDDY, (WPARAM)hEdit, 0);
            SendMessageW(hSpin, UDM_SETRANGE32, 1, 32);
            SendMessageW(hSpin, UDM_SETPOS32, 0, g_settings.tabWidth);
        }
        else
        {
            SetDlgItemInt(hDlg, IDC_INDENT_SIZE, (UINT)g_settings.tabWidth, FALSE);
        }

        if (g_hWndEdit == NULL)
        {
            EnableWindow(GetDlgItem(hDlg, IDC_INDENT_TABS2SP), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_INDENT_SP2TABS), FALSE);
        }
        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
        // Etiqueta do atalho (Ctrl+Shift+I): cor de texto desabilitado, como o
        // atalho exibido nos itens de menu.
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_INDENT_AUTO_HINT))
        {
            SetTextColor((HDC)wParam, GetSysColor(COLOR_GRAYTEXT));
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            IndentDlgData* pData = (IndentDlgData*)GetWindowLongPtrW(hDlg, DWLP_USER);
            BOOL translated = FALSE;
            UINT size = GetDlgItemInt(hDlg, IDC_INDENT_SIZE, &translated, FALSE);

            if (translated)
            {
                if (size < 1)
                    size = 1;
                if (size > 32)
                    size = 32;
                g_settings.tabWidth = (int)size;
            }

            g_settings.autoIndent =
                (IsDlgButtonChecked(hDlg, IDC_INDENT_AUTO) == BST_CHECKED);
            g_settings.indentWithTabs =
                (IsDlgButtonChecked(hDlg, IDC_INDENT_TABS) == BST_CHECKED);
            SettingsSave();

            if (pData != NULL)
            {
                if (IsDlgButtonChecked(hDlg, IDC_INDENT_TABS2SP) == BST_CHECKED)
                    pData->convert = 1;
                else if (IsDlgButtonChecked(hDlg, IDC_INDENT_SP2TABS) == BST_CHECKED)
                    pData->convert = 2;
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

void ShowIndentDialog(HWND hWnd)
{
    IndentDlgData data = {};

    if (DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_INDENT), hWnd,
                        IndentDlgProc, (LPARAM)&data) != IDOK)
        return;

    // Largura visual do tab e estilo de indentacao na edicao corrente.
    EditorApplyIndentation();

    // A conversao roda depois que o dialogo fecha, ja com o tamanho de tab novo.
    if (data.convert == 1)
        EditorConvertTabs(TRUE);
    else if (data.convert == 2)
        EditorConvertTabs(FALSE);
}

static INT_PTR CALLBACK GoToDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        CHARRANGE cr = {};
        LangApplyDialog(hDlg, IDD_GOTO);
        SendMessageW(g_hWndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
        int line = EditorGetParagraphNumber(g_hWndEdit, (int)cr.cpMin);
        if (line < 1)
            line = 1;
        SetDlgItemInt(hDlg, IDC_GOTO_EDIT, line, FALSE);
        SendDlgItemMessageW(hDlg, IDC_GOTO_EDIT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, IDC_GOTO_EDIT));
        return FALSE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_GOTO_OK:
        {
            BOOL translated = FALSE;
            UINT line = GetDlgItemInt(hDlg, IDC_GOTO_EDIT, &translated, FALSE);
            int lineCount = EditorGetParagraphCount(g_hWndEdit);

            if (!translated || line == 0)
                line = 1;

            if ((int)line > lineCount)
                line = (UINT)lineCount;

            int lineIndex = EditorGetParagraphStartByNumber(g_hWndEdit, (int)line);
            SendMessageW(g_hWndEdit, EM_SETSEL, lineIndex, lineIndex);
            SendMessageW(g_hWndEdit, EM_SCROLLCARET, 0, 0);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
        case IDC_GOTO_CANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

void EditorGoToLine(void)
{
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_GOTO), g_hWndMain,
                    GoToDlgProc, 0);
}

void DoPageSetup(HWND hWnd)
{
    if (!g_pageSetupInit)
    {
        ZeroMemory(&g_pageSetup, sizeof(g_pageSetup));
        g_pageSetup.lStructSize = sizeof(g_pageSetup);
        g_pageSetup.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES;
        g_pageSetupInit = TRUE;
    }

    g_pageSetup.hwndOwner = hWnd;
    PageSetupDlgW(&g_pageSetup);
}

static HFONT CreatePrinterFont(HDC hdc)
{
    LOGFONTW lf = g_settings.logFont;
    int pointSize;
    int dpiY;
    HDC hScreen;
    int screenDpi;

    dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

    hScreen = GetDC(NULL);
    screenDpi = hScreen != NULL ? GetDeviceCaps(hScreen, LOGPIXELSY) : 96;
    ReleaseDC(NULL, hScreen);

    pointSize = MulDiv(-lf.lfHeight, 72, screenDpi);
    if (pointSize <= 0)
        pointSize = 10;

    lf.lfHeight = -MulDiv(pointSize, dpiY, 72);
    lf.lfWidth  = 0;

    return CreateFontIndirectW(&lf);
}

void DoPrint(HWND hWnd)
{
    PRINTDLGW pd;
    int cchText = 0;
    LPWSTR lpText = NULL;
    DOCINFOW doc;
    HDC hdc;
    HFONT hFont;
    HFONT hOldFont;
    int pageWidth;
    int pageHeight;
    int marginLeft;
    int marginTop;
    int marginRight;
    int marginBottom;
    RECT rcText;
    int textHeight;
    int pages;
    int page;
    WCHAR szDocName[MAX_PATH];

    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hWnd;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE |
               PD_NOSELECTION | PD_NOPAGENUMS;

    if (!PrintDlgW(&pd))
        return;

    hdc = pd.hDC;
    if (hdc == NULL)
        return;

    int length = GetWindowTextLengthW(g_hWndEdit);
    lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpText == NULL)
    {
        DeleteDC(hdc);
        return;
    }
    if (length > 0)
        GetWindowTextW(g_hWndEdit, lpText, length + 1);
    cchText = length;

    pageWidth  = GetDeviceCaps(hdc, HORZRES);
    pageHeight = GetDeviceCaps(hdc, VERTRES);

    marginLeft = MulDiv(g_pageSetup.rtMargin.left, GetDeviceCaps(hdc, LOGPIXELSX), 1000);
    marginTop  = MulDiv(g_pageSetup.rtMargin.top, GetDeviceCaps(hdc, LOGPIXELSY), 1000);
    marginRight = MulDiv(g_pageSetup.rtMargin.right, GetDeviceCaps(hdc, LOGPIXELSX), 1000);
    marginBottom = MulDiv(g_pageSetup.rtMargin.bottom, GetDeviceCaps(hdc, LOGPIXELSY), 1000);

    if (g_pageSetup.rtMargin.left == 0 && g_pageSetup.rtMargin.top == 0 &&
        g_pageSetup.rtMargin.right == 0 && g_pageSetup.rtMargin.bottom == 0)
    {
        marginLeft = marginRight = GetDeviceCaps(hdc, LOGPIXELSX) / 2;
        marginTop = marginBottom = GetDeviceCaps(hdc, LOGPIXELSY) / 2;
    }

    rcText.left   = marginLeft;
    rcText.top    = marginTop;
    rcText.right  = pageWidth - marginRight;
    rcText.bottom = 0;

    hFont = CreatePrinterFont(hdc);
    hOldFont = (HFONT)SelectObject(hdc, hFont);

    textHeight = DrawTextW(hdc, lpText, cchText, &rcText,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);

    pages = 1;
    if (textHeight > 0)
        pages = (textHeight + (pageHeight - marginTop - marginBottom) - 1) /
                (pageHeight - marginTop - marginBottom);

    DocGetDisplayName(g_pActiveDoc, szDocName, ARRAYSIZE(szDocName));

    ZeroMemory(&doc, sizeof(doc));
    doc.cbSize = sizeof(doc);
    doc.lpszDocName = szDocName;

    if (StartDocW(hdc, &doc) > 0)
    {
        for (page = 0; page < pages; ++page)
        {
            StartPage(hdc);

            SetViewportOrgEx(hdc, 0, -(page * (pageHeight - marginTop - marginBottom)), NULL);

            RECT rc = rcText;
            rc.bottom = rcText.top + textHeight;
            DrawTextW(hdc, lpText, cchText, &rc,
                      DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);

            SetViewportOrgEx(hdc, 0, 0, NULL);

            EndPage(hdc);
        }

        EndDoc(hdc);
    }

    SelectObject(hdc, hOldFont);
    if (hFont != NULL)
        DeleteObject(hFont);

    DeleteDC(hdc);
    if (pd.hDevMode != NULL)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames != NULL)
        GlobalFree(pd.hDevNames);

    LocalFree(lpText);
}

void ShowHelp(HWND hWnd)
{
    WCHAR szFormat[1024];
    WCHAR szMessage[1280];

    LoadStringResource(IDS_HELP_TEXT, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat, NOTBAD_VERSION);

    MessageBoxW(hWnd, szMessage, NOTBAD_NAME, MB_ICONINFORMATION | MB_OK);
}
