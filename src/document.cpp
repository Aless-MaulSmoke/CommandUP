#include "notbad.h"
#include <commctrl.h>
#include <richedit.h>

static LPCWSTR g_pszRichEditClass = NULL;
static HMODULE g_hRichEditLib = NULL;

static LPCWSTR GetRichEditClass(void)
{
    if (g_pszRichEditClass != NULL)
        return g_pszRichEditClass;

    g_hRichEditLib = LoadLibraryW(L"Msftedit.dll");
    if (g_hRichEditLib != NULL)
    {
        g_pszRichEditClass = L"RICHEDIT50W";
        return g_pszRichEditClass;
    }

    g_hRichEditLib = LoadLibraryW(L"Riched20.dll");
    g_pszRichEditClass = L"RichEdit20W";
    return g_pszRichEditClass;
}

// Atualiza titulo da aba, da janela e menus depois de o estado "modificado"
// ter sido alterado por fora do fluxo normal de edicao.
static void RefreshModifiedUi(HWND hEdit)
{
    Document* pDoc = DocFromEdit(hEdit);

    if (pDoc == NULL)
        return;

    DocUpdateTabTitle(pDoc);
    if (pDoc == g_pActiveDoc)
        UpdateWindowTitle();
    UpdateEditMenuState(g_hWndMain);
}

static LRESULT CALLBACK RichEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    LRESULT result;
    Document* pDoc = (Document*)dwRefData;

    (void)uIdSubclass;

    // O clique que reativa uma janela inativa nao pode ser descartado pelo
    // RichEdit; MA_ACTIVATE garante que o botao chegue ao controle e que o
    // caret seja posicionado no ponto clicado logo no primeiro clique.
    if (msg == WM_MOUSEACTIVATE)
        return MA_ACTIVATE;

    // Shift e Ctrl sozinhos: o RichEdit usa o par Ctrl+Shift para trocar a
    // direcao de leitura do paragrafo, o que marcava como modificado um arquivo
    // limpo. Os atalhos com Shift/Ctrl consultam GetKeyState, entao descartar
    // o WM_KEYDOWN/WM_KEYUP dessas teclas isoladas nao afeta nenhum deles.
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) &&
        (wParam == VK_SHIFT || wParam == VK_CONTROL))
    {
        return 0;
    }

    // Ctrl+Shift tambem e o atalho do Windows para trocar o idioma do teclado.
    // A troca nao e uma edicao: se o RichEdit marcar o texto como modificado
    // por causa dela, o estado anterior e restaurado.
    if (msg == WM_INPUTLANGCHANGE)
    {
        BOOL wasModified = (BOOL)SendMessageW(hWnd, EM_GETMODIFY, 0, 0);

        result = DefSubclassProc(hWnd, msg, wParam, lParam);

        if (!wasModified && SendMessageW(hWnd, EM_GETMODIFY, 0, 0))
        {
            SendMessageW(hWnd, EM_SETMODIFY, FALSE, 0);
            RefreshModifiedUi(hWnd);
        }

        return result;
    }

    // Modo Overwrite: o caractere digitado substitui o que esta sob o cursor.
    if (msg == WM_CHAR && pDoc != NULL && pDoc->bOverwrite)
    {
        // Caminho normal: troca em uma operacao e descarta o WM_CHAR.
        if (EditorTryOverwriteChar(hWnd, (WCHAR)wParam))
            return 0;

        EditorHandleOverwriteChar(hWnd, (WCHAR)wParam);
    }

    // O RichEdit recria o caret no foco e na troca de fonte; o formato de
    // bloco do Overwrite precisa ser reaplicado depois disso.
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_SETFONT)
        EditorResetCaretShape();

    if (msg == WM_LBUTTONDOWN)
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

        SetFocus(hWnd);
        result = DefSubclassProc(hWnd, msg, wParam, lParam);

        if ((GetKeyState(VK_SHIFT) & 0x8000) == 0)
            EditorPlaceCaretAtPoint(hWnd, pt);

        // O RichEdit acabou de recriar o caret no ponto clicado: reaplica o
        // bloco do Overwrite.
        EditorUpdateCaretShape(hWnd);

        return result;
    }

    if (msg == WM_KEYDOWN && EditorHandleKeyDown(hWnd, wParam))
        return 0;

    result = DefSubclassProc(hWnd, msg, wParam, lParam);

    if (msg == WM_SETFOCUS)
    {
        EditorPlaceCaretAtClick(hWnd);
        EditorUpdateCaretShape(hWnd);
    }

    if (msg == WM_SETFONT)
    {
        // O WM_SETFONT mexe no formato padrao: reaplica a cor do texto.
        EditorApplyTextColorTo(hWnd);
        EditorUpdateCaretShape(hWnd);
    }

    if (msg == WM_KEYUP || msg == WM_LBUTTONUP)
    {
        if (DocFromEdit(hWnd) == g_pActiveDoc)
        {
            UpdateStatusBar();
            InvalidateLineNumbers();
        }
    }

    // Ctrl+roda muda o zoom so deste RichEdit: replica nas demais abas.
    if (msg == WM_MOUSEWHEEL && (LOWORD(wParam) & MK_CONTROL) != 0 &&
        DocFromEdit(hWnd) == g_pActiveDoc)
    {
        EditorSyncZoomFrom(hWnd);
    }

    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL)
        InvalidateLineNumbers();

    // Rolagem e zoom fazem o RichEdit recriar o caret.
    if (msg == WM_VSCROLL || msg == WM_HSCROLL || msg == WM_MOUSEWHEEL)
        EditorUpdateCaretShape(hWnd);

    return result;
}

static HWND CreateRichEdit(Document* pDoc)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                  ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                  ES_NOHIDESEL | ES_WANTRETURN;

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, GetRichEditClass(), L"",
                                 style, 0, 0, 0, 0,
                                 g_hWndMain, (HMENU)(INT_PTR)ID_EDIT,
                                 g_hInstance, NULL);
    if (hEdit == NULL)
        return NULL;

    EditorApplyWordWrapTo(hEdit, g_settings.wordWrap);
    SendMessageW(hEdit, EM_SETEDITSTYLE, SES_EXTENDBACKCOLOR, SES_EXTENDBACKCOLOR);

    SendMessageW(hEdit, EM_SETLIMITTEXT, MAX_EDIT_BUFFER, 0);
    SendMessageW(hEdit, EM_SETUNDOLIMIT, 1000, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL);
    SetWindowSubclass(hEdit, RichEditSubclassProc, 1, (DWORD_PTR)pDoc);

    if (g_hFont != NULL)
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    EditorApplyTextColorTo(hEdit);
    EditorApplyTabWidthTo(hEdit);
    EditorApplyGlobalZoomTo(hEdit);

    return hEdit;
}

Document* DocActive(void)
{
    return g_pActiveDoc;
}

Document* DocFromEdit(HWND hEdit)
{
    size_t i;

    if (hEdit == NULL)
        return NULL;

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (g_documents[i]->hEdit == hEdit)
            return g_documents[i];
    }

    return NULL;
}

int DocIndexOf(Document* pDoc)
{
    size_t i;

    if (pDoc == NULL)
        return -1;

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (g_documents[i] == pDoc)
            return (int)i;
    }

    return -1;
}

// Menor numero de "Untitled" ainda nao usado por outro documento sem arquivo.
static int DocAllocUntitledNumber(void)
{
    int number;
    size_t i;

    for (number = 1; ; ++number)
    {
        BOOL used = FALSE;

        for (i = 0; i < g_documents.size(); ++i)
        {
            if (!g_documents[i]->bHasPath && g_documents[i]->untitledNum == number)
            {
                used = TRUE;
                break;
            }
        }

        if (!used)
            return number;
    }
}

// Nome exibido do documento: titulo do arquivo, ou "Untitled", "Untitled 2", ...
void DocGetDisplayName(Document* pDoc, LPWSTR lpBuffer, int cchBuffer)
{
    if (lpBuffer == NULL || cchBuffer <= 0)
        return;

    if (pDoc != NULL && pDoc->bHasPath && pDoc->szFileTitle[0] != L'\0')
    {
        StringCchCopyW(lpBuffer, cchBuffer, pDoc->szFileTitle);
        return;
    }

    LoadStringResource(IDS_UNTITLED, lpBuffer, cchBuffer);

    if (pDoc != NULL && pDoc->untitledNum > 1)
    {
        size_t len = lstrlenW(lpBuffer);
        StringCchPrintfW(lpBuffer + len, cchBuffer - len, L" %d", pDoc->untitledNum);
    }
}

void DocUpdateTabTitle(Document* pDoc)
{
    WCHAR szName[MAX_PATH + 24];
    WCHAR szTitle[MAX_PATH];
    TCITEMW item = {};

    if (pDoc == NULL || g_hWndTabs == NULL)
        return;

    int index = DocIndexOf(pDoc);
    if (index < 0)
        return;

    DocGetDisplayName(pDoc, szTitle, ARRAYSIZE(szTitle));

    // O espaco extra a direita do texto reserva a area do botao de fechar
    // desenhado pelo subclass da barra de abas (window.cpp).
    StringCchPrintfW(szName, ARRAYSIZE(szName), L"%s%s%s",
                     SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0) ? L"* " : L"",
                     szTitle, TabGetClosePadding());

    // Se o texto nao mudou, nao reescrever o item: SetItem a cada tecla
    // repinta a aba e contribui para o flicker.
    WCHAR szOld[MAX_PATH + 24] = L"";
    TCITEMW oldItem = {};
    oldItem.mask = TCIF_TEXT;
    oldItem.pszText = szOld;
    oldItem.cchTextMax = ARRAYSIZE(szOld);
    if (TabCtrl_GetItem(g_hWndTabs, index, &oldItem) &&
        lstrcmpW(szOld, szName) == 0)
    {
        return;
    }

    item.mask = TCIF_TEXT;
    item.pszText = szName;
    TabCtrl_SetItem(g_hWndTabs, index, &item);
}

void DocActivate(Document* pDoc)
{
    size_t i;
    int index;

    if (pDoc == NULL)
        return;

    index = DocIndexOf(pDoc);
    if (index < 0)
        return;

    g_pActiveDoc = pDoc;
    g_iActiveDoc = index;
    g_hWndEdit = pDoc->hEdit;

    if (g_hWndTabs != NULL)
    {
        g_bUpdatingTabs = TRUE;
        TabCtrl_SetCurSel(g_hWndTabs, index);
        g_bUpdatingTabs = FALSE;
    }

    for (i = 0; i < g_documents.size(); ++i)
    {
        ShowWindow(g_documents[i]->hEdit, g_documents[i] == pDoc ? SW_SHOW : SW_HIDE);
    }

    // Rede de seguranca: a aba que aparece segue o zoom global.
    EditorApplyGlobalZoomTo(pDoc->hEdit);

    LayoutMainWindow();
    EditorApplyTabWidthTo(pDoc->hEdit);
    InvalidateLineNumbers();
    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    SetFocus(pDoc->hEdit);

    // Reanalisa o realce da aba que recebeu o foco: se o highlighting foi
    // ligado/desligado enquanto ela estava oculta, ela se ajusta agora; senao
    // apenas retoma a varredura lazy. Vem antes da linha atual, que depende
    // dos flags reiniciados aqui.
    EditorSyncUniversalHighlight(pDoc);
    EditorUpdateCurrentLineHighlight(pDoc->hEdit);
}

Document* DocCreate(void)
{
    Document* pDoc = new Document;
    TCITEMW item = {};
    WCHAR szName[MAX_PATH];

    if (pDoc == NULL)
        return NULL;

    ZeroMemory(pDoc, sizeof(*pDoc));
    pDoc->encoding = (TextEncoding)g_settings.defaultEncoding;
    pDoc->untitledNum = DocAllocUntitledNumber();
    pDoc->hEdit = CreateRichEdit(pDoc);
    if (pDoc->hEdit == NULL)
    {
        delete pDoc;
        return NULL;
    }

    g_documents.push_back(pDoc);

    if (g_hWndTabs != NULL)
    {
        LoadStringResource(IDS_UNTITLED, szName, ARRAYSIZE(szName));
        item.mask = TCIF_TEXT;
        item.pszText = szName;

        g_bUpdatingTabs = TRUE;
        int inserted = TabCtrl_InsertItem(g_hWndTabs, (int)g_documents.size() - 1, &item);
        g_bUpdatingTabs = FALSE;

        if (inserted < 0)
        {
            g_documents.pop_back();
            DestroyWindow(pDoc->hEdit);
            delete pDoc;
            return NULL;
        }
    }

    DocUpdateTabTitle(pDoc);
    DocActivate(pDoc);

    return pDoc;
}

void DocDestroy(Document* pDoc)
{
    BOOL wasActive;
    int index;
    size_t next;

    if (pDoc == NULL)
        return;

    index = DocIndexOf(pDoc);
    if (index < 0)
        return;

    wasActive = (pDoc == g_pActiveDoc);

    if (g_hWndTabs != NULL)
    {
        g_bUpdatingTabs = TRUE;
        TabCtrl_DeleteItem(g_hWndTabs, index);
        g_bUpdatingTabs = FALSE;
    }

    g_documents.erase(g_documents.begin() + index);
    UpdateTreeOpenMarks();

    if (wasActive)
    {
        g_pActiveDoc = NULL;
        g_iActiveDoc = -1;
        g_hWndEdit = NULL;
    }

    EditorCancelUniversalHighlightFor(pDoc);
    EditorClearCurrentLineHighlight(pDoc);
    DestroyWindow(pDoc->hEdit);
    delete pDoc;

    if (wasActive)
    {
        if (!g_documents.empty())
        {
            next = (size_t)index;
            if (next >= g_documents.size())
                next = g_documents.size() - 1;
            DocActivate(g_documents[next]);
        }
        else
        {
            UpdateWindowTitle();
            UpdateStatusBar();
        }
    }
    else
    {
        // Fechar uma aba anterior a ativa desloca o indice do documento ativo.
        // Sem isso, Ctrl+Tab e o item selecionado passam a apontar para outro
        // documento.
        g_iActiveDoc = DocIndexOf(g_pActiveDoc);
        if (g_hWndTabs != NULL && g_iActiveDoc >= 0)
        {
            g_bUpdatingTabs = TRUE;
            TabCtrl_SetCurSel(g_hWndTabs, g_iActiveDoc);
            g_bUpdatingTabs = FALSE;
        }
    }

    InvalidateLineNumbers();
}

void DocDestroyAll(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
    {
        EditorClearCurrentLineHighlight(g_documents[i]);
        if (IsWindow(g_documents[i]->hEdit))
            DestroyWindow(g_documents[i]->hEdit);
        delete g_documents[i];
    }

    g_documents.clear();
    g_pActiveDoc = NULL;
    g_iActiveDoc = -1;
    g_hWndEdit = NULL;
}

// Buffers de normalizacao em pilha: MAX_FILE_PATH (32768) custaria 64 KB cada.
// Caminhos maiores que isso caem na comparacao direta em DocFindByPath.
#define DOC_NORM_PATH_MAX 4096

// Resolve um caminho para a forma absoluta e longa (sem 8.3, sem ".."), para
// comparar caminhos que chegam por origens diferentes (arrastar, Explorer,
// dialogo). Retorna FALSE se nao couber no buffer.
static BOOL DocNormalizePath(LPCWSTR lpPath, LPWSTR lpOut, DWORD cchOut)
{
    WCHAR szFull[DOC_NORM_PATH_MAX];
    DWORD n = GetFullPathNameW(lpPath, ARRAYSIZE(szFull), szFull, NULL);

    if (n == 0 || n >= ARRAYSIZE(szFull))
        return FALSE;

    n = GetLongPathNameW(szFull, lpOut, cchOut);
    if (n == 0 || n >= cchOut)
        return SUCCEEDED(StringCchCopyW(lpOut, cchOut, szFull));

    return TRUE;
}

// Retorna o documento aberto que corresponde ao arquivo, ou NULL. A
// comparacao e case-insensitive, como no Windows.
Document* DocFindByPath(LPCWSTR lpFileName)
{
    WCHAR szWanted[DOC_NORM_PATH_MAX];
    WCHAR szOther[DOC_NORM_PATH_MAX];
    BOOL bHaveWanted;
    size_t i;

    if (lpFileName == NULL || lpFileName[0] == L'\0')
        return NULL;

    bHaveWanted = DocNormalizePath(lpFileName, szWanted, ARRAYSIZE(szWanted));

    for (i = 0; i < g_documents.size(); ++i)
    {
        Document* pDoc = g_documents[i];

        if (!pDoc->bHasPath)
            continue;

        if (lstrcmpiW(lpFileName, pDoc->szFilePath) == 0)
            return pDoc;

        if (bHaveWanted &&
            DocNormalizePath(pDoc->szFilePath, szOther, ARRAYSIZE(szOther)) &&
            lstrcmpiW(szWanted, szOther) == 0)
        {
            return pDoc;
        }
    }

    return NULL;
}

BOOL DocLoad(Document* pDoc, LPCWSTR lpFileName)
{
    LPWSTR lpText = NULL;
    TextEncoding encoding = ENC_ANSI;
    DWORD attrs;

    if (pDoc == NULL || lpFileName == NULL || lpFileName[0] == L'\0')
        return FALSE;

    attrs = GetFileAttributesW(lpFileName);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        WCHAR szMessage[512];
        LoadStringResource(IDS_OPEN_FAILED, szMessage, ARRAYSIZE(szMessage));
        MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME, MB_ICONERROR | MB_OK);
        return FALSE;
    }

    if (!LoadTextFile(lpFileName, &lpText, &encoding))
    {
        WCHAR szMessage[512];
        LoadStringResource(IDS_OPEN_FAILED, szMessage, ARRAYSIZE(szMessage));
        MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME, MB_ICONERROR | MB_OK);
        return FALSE;
    }

    EditorClearCurrentLineHighlight(pDoc);

    SetWindowTextW(pDoc->hEdit, lpText != NULL ? lpText : L"");
    if (lpText != NULL)
        LocalFree(lpText);

    // Trocar o texto pode redefinir a largura do tab do RichEdit.
    EditorApplyTabWidthTo(pDoc->hEdit);

    StringCchCopyW(pDoc->szFilePath, ARRAYSIZE(pDoc->szFilePath), lpFileName);
    StringCchCopyW(pDoc->szFileTitle, ARRAYSIZE(pDoc->szFileTitle),
                   PathFindFileNameW(lpFileName));

    pDoc->bHasPath = TRUE;
    pDoc->encoding = encoding;
    pDoc->bReinterpreted = FALSE;
    pDoc->bReadOnly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;

    SendMessageW(pDoc->hEdit, EM_SETREADONLY, pDoc->bReadOnly, 0);
    SendMessageW(pDoc->hEdit, EM_SETMODIFY, FALSE, 0);
    SendMessageW(pDoc->hEdit, EM_EMPTYUNDOBUFFER, 0, 0);

    if (pDoc == g_pActiveDoc)
        EditorUpdateCurrentLineHighlight(pDoc->hEdit);

    DocUpdateTabTitle(pDoc);
    UpdateTreeOpenMarks();

    if (pDoc == g_pActiveDoc)
    {
        InvalidateLineNumbers();
        UpdateWindowTitle();
        UpdateStatusBar();
        UpdateEditMenuState(g_hWndMain);

        // Reinicia a varredura lazy para o texto recem-carregado.
        EditorRestartUniversalHighlight(pDoc, TRUE);
    }
    else
    {
        // Aba oculta: nao mexe no editor agora; ao ativar, DocActivate reanalisa.
        pDoc->bCodeHighlightSynced = FALSE;
    }

    return TRUE;
}

BOOL DocLoadCurrent(LPCWSTR lpFileName)
{
    return DocLoad(g_pActiveDoc, lpFileName);
}

BOOL DocSaveFile(Document* pDoc, LPCWSTR lpFileName)
{
    int length;
    LPWSTR lpText;
    BOOL result;

    if (pDoc == NULL || lpFileName == NULL)
        return FALSE;

    length = GetWindowTextLengthW(pDoc->hEdit);
    lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpText == NULL)
        return FALSE;

    if (length > 0)
        GetWindowTextW(pDoc->hEdit, lpText, length + 1);

    result = SaveTextFile(lpFileName, lpText, pDoc->encoding);
    if (result)
        pDoc->bReinterpreted = FALSE;

    LocalFree(lpText);
    return result;
}

static BOOL SaveWithDialog(Document* pDoc)
{
    WCHAR szFile[MAX_FILE_PATH];
    WCHAR szFilter[512];
    WCHAR szText[128];
    WCHAR szAll[128];

    if (pDoc->bHasPath)
        StringCchCopyW(szFile, ARRAYSIZE(szFile), pDoc->szFilePath);
    else
        szFile[0] = L'\0';

    LoadStringResource(IDS_FILTER_TEXT, szText, ARRAYSIZE(szText));
    LoadStringResource(IDS_FILTER_ALL, szAll, ARRAYSIZE(szAll));

    StringCchPrintfW(szFilter, ARRAYSIZE(szFilter),
                     L"%s%c*.txt%c%s%c*.*%c",
                     szText, L'\0', L'\0', szAll, L'\0', L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWndMain;
    ofn.lpstrFilter = szFilter;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = ARRAYSIZE(szFile);
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn))
        return FALSE;

    if (DocSaveFile(pDoc, szFile))
    {
        StringCchCopyW(pDoc->szFilePath, ARRAYSIZE(pDoc->szFilePath), szFile);
        StringCchCopyW(pDoc->szFileTitle, ARRAYSIZE(pDoc->szFileTitle),
                       PathFindFileNameW(szFile));
        pDoc->bHasPath = TRUE;
        pDoc->bReadOnly = FALSE;
        SendMessageW(pDoc->hEdit, EM_SETREADONLY, FALSE, 0);
        SendMessageW(pDoc->hEdit, EM_SETMODIFY, FALSE, 0);
        DocUpdateTabTitle(pDoc);
        UpdateWindowTitle();
        UpdateStatusBar();
        UpdateEditMenuState(g_hWndMain);
        return TRUE;
    }

    WCHAR szMessage[512];
    LoadStringResource(IDS_SAVE_FAILED, szMessage, ARRAYSIZE(szMessage));
    MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME, MB_ICONERROR | MB_OK);
    return FALSE;
}

BOOL DocSave(Document* pDoc, BOOL bSaveAs)
{
    if (pDoc == NULL)
        return FALSE;

    if (pDoc->bReadOnly)
        bSaveAs = TRUE;

    if (!pDoc->bHasPath || bSaveAs)
        return SaveWithDialog(pDoc);

    if (!DocSaveFile(pDoc, pDoc->szFilePath))
    {
        WCHAR szMessage[512];
        LoadStringResource(IDS_SAVE_FAILED, szMessage, ARRAYSIZE(szMessage));
        MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME, MB_ICONERROR | MB_OK);
        return FALSE;
    }

    SendMessageW(pDoc->hEdit, EM_SETMODIFY, FALSE, 0);
    DocUpdateTabTitle(pDoc);
    UpdateTreeOpenMarks();
    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    return TRUE;
}

BOOL DocSaveAll(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
    {
        Document* pDoc = g_documents[i];

        if (!SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0))
            continue;

        if (!DocSave(pDoc, FALSE))
            return FALSE;
    }

    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    return TRUE;
}

BOOL DocQuerySaveChanges(Document* pDoc)
{
    WCHAR szPrompt[512];
    WCHAR szName[MAX_PATH];
    WCHAR szMessage[768];
    int result;

    if (pDoc == NULL)
        return TRUE;

    if (!SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0))
        return TRUE;

    DocGetDisplayName(pDoc, szName, ARRAYSIZE(szName));

    LoadStringResource(IDS_SAVE_PROMPT, szPrompt, ARRAYSIZE(szPrompt));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szPrompt, szName);

    result = MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME,
                         MB_YESNOCANCEL | MB_ICONWARNING);

    if (result == IDYES)
        return DocSave(pDoc, FALSE);
    if (result == IDNO)
        return TRUE;

    return FALSE;
}

BOOL DocQuerySaveAllChanges(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (!DocQuerySaveChanges(g_documents[i]))
            return FALSE;
    }

    return TRUE;
}

BOOL DocClose(Document* pDoc)
{
    BOOL wasLast;

    if (pDoc == NULL)
        return FALSE;

    if (!DocQuerySaveChanges(pDoc))
        return FALSE;

    wasLast = (g_documents.size() == 1 && g_documents[0] == pDoc);

    DocDestroy(pDoc);

    if (wasLast)
        PostMessageW(g_hWndMain, WM_CLOSE, 0, 0);

    return TRUE;
}

// Move o documento 'from' para a posicao 'to' (reordenar abas).
// As abas so guardam texto, entao basta reordenar a lista e reescrever os
// titulos do intervalo afetado; nenhum item e removido/inserido (sem piscar).
void DocMove(int from, int to)
{
    int count = (int)g_documents.size();
    int lo, hi, i;
    Document* pDoc;

    if (from < 0 || to < 0 || from >= count || to >= count || from == to)
        return;

    pDoc = g_documents[from];
    g_documents.erase(g_documents.begin() + from);
    g_documents.insert(g_documents.begin() + to, pDoc);

    lo = from < to ? from : to;
    hi = from < to ? to : from;
    for (i = lo; i <= hi; ++i)
        DocUpdateTabTitle(g_documents[i]);

    g_iActiveDoc = DocIndexOf(g_pActiveDoc);

    if (g_hWndTabs != NULL && g_iActiveDoc >= 0)
    {
        g_bUpdatingTabs = TRUE;
        TabCtrl_SetCurSel(g_hWndTabs, g_iActiveDoc);
        g_bUpdatingTabs = FALSE;
    }
}

void DocCycle(int direction)
{
    int count = (int)g_documents.size();
    int index;

    if (count < 2)
        return;

    index = g_iActiveDoc + direction;
    if (index < 0)
        index = count - 1;
    else if (index >= count)
        index = 0;

    DocActivate(g_documents[index]);
}
