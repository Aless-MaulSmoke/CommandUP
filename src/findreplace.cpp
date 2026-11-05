#include "notbad.h"
#include <richedit.h>
#include <wctype.h>

static WCHAR g_szFind[512] = {};
static WCHAR g_szReplace[512] = {};
static BOOL g_bMatchCase = FALSE;
static BOOL g_bWholeWord = FALSE;
static BOOL g_bAllFiles = FALSE;
static BOOL g_bSearchDown = TRUE;
static BOOL g_bReplaceMode = FALSE;
static BOOL g_bUpdatingControls = FALSE;

static BOOL IsWordChar(WCHAR ch)
{
    return iswalnum(ch) || ch == L'_';
}

static int FindInBuffer(LPCWSTR lpText, int cchText, LPCWSTR lpFind,
                        int start, BOOL bDown, BOOL bMatchCase, BOOL bWholeWord)
{
    int cchFind = lstrlenW(lpFind);
    int i;

    if (cchFind == 0)
        return -1;

    if (bDown)
    {
        for (i = start; i <= cchText - cchFind; ++i)
        {
            int cmp = bMatchCase
                    ? wcsncmp(lpText + i, lpFind, cchFind)
                    : _wcsnicmp(lpText + i, lpFind, cchFind);

            if (cmp == 0)
            {
                if (!bWholeWord)
                    return i;
                if ((i == 0 || !IsWordChar(lpText[i - 1])) &&
                    (i + cchFind >= cchText || !IsWordChar(lpText[i + cchFind])))
                    return i;
            }
        }
    }
    else
    {
        int last = start;
        if (last > cchText - cchFind)
            last = cchText - cchFind;

        for (i = last; i >= 0; --i)
        {
            int cmp = bMatchCase
                    ? wcsncmp(lpText + i, lpFind, cchFind)
                    : _wcsnicmp(lpText + i, lpFind, cchFind);

            if (cmp == 0)
            {
                if (!bWholeWord)
                    return i;
                if ((i == 0 || !IsWordChar(lpText[i - 1])) &&
                    (i + cchFind >= cchText || !IsWordChar(lpText[i + cchFind])))
                    return i;
            }
        }
    }

    return -1;
}

// IMPORTANTE: WM_GETTEXT/GetWindowText devolvem CRLF, mas as posicoes do
// RichEdit (EM_EXGETSEL/EM_EXSETSEL) contam apenas CR. Misturar os dois
// desalinha toda busca depois da primeira quebra de linha. EM_GETTEXTEX com
// GT_DEFAULT devolve o texto interno (CR), entao os indices batem.
static LPWSTR GetDocumentText(Document* pDoc, int* pcchText)
{
    GETTEXTLENGTHEX gtl = {};
    GETTEXTEX gt = {};
    LPWSTR lpText;
    int length;
    int copied;

    *pcchText = 0;
    if (pDoc == NULL)
        return NULL;

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;    // CP_UTF16
    length = (int)SendMessageW(pDoc->hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (length < 0)
        length = 0;

    lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpText == NULL)
        return NULL;

    gt.cb = (DWORD)(((SIZE_T)length + 1) * sizeof(WCHAR));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    copied = (int)SendMessageW(pDoc->hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)lpText);
    if (copied < 0 || copied > length)
        copied = length;
    lpText[copied] = L'\0';

    *pcchText = copied;
    return lpText;
}

static void SetFindStatus(LPCWSTR lpText)
{
    if (g_hFindDialog != NULL)
        SetDlgItemTextW(g_hFindDialog, IDC_FIND_STATUS, lpText);
}

static void ReportNotFound(void)
{
    WCHAR szFormat[256];
    WCHAR szMessage[512];

    LoadStringResource(IDS_FIND_NOT_FOUND, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat, g_szFind);

    if (g_hFindDialog != NULL)
        SetFindStatus(szMessage);
    else
        MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME, MB_ICONINFORMATION | MB_OK);
}

static void ReadControls(HWND hDlg)
{
    if (hDlg == NULL)
        return;

    GetDlgItemTextW(hDlg, IDC_FIND_EDIT, g_szFind, ARRAYSIZE(g_szFind));
    GetDlgItemTextW(hDlg, IDC_REPLACE_EDIT, g_szReplace, ARRAYSIZE(g_szReplace));
    g_bMatchCase = IsDlgButtonChecked(hDlg, IDC_MATCHCASE) == BST_CHECKED;
    g_bWholeWord = IsDlgButtonChecked(hDlg, IDC_WHOLEWORD) == BST_CHECKED;
    g_bAllFiles = IsDlgButtonChecked(hDlg, IDC_ALLFILES) == BST_CHECKED;
    g_bSearchDown = IsDlgButtonChecked(hDlg, IDC_DIRDOWN) == BST_CHECKED;
}

static void UpdateDialogControls(HWND hDlg)
{
    if (hDlg == NULL)
        return;

    WCHAR szTitle[64];

    g_bUpdatingControls = TRUE;

    LoadStringResource(g_bReplaceMode ? IDS_REPLACE_TITLE : IDS_FIND_TITLE,
                       szTitle, ARRAYSIZE(szTitle));
    SetWindowTextW(hDlg, szTitle);
    SetDlgItemTextW(hDlg, IDC_FIND_EDIT, g_szFind);
    SetDlgItemTextW(hDlg, IDC_REPLACE_EDIT, g_szReplace);
    CheckDlgButton(hDlg, IDC_MATCHCASE, g_bMatchCase ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_WHOLEWORD, g_bWholeWord ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_ALLFILES, g_bAllFiles ? BST_CHECKED : BST_UNCHECKED);

    CheckRadioButton(hDlg, IDC_DIRDOWN, IDC_DIRUP,
                     g_bSearchDown ? IDC_DIRDOWN : IDC_DIRUP);

    ShowWindow(GetDlgItem(hDlg, IDC_REPLACE_LABEL), g_bReplaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_REPLACE_EDIT), g_bReplaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_REPLACE), g_bReplaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_REPLACEALL), g_bReplaceMode ? SW_SHOW : SW_HIDE);

    g_bUpdatingControls = FALSE;
}

static void SelectFound(Document* pDoc, int found, int cchFind)
{
    CHARRANGE cr;

    if (pDoc == NULL)
        return;

    if (pDoc != g_pActiveDoc)
        DocActivate(pDoc);

    cr.cpMin = found;
    cr.cpMax = found + cchFind;
    SendMessageW(pDoc->hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(pDoc->hEdit, EM_SCROLLCARET, 0, 0);
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);

    if (g_hFindDialog != NULL)
        SetFocus(g_hFindDialog);
}

static void DoFind(BOOL bFromDialog)
{
    CHARRANGE cr;
    Document* pFoundDoc = NULL;
    int foundPos = -1;
    int cchFind;
    int startIndex;
    int count;
    int n;

    if (g_hFindDialog != NULL)
        ReadControls(g_hFindDialog);

    if (g_szFind[0] == L'\0')
        return;

    count = (int)g_documents.size();
    if (count == 0)
        return;

    startIndex = DocIndexOf(g_pActiveDoc);
    if (startIndex < 0)
        startIndex = 0;

    cchFind = lstrlenW(g_szFind);

    for (n = 0; n < (g_bAllFiles ? count : 1); ++n)
    {
        int index;
        Document* pDoc;
        LPWSTR lpText;
        int cchText;
        int start;
        int found;

        index = g_bSearchDown
              ? (startIndex + n) % count
              : (startIndex - n + count) % count;
        pDoc = g_documents[index];

        lpText = GetDocumentText(pDoc, &cchText);
        if (lpText == NULL)
            continue;

        if (n == 0)
        {
            SendMessageW(pDoc->hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            start = g_bSearchDown ? cr.cpMax : cr.cpMin;
        }
        else
        {
            start = g_bSearchDown ? 0 : cchText;
        }

        if (start > cchText)
            start = cchText;

        // Para cima, a busca comeca um caractere antes do inicio da selecao
        // (senao o resultado atual seria encontrado de novo).
        if (n == 0 && !g_bSearchDown)
            --start;

        if (start < 0)
            found = -1;
        else
            found = FindInBuffer(lpText, cchText, g_szFind, start,
                                 g_bSearchDown, g_bMatchCase, g_bWholeWord);
        LocalFree(lpText);

        if (found >= 0)
        {
            pFoundDoc = pDoc;
            foundPos = found;
            break;
        }
    }

    if (foundPos < 0 && g_pActiveDoc != NULL)
    {
        LPWSTR lpText;
        int cchText;
        int start;
        int found;

        lpText = GetDocumentText(g_pActiveDoc, &cchText);
        if (lpText != NULL)
        {
            start = g_bSearchDown ? 0 : cchText;
            found = FindInBuffer(lpText, cchText, g_szFind, start,
                                 g_bSearchDown, g_bMatchCase, g_bWholeWord);
            LocalFree(lpText);

            if (found >= 0)
            {
                pFoundDoc = g_pActiveDoc;
                foundPos = found;
            }
        }
    }

    if (foundPos >= 0)
    {
        SetFindStatus(L"");
        SelectFound(pFoundDoc, foundPos, cchFind);
    }
    else if (bFromDialog)
    {
        ReportNotFound();
    }
}

static void DoReplace(void)
{
    CHARRANGE cr;
    int cchFind;
    int cchText = 0;
    LPWSTR lpText;
    BOOL matches = FALSE;

    if (g_hFindDialog != NULL)
        ReadControls(g_hFindDialog);

    if (g_szFind[0] == L'\0' || g_pActiveDoc == NULL)
        return;

    cchFind = lstrlenW(g_szFind);
    SendMessageW(g_pActiveDoc->hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    if (cr.cpMax - cr.cpMin == cchFind)
    {
        lpText = GetDocumentText(g_pActiveDoc, &cchText);
        if (lpText != NULL)
        {
            if (cr.cpMin >= 0 && cr.cpMax <= cchText)
            {
                int cmp = g_bMatchCase
                        ? wcsncmp(lpText + cr.cpMin, g_szFind, cchFind)
                        : _wcsnicmp(lpText + cr.cpMin, g_szFind, cchFind);
                matches = (cmp == 0);
            }
            LocalFree(lpText);
        }
    }

    if (matches)
    {
        SendMessageW(g_pActiveDoc->hEdit, EM_REPLACESEL, TRUE, (LPARAM)g_szReplace);

        // Buscando para cima, o cursor volta ao inicio do trecho substituido
        // para que a proxima busca siga para o resultado anterior.
        if (!g_bSearchDown)
            SendMessageW(g_pActiveDoc->hEdit, EM_SETSEL, cr.cpMin, cr.cpMin);
        DocUpdateTabTitle(g_pActiveDoc);
        UpdateWindowTitle();
        UpdateStatusBar();
        UpdateEditMenuState(g_hWndMain);
    }

    DoFind(FALSE);
}

static int ReplaceAllInDocument(Document* pDoc)
{
    int cchFind = lstrlenW(g_szFind);
    int cchReplace = lstrlenW(g_szReplace);
    int cchText = 0;
    int start = 0;
    int count = 0;
    LPWSTR lpText;

    if (pDoc == NULL || cchFind == 0)
        return 0;

    lpText = GetDocumentText(pDoc, &cchText);
    if (lpText == NULL)
        return 0;

    SendMessageW(pDoc->hEdit, WM_SETREDRAW, FALSE, 0);

    for (;;)
    {
        int found = FindInBuffer(lpText, cchText, g_szFind, start,
                                 TRUE, g_bMatchCase, g_bWholeWord);
        CHARRANGE cr;

        if (found < 0)
            break;

        cr.cpMin = found;
        cr.cpMax = found + cchFind;
        SendMessageW(pDoc->hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(pDoc->hEdit, EM_REPLACESEL, FALSE, (LPARAM)g_szReplace);
        ++count;

        LocalFree(lpText);
        lpText = GetDocumentText(pDoc, &cchText);
        if (lpText == NULL)
            break;

        start = found + cchReplace;
    }

    if (lpText != NULL)
        LocalFree(lpText);

    SendMessageW(pDoc->hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(pDoc->hEdit, NULL, TRUE);

    if (count > 0)
    {
        DocUpdateTabTitle(pDoc);
        if (pDoc == g_pActiveDoc)
        {
            UpdateWindowTitle();
            UpdateStatusBar();
        }
    }

    return count;
}

static void DoReplaceAll(void)
{
    int total = 0;
    WCHAR szFormat[128];
    WCHAR szMessage[256];
    size_t i;

    if (g_hFindDialog != NULL)
        ReadControls(g_hFindDialog);

    if (g_szFind[0] == L'\0')
        return;

    if (g_bAllFiles)
    {
        for (i = 0; i < g_documents.size(); ++i)
            total += ReplaceAllInDocument(g_documents[i]);
    }
    else
    {
        total = ReplaceAllInDocument(g_pActiveDoc);
    }

    LoadStringResource(IDS_REPLACED_COUNT, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat, total);
    SetFindStatus(szMessage);

    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
}

static INT_PTR CALLBACK FindDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        g_hFindDialog = hDlg;
        g_bReplaceMode = (BOOL)lParam;
        LangApplyDialog(hDlg, IDD_FIND);
        SendDlgItemMessageW(hDlg, IDC_FIND_EDIT, EM_SETLIMITTEXT,
                            ARRAYSIZE(g_szFind) - 1, 0);
        SendDlgItemMessageW(hDlg, IDC_REPLACE_EDIT, EM_SETLIMITTEXT,
                            ARRAYSIZE(g_szReplace) - 1, 0);
        UpdateDialogControls(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_FIND_EDIT:
        case IDC_REPLACE_EDIT:
            if (HIWORD(wParam) == EN_CHANGE && !g_bUpdatingControls)
                ReadControls(hDlg);
            return TRUE;

        case IDC_FIND_NEXT:
        case IDOK:
            ReadControls(hDlg);
            DoFind(TRUE);
            return TRUE;

        case IDC_REPLACE:
            ReadControls(hDlg);
            DoReplace();
            return TRUE;

        case IDC_REPLACEALL:
            ReadControls(hDlg);
            DoReplaceAll();
            return TRUE;

        case IDC_MATCHCASE:
        case IDC_WHOLEWORD:
        case IDC_ALLFILES:
        case IDC_DIRDOWN:
        case IDC_DIRUP:
            ReadControls(hDlg);
            return TRUE;

        case IDCANCEL:
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY:
        g_hFindDialog = NULL;
        return TRUE;
    }

    return FALSE;
}

void FindTextDialog(BOOL bReplace)
{
    g_bReplaceMode = bReplace;

    if (g_hFindDialog != NULL)
    {
        UpdateDialogControls(g_hFindDialog);
        ShowWindow(g_hFindDialog, SW_SHOWNORMAL);
        SetForegroundWindow(g_hFindDialog);
        SetFocus(GetDlgItem(g_hFindDialog,
                            bReplace ? IDC_REPLACE_EDIT : IDC_FIND_EDIT));
        return;
    }

    g_hFindDialog = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_FIND),
                                       g_hWndMain, FindDlgProc,
                                       (LPARAM)(bReplace ? 1 : 0));
    if (g_hFindDialog != NULL)
    {
        ShowWindow(g_hFindDialog, SW_SHOW);
        SetForegroundWindow(g_hFindDialog);
        SetFocus(GetDlgItem(g_hFindDialog,
                            bReplace ? IDC_REPLACE_EDIT : IDC_FIND_EDIT));
    }
}

void FindNext(void)
{
    if (g_hFindDialog != NULL)
        ReadControls(g_hFindDialog);

    if (g_szFind[0] == L'\0')
    {
        FindTextDialog(FALSE);
        return;
    }

    DoFind(TRUE);
}
