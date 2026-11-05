#include "notbad.h"

// Chave propria do Notbad. NUNCA usar Software\Microsoft\Notepad: o Notepad
// compartilha fonte, word wrap e posicao da janela nessa chave e os dois
// aplicativos corrompiam as configuracoes um do outro.
#define REG_PATH        L"Software\\Notbad"

static void ReadDword(HKEY hKey, LPCWSTR name, DWORD *value, DWORD defaultValue)
{
    DWORD size = sizeof(DWORD);
    DWORD type = 0;

    if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
    {
        *value = defaultValue;
    }
}

static void ReadString(HKEY hKey, LPCWSTR name, LPWSTR buffer, DWORD cchBuffer,
                       LPCWSTR defaultValue)
{
    DWORD size = cchBuffer * sizeof(WCHAR);
    DWORD type = 0;

    if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)buffer, &size) != ERROR_SUCCESS ||
        type != REG_SZ)
    {
        StringCchCopyW(buffer, cchBuffer, defaultValue);
    }
    else
    {
        buffer[cchBuffer - 1] = L'\0';
    }
}

static void WriteDword(HKEY hKey, LPCWSTR name, DWORD value)
{
    RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static void WriteString(HKEY hKey, LPCWSTR name, LPCWSTR value)
{
    RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE *)value,
                   (DWORD)((lstrlenW(value) + 1) * sizeof(WCHAR)));
}

void SettingsLoadDefaults(void)
{
    ZeroMemory(&g_settings, sizeof(g_settings));

    g_settings.logFont.lfHeight         = -13;
    g_settings.logFont.lfWeight         = FW_NORMAL;
    g_settings.logFont.lfCharSet        = DEFAULT_CHARSET;
    g_settings.logFont.lfOutPrecision   = OUT_DEFAULT_PRECIS;
    g_settings.logFont.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    g_settings.logFont.lfQuality        = DEFAULT_QUALITY;
    g_settings.logFont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    StringCchCopyW(g_settings.logFont.lfFaceName, LF_FACESIZE, L"Lucida Console");

    g_settings.wordWrap             = FALSE;
    g_settings.statusBar            = TRUE;
    g_settings.saveWindowPositions  = TRUE;
    g_settings.windowsOnlyEol       = FALSE;
    g_settings.pasteOriginalEol     = FALSE;
    g_settings.showLineNumbers      = FALSE;
    g_settings.highlightCurrentLine = FALSE;
    g_settings.codeHighlighting     = FALSE;
    g_settings.autoIndent           = FALSE;
    g_settings.indentWithTabs       = FALSE;
    g_settings.tabWidth             = 4;
    g_settings.treeWidth            = 220;
    g_settings.defaultEncoding      = ENC_UTF8;
    g_settings.hasWindowRect        = FALSE;
}

// Rejeita posicoes salvas fora de qualquer monitor (ex.: configuracao antiga
// de outra maquina), evitando abrir a janela invisivel.
static BOOL IsWindowRectUsable(const RECT* prc)
{
    HMONITOR hMon;
    MONITORINFO mi = {};

    if (prc->right <= prc->left || prc->bottom <= prc->top)
        return FALSE;

    hMon = MonitorFromRect(prc, MONITOR_DEFAULTTONULL);
    if (hMon == NULL)
        return FALSE;

    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi))
        return FALSE;

    RECT rcInter;
    return IntersectRect(&rcInter, prc, &mi.rcWork) != FALSE;
}

void SettingsLoad(void)
{
    HKEY hKey = NULL;
    DWORD value = 0;

    SettingsLoadDefaults();

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    ReadDword(hKey, L"fWrap", &value, g_settings.wordWrap);
    g_settings.wordWrap = value != 0;

    ReadDword(hKey, L"StatusBar", &value, g_settings.statusBar);
    g_settings.statusBar = value != 0;

    ReadDword(hKey, L"fSaveWindowPositions", &value, 1);
    g_settings.saveWindowPositions = value != 0;

    ReadDword(hKey, L"fWindowsOnlyEOL", &value, 0);
    g_settings.windowsOnlyEol = value != 0;

    ReadDword(hKey, L"fPasteOriginalEOL", &value, 0);
    g_settings.pasteOriginalEol = value != 0;

    ReadDword(hKey, L"ShowLineNumbers", &value, 0);
    g_settings.showLineNumbers = value != 0;

    ReadDword(hKey, L"HighlightCurrentLine", &value, 0);
    g_settings.highlightCurrentLine = value != 0;

    ReadDword(hKey, L"CodeHighlighting", &value, 0);
    g_settings.codeHighlighting = value != 0;

    ReadDword(hKey, L"AutoIndent", &value, 0);
    g_settings.autoIndent = value != 0;

    ReadDword(hKey, L"IndentWithTabs", &value, 0);
    g_settings.indentWithTabs = value != 0;

    ReadDword(hKey, L"TabWidth", &value, 4);
    g_settings.tabWidth = (int)value;
    if (g_settings.tabWidth < 1 || g_settings.tabWidth > 32)
        g_settings.tabWidth = 4;

    ReadDword(hKey, L"TreeWidth", &value, 220);
    g_settings.treeWidth = (int)value;
    if (g_settings.treeWidth < 100 || g_settings.treeWidth > 2000)
        g_settings.treeWidth = 220;

    // O valor antigo "iDefaultEncoding" era regravado como ANSI a cada
    // execucao; o padrao passou a ser UTF-8 na 1.0.2, entao a chave ganhou
    // outro nome para que aquele valor antigo nao sobreponha o novo padrao.
    ReadDword(hKey, L"DefaultEncoding", &value, ENC_UTF8);
    g_settings.defaultEncoding = (int)value;
    if (g_settings.defaultEncoding < ENC_ANSI ||
        g_settings.defaultEncoding > ENC_GB18030)
    {
        g_settings.defaultEncoding = ENC_UTF8;
    }

    ReadDword(hKey, L"iWindowPosX", &value, 0);
    g_settings.windowRect.left = (LONG)value;
    ReadDword(hKey, L"iWindowPosY", &value, 0);
    g_settings.windowRect.top = (LONG)value;
    ReadDword(hKey, L"iWindowPosDX", &value, 0);
    g_settings.windowRect.right = g_settings.windowRect.left + (LONG)value;
    ReadDword(hKey, L"iWindowPosDY", &value, 0);
    g_settings.windowRect.bottom = g_settings.windowRect.top + (LONG)value;

    g_settings.hasWindowRect = g_settings.saveWindowPositions &&
                               IsWindowRectUsable(&g_settings.windowRect);

    RegCloseKey(hKey);
}

void SettingsLoadFont(void)
{
    HKEY hKey = NULL;
    DWORD pointSize = 10;
    DWORD weight = FW_NORMAL;
    DWORD italic = 0;
    DWORD underline = 0;
    DWORD strikeOut = 0;
    DWORD charSet = DEFAULT_CHARSET;
    WCHAR faceName[LF_FACESIZE];
    HDC hdc;

    StringCchCopyW(faceName, ARRAYSIZE(faceName), g_settings.logFont.lfFaceName);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD value = 0;

        ReadDword(hKey, L"iPointSize", &value, pointSize);
        pointSize = value;
        ReadDword(hKey, L"lfWeight", &value, weight);
        weight = value;
        ReadDword(hKey, L"lfItalic", &value, italic);
        italic = value;
        ReadDword(hKey, L"lfUnderline", &value, underline);
        underline = value;
        ReadDword(hKey, L"lfStrikeOut", &value, strikeOut);
        strikeOut = value;
        ReadDword(hKey, L"lfCharSet", &value, charSet);
        charSet = value;
        ReadString(hKey, L"lfFaceName", faceName, ARRAYSIZE(faceName),
                   g_settings.logFont.lfFaceName);

        RegCloseKey(hKey);
    }

    // Valores corrompidos (o bug antigo gravava tamanho 1 na chave do
    // Notepad) nao podem gerar fonte ilegivel.
    if (pointSize < 4 || pointSize > 96)
        pointSize = 10;
    if (weight < 100 || weight > 900)
        weight = FW_NORMAL;
    if (faceName[0] == L'\0')
        StringCchCopyW(faceName, ARRAYSIZE(faceName), L"Lucida Console");

    hdc = GetDC(NULL);
    if (hdc != NULL)
        g_settings.logFont.lfHeight = -MulDiv((int)pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    else
        g_settings.logFont.lfHeight = -(LONG)pointSize;

    ReleaseDC(NULL, hdc);

    g_settings.logFont.lfWeight         = (LONG)weight;
    g_settings.logFont.lfItalic         = (BYTE)(italic != 0);
    g_settings.logFont.lfUnderline      = (BYTE)(underline != 0);
    g_settings.logFont.lfStrikeOut      = (BYTE)(strikeOut != 0);
    g_settings.logFont.lfCharSet        = (BYTE)charSet;
    g_settings.logFont.lfOutPrecision   = OUT_DEFAULT_PRECIS;
    g_settings.logFont.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    g_settings.logFont.lfQuality        = DEFAULT_QUALITY;
    g_settings.logFont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    StringCchCopyW(g_settings.logFont.lfFaceName, LF_FACESIZE, faceName);

    g_hFont = CreateFontIndirectW(&g_settings.logFont);
    if (g_hFont == NULL)
        g_hFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);

    g_hFontStatus = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    SettingsRefreshMenuFont();
}

// Cria (ou recria) a fonte usada nos itens de menu do sistema.
// Retorna TRUE se g_hFontMenu mudou e os controles precisam receber WM_SETFONT.
BOOL SettingsRefreshMenuFont(void)
{
    NONCLIENTMETRICSW ncm = {};
    HFONT hNew = NULL;
    HFONT hOld = g_hFontMenu;

    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        hNew = CreateFontIndirectW(&ncm.lfMenuFont);

    if (hNew == NULL)
        hNew = g_hFontStatus;   // fonte stock; DeleteObject nela e inofensivo

    g_hFontMenu = hNew;
    return hNew != hOld;
}

void SettingsSaveFont(void)
{
    HKEY hKey = NULL;
    DWORD pointSize;
    HDC hdc;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE,
                        NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }

    hdc = GetDC(NULL);
    if (hdc != NULL)
    {
        pointSize = (DWORD)MulDiv(-g_settings.logFont.lfHeight,
                                 72, GetDeviceCaps(hdc, LOGPIXELSY));
    }
    else
    {
        pointSize = (DWORD)(-g_settings.logFont.lfHeight);
    }
    ReleaseDC(NULL, hdc);

    WriteDword(hKey, L"iPointSize", pointSize);
    WriteDword(hKey, L"lfWeight", (DWORD)g_settings.logFont.lfWeight);
    WriteDword(hKey, L"lfItalic", g_settings.logFont.lfItalic ? 1 : 0);
    WriteDword(hKey, L"lfUnderline", g_settings.logFont.lfUnderline ? 1 : 0);
    WriteDword(hKey, L"lfStrikeOut", g_settings.logFont.lfStrikeOut ? 1 : 0);
    WriteDword(hKey, L"lfCharSet", g_settings.logFont.lfCharSet);
    WriteString(hKey, L"lfFaceName", g_settings.logFont.lfFaceName);

    RegCloseKey(hKey);
}

void SettingsSave(void)
{
    HKEY hKey = NULL;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE,
                        NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }

    WriteDword(hKey, L"fWrap", g_settings.wordWrap ? 1 : 0);
    WriteDword(hKey, L"StatusBar", g_settings.statusBar ? 1 : 0);
    WriteDword(hKey, L"fSaveWindowPositions", g_settings.saveWindowPositions ? 1 : 0);
    WriteDword(hKey, L"fWindowsOnlyEOL", g_settings.windowsOnlyEol ? 1 : 0);
    WriteDword(hKey, L"fPasteOriginalEOL", g_settings.pasteOriginalEol ? 1 : 0);
    WriteDword(hKey, L"ShowLineNumbers", g_settings.showLineNumbers ? 1 : 0);
    WriteDword(hKey, L"HighlightCurrentLine", g_settings.highlightCurrentLine ? 1 : 0);
    WriteDword(hKey, L"CodeHighlighting", g_settings.codeHighlighting ? 1 : 0);
    WriteDword(hKey, L"AutoIndent", g_settings.autoIndent ? 1 : 0);
    WriteDword(hKey, L"IndentWithTabs", g_settings.indentWithTabs ? 1 : 0);
    WriteDword(hKey, L"TabWidth", (DWORD)g_settings.tabWidth);
    WriteDword(hKey, L"TreeWidth", (DWORD)g_settings.treeWidth);
    WriteDword(hKey, L"DefaultEncoding", (DWORD)g_settings.defaultEncoding);

    RegCloseKey(hKey);
}

void SettingsLoadWindowPlacement(HWND hWnd)
{
    if (hWnd != NULL && g_settings.hasWindowRect)
    {
        SetWindowPos(hWnd, NULL,
                     g_settings.windowRect.left,
                     g_settings.windowRect.top,
                     g_settings.windowRect.right - g_settings.windowRect.left,
                     g_settings.windowRect.bottom - g_settings.windowRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SettingsSaveWindowPlacement(HWND hWnd)
{
    RECT rc;
    HKEY hKey = NULL;

    if (hWnd == NULL || !g_settings.saveWindowPositions)
        return;

    if (IsIconic(hWnd) || IsZoomed(hWnd))
        return;

    GetWindowRect(hWnd, &rc);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE,
                        NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }

    WriteDword(hKey, L"iWindowPosX", (DWORD)rc.left);
    WriteDword(hKey, L"iWindowPosY", (DWORD)rc.top);
    WriteDword(hKey, L"iWindowPosDX", (DWORD)(rc.right - rc.left));
    WriteDword(hKey, L"iWindowPosDY", (DWORD)(rc.bottom - rc.top));

    RegCloseKey(hKey);
}

// ---------------------------------------------------------------------------
// Estado da TreeView (pasta raiz, visibilidade, pastas expandidas e item
// selecionado). Gravado ao fechar o programa e relido na inicializacao.
// ---------------------------------------------------------------------------
#define TREE_VALUE_MAX_BYTES  (1u << 20)

void SettingsSaveTree(LPCWSTR lpRoot, BOOL bVisible,
                      const std::vector<std::wstring>& expanded, LPCWSTR lpSelected)
{
    HKEY hKey = NULL;
    std::vector<WCHAR> multi;
    size_t i;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE,
                        NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }

    WriteString(hKey, L"TreeRoot", lpRoot != NULL ? lpRoot : L"");
    WriteDword(hKey, L"TreeVisible", bVisible ? 1 : 0);
    WriteString(hKey, L"TreeSelected", lpSelected != NULL ? lpSelected : L"");

    // REG_MULTI_SZ: cada caminho termina em NUL e a lista termina em NUL duplo.
    for (i = 0; i < expanded.size(); ++i)
    {
        if (expanded[i].empty())
            continue;
        multi.insert(multi.end(), expanded[i].begin(), expanded[i].end());
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    if (multi.size() < 2)
        multi.push_back(L'\0');

    RegSetValueExW(hKey, L"TreeExpanded", 0, REG_MULTI_SZ,
                   (const BYTE *)&multi[0], (DWORD)(multi.size() * sizeof(WCHAR)));

    RegCloseKey(hKey);
}

// Le o valor inteiro para o buffer, sempre com dois NULs no fim.
static BOOL ReadRegData(HKEY hKey, LPCWSTR name, DWORD expectedType, std::vector<WCHAR>& buffer)
{
    DWORD type = 0;
    DWORD size = 0;

    buffer.clear();

    if (RegQueryValueExW(hKey, name, NULL, &type, NULL, &size) != ERROR_SUCCESS ||
        type != expectedType || size == 0 || size > TREE_VALUE_MAX_BYTES)
    {
        return FALSE;
    }

    buffer.assign(size / sizeof(WCHAR) + 2, L'\0');
    size = (DWORD)((buffer.size() - 2) * sizeof(WCHAR));

    if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)&buffer[0], &size) != ERROR_SUCCESS)
    {
        buffer.clear();
        return FALSE;
    }

    return TRUE;
}

BOOL SettingsLoadTree(std::wstring& root, BOOL* pbVisible,
                      std::vector<std::wstring>& expanded, std::wstring& selected)
{
    HKEY hKey = NULL;
    std::vector<WCHAR> buffer;
    DWORD value = 0;

    root.clear();
    selected.clear();
    expanded.clear();
    *pbVisible = TRUE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    if (ReadRegData(hKey, L"TreeRoot", REG_SZ, buffer))
        root = &buffer[0];

    if (root.empty())
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    ReadDword(hKey, L"TreeVisible", &value, 1);
    *pbVisible = value != 0;

    if (ReadRegData(hKey, L"TreeSelected", REG_SZ, buffer))
        selected = &buffer[0];

    if (ReadRegData(hKey, L"TreeExpanded", REG_MULTI_SZ, buffer))
    {
        const WCHAR* p = &buffer[0];

        while (*p != L'\0')
        {
            expanded.push_back(p);
            p += lstrlenW(p) + 1;
        }
    }

    RegCloseKey(hKey);
    return TRUE;
}
