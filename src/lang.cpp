// Sistema de idiomas do Notbad.
//
// O ingles do notbad.rc e a base: menus, dialogos e stringtable continuam
// funcionando sozinhos. Um arquivo de idioma (JSON plano, veja lang/en.json)
// so SUBSTITUI textos; o que faltar nele continua em ingles.
//
// Onde a traducao e aplicada:
//   - stringtable (IDS_*): LoadStringResource() consulta LangGetStringById();
//   - menu principal:      LangApplyMenu(), uma vez, ao criar a janela;
//   - dialogos:            LangApplyDialog(), no inicio de cada WM_INITDIALOG.
//
// Escolha do idioma: usa o idioma de exibicao do Windows (ex.: "pt-BR") e
// monta a cadeia  pt-BR -> pt -> en  (langcore::BuildLangChain). Para cada item
// da cadeia, em ordem, procura:
//   1. <pasta do exe>\lang\<tag>.json   (permite ajustar/adicionar idiomas)
//   2. recurso embutido NBLANG_<TAG>    (RCDATA no notbad.rc)
// O primeiro arquivo valido encontrado e o idioma carregado. Ou seja, o idioma
// mais especifico sempre vence, e dentro do mesmo idioma a pasta vence o
// embutido. Arquivo ausente ou com JSON invalido e simplesmente pulado.

#include "notbad.h"
#include "langcore.h"

#include <wchar.h>

#define NOTBAD_LANG_MAX_BYTES   (1024 * 1024)

typedef std::unordered_map<std::wstring, std::wstring> LangMap;

// Textos do idioma carregado, por chave ("IDM_NEW", "IDS_SAVE_PROMPT"...).
static LangMap s_strings;

// As strings da stringtable ja validadas, por ID: LoadStringResource e chamado
// a cada movimento do cursor (barra de status), entao a consulta e direta.
static std::unordered_map<UINT, std::wstring> s_stringsById;

static WCHAR s_szLangTag[64] = L"";    // idioma carregado; vazio = so o RC (ingles)

// Chave de cada string da stringtable: o proprio nome do ID.
struct LangIdEntry
{
    UINT    id;
    LPCWSTR key;
};

#define NB_WIDEN2(s)    L##s
#define NB_WIDEN(s)     NB_WIDEN2(s)
#define NB_IDS(name)    { name, NB_WIDEN(#name) }

static const LangIdEntry s_idsTable[] =
{
    NB_IDS(IDS_UNTITLED),
    NB_IDS(IDS_FILTER_TEXT),
    NB_IDS(IDS_FILTER_ALL),
    NB_IDS(IDS_SAVE_PROMPT),
    NB_IDS(IDS_OPEN_FAILED),
    NB_IDS(IDS_SAVE_FAILED),
    NB_IDS(IDS_READONLY),
    NB_IDS(IDS_ABOUT_TITLE),
    NB_IDS(IDS_STATUS_LINE),
    NB_IDS(IDS_STATUS_COL),
    NB_IDS(IDS_ENC_ASK),
    NB_IDS(IDS_ENC_LOSS),
    NB_IDS(IDS_REOPEN_DIRTY),
    NB_IDS(IDS_ABOUT_VERSION),
    NB_IDS(IDS_HELP_TEXT),
    NB_IDS(IDS_INDENT_NO_TABS),
    NB_IDS(IDS_INDENT_NO_SPACES),
    NB_IDS(IDS_FIND_TITLE),
    NB_IDS(IDS_REPLACE_TITLE),
    NB_IDS(IDS_FIND_NOT_FOUND),
    NB_IDS(IDS_REPLACED_COUNT),
    NB_IDS(IDS_STATUS_AUTOINDENT),
    NB_IDS(IDS_ENC_UTF8BOM)
};

// Itens de menu: o comando (ID) e a chave do texto, que inclui o "\tCtrl+X".
static const LangIdEntry s_menuItems[] =
{
    NB_IDS(IDM_NEW),            NB_IDS(IDM_OPEN),           NB_IDS(IDM_OPENFOLDER),
    NB_IDS(IDM_SAVE),           NB_IDS(IDM_SAVEAS),         NB_IDS(IDM_SAVEALL),
    NB_IDS(IDM_CLOSETAB),       NB_IDS(IDM_PAGESETUP),      NB_IDS(IDM_PRINT),
    NB_IDS(IDM_EXIT),
    NB_IDS(IDM_UNDO),           NB_IDS(IDM_CUT),            NB_IDS(IDM_COPY),
    NB_IDS(IDM_PASTE),          NB_IDS(IDM_DELETE),         NB_IDS(IDM_FIND),
    NB_IDS(IDM_FINDNEXT),       NB_IDS(IDM_REPLACE),        NB_IDS(IDM_GOTO),
    NB_IDS(IDM_SELECTALL),      NB_IDS(IDM_TIMEDATE),
    NB_IDS(IDM_WORDWRAP),       NB_IDS(IDM_FONT),           NB_IDS(IDM_INDENT),
    NB_IDS(IDM_ENC_ANSI),       NB_IDS(IDM_ENC_UTF8),       NB_IDS(IDM_ENC_UTF8BOM),
    NB_IDS(IDM_ENC_UTF16LE),
    NB_IDS(IDM_NEXT_TAB),       NB_IDS(IDM_PREV_TAB),       NB_IDS(IDM_TREE),
    NB_IDS(IDM_LINENUMBERS),    NB_IDS(IDM_HIGHLIGHTLINE),  NB_IDS(IDM_HIGHLIGHTCODE),
    NB_IDS(IDM_STATUS),
    NB_IDS(IDM_HELP),           NB_IDS(IDM_ABOUT)
};

// Os menus (File, Edit...) nao tem ID: cada um e reconhecido pelo seu primeiro
// item de comando, o que independe da posicao e de separadores.
static const LangIdEntry s_menuPopups[] =
{
    { IDM_NEW,      L"IDM_POPUP_FILE" },
    { IDM_UNDO,     L"IDM_POPUP_EDIT" },
    { IDM_WORDWRAP, L"IDM_POPUP_FORMAT" },
    { IDM_ENC_ANSI, L"IDM_POPUP_ENCODING" },
    { IDM_NEXT_TAB, L"IDM_POPUP_VIEW" },
    { IDM_HELP,     L"IDM_POPUP_HELP" }
};

// Textos dos controles de cada dialogo, pelo ID do controle. IDOK e IDCANCEL
// sao compartilhados entre dialogos, por isso cada um tem chave propria.
struct LangControlEntry
{
    int     id;
    LPCWSTR key;
};

#define NB_CTL(id)  { id, NB_WIDEN(#id) }

static const LangControlEntry s_aboutControls[] =
{
    NB_CTL(IDC_ABOUT_COPYRIGHT), NB_CTL(IDC_ABOUT_TEAM_LABEL), NB_CTL(IDC_ABOUT_TEAM1),
    NB_CTL(IDC_ABOUT_TEAM2),     NB_CTL(IDC_ABOUT_OK)
};

static const LangControlEntry s_gotoControls[] =
{
    NB_CTL(IDC_GOTO_PROMPT), NB_CTL(IDC_GOTO_OK), NB_CTL(IDC_GOTO_CANCEL)
};

static const LangControlEntry s_findControls[] =
{
    NB_CTL(IDC_FIND_LABEL),   NB_CTL(IDC_FIND_NEXT),    NB_CTL(IDC_REPLACE_LABEL),
    NB_CTL(IDC_REPLACE),      NB_CTL(IDC_REPLACEALL),   NB_CTL(IDC_FIND_OPTIONS),
    NB_CTL(IDC_MATCHCASE),    NB_CTL(IDC_WHOLEWORD),    NB_CTL(IDC_ALLFILES),
    NB_CTL(IDC_DIRUP),        NB_CTL(IDC_DIRDOWN),
    { IDCANCEL, L"IDC_FIND_CLOSE" }
};

static const LangControlEntry s_indentControls[] =
{
    NB_CTL(IDC_INDENT_AUTO),        NB_CTL(IDC_INDENT_AUTO_HINT), NB_CTL(IDC_INDENT_SIZE_LABEL),
    NB_CTL(IDC_INDENT_STYLE_GROUP), NB_CTL(IDC_INDENT_SPACES),    NB_CTL(IDC_INDENT_TABS),
    NB_CTL(IDC_INDENT_GROUP),       NB_CTL(IDC_INDENT_NONE),      NB_CTL(IDC_INDENT_TABS2SP),
    NB_CTL(IDC_INDENT_SP2TABS),
    { IDOK,     L"IDC_INDENT_OK" },
    { IDCANCEL, L"IDC_INDENT_CANCEL" }
};

// Copia texto ASCII. Falha se houver qualquer caractere fora do ASCII.
static BOOL WideToAscii(LPCWSTR lpWide, std::string& out)
{
    out.clear();

    while (*lpWide != L'\0')
    {
        if (*lpWide >= 0x80)
        {
            out.clear();
            return FALSE;
        }
        out.push_back((char)*lpWide);
        ++lpWide;
    }

    return !out.empty();
}

// Idioma de exibicao do Windows, ex.: "pt-BR". Usa o primeiro idioma da lista
// de preferencia do usuario (Vista+, inclusive Windows 7) e, se falhar, o
// idioma de interface padrao.
static BOOL GetSystemUiLanguage(std::string& tag)
{
    WCHAR szLangs[256] = {};
    ULONG numLangs = 0;
    ULONG cchLangs = ARRAYSIZE(szLangs);
    WCHAR szName[LOCALE_NAME_MAX_LENGTH] = {};

    tag.clear();

    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLangs, szLangs, &cchLangs) &&
        numLangs > 0 && szLangs[0] != L'\0' && WideToAscii(szLangs, tag))
    {
        return TRUE;
    }

    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT),
                         szName, ARRAYSIZE(szName), 0) > 0 &&
        WideToAscii(szName, tag))
    {
        return TRUE;
    }

    tag.clear();
    return FALSE;
}

// "<pasta do exe>\lang\". Deixa o buffer vazio se nao conseguir.
static void GetLangDirectory(WCHAR* lpBuffer, size_t cchBuffer)
{
    DWORD len;
    WCHAR* lpSlash;

    lpBuffer[0] = L'\0';

    len = GetModuleFileNameW(NULL, lpBuffer, (DWORD)cchBuffer);
    if (len == 0 || len >= (DWORD)cchBuffer)
    {
        lpBuffer[0] = L'\0';
        return;
    }

    lpSlash = wcsrchr(lpBuffer, L'\\');
    if (lpSlash == NULL)
    {
        lpBuffer[0] = L'\0';
        return;
    }

    lpSlash[1] = L'\0';
    if (FAILED(StringCchCatW(lpBuffer, cchBuffer, L"lang\\")))
        lpBuffer[0] = L'\0';
}

static BOOL ReadLangFile(LPCWSTR lpPath, std::vector<char>& data)
{
    HANDLE hFile;
    LARGE_INTEGER size;
    DWORD read = 0;
    BOOL ok;

    hFile = CreateFileW(lpPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 ||
        size.QuadPart > NOTBAD_LANG_MAX_BYTES)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    data.resize((size_t)size.QuadPart);
    ok = ReadFile(hFile, &data[0], (DWORD)data.size(), &read, NULL) && read == data.size();
    CloseHandle(hFile);

    return ok;
}

static BOOL ReadLangResource(const std::string& resName, std::vector<char>& data)
{
    std::wstring wideName(resName.begin(), resName.end());   // nome ASCII
    HRSRC hRes;
    HGLOBAL hGlobal;
    DWORD size;
    const char* lpData;

    hRes = FindResourceW(g_hInstance, wideName.c_str(), RT_RCDATA);
    if (hRes == NULL)
        return FALSE;

    size = SizeofResource(g_hInstance, hRes);
    if (size == 0 || size > NOTBAD_LANG_MAX_BYTES)
        return FALSE;

    hGlobal = LoadResource(g_hInstance, hRes);
    if (hGlobal == NULL)
        return FALSE;

    lpData = (const char*)LockResource(hGlobal);
    if (lpData == NULL)
        return FALSE;

    data.assign(lpData, lpData + size);
    return TRUE;
}

// Carrega o idioma de um tag: pasta lang primeiro, depois o recurso embutido.
static BOOL LoadLangCandidate(const std::string& tag, LPCWSTR lpLangDir, LangMap& out)
{
    std::vector<char> data;

    if (lpLangDir[0] != L'\0')
    {
        std::wstring path = lpLangDir;

        path.append(tag.begin(), tag.end());
        path += L".json";

        if (ReadLangFile(path.c_str(), data) &&
            langcore::ParseFlatJson(data.data(), data.size(), out))
        {
            return TRUE;
        }
    }

    if (ReadLangResource(langcore::ResourceNameFor(tag), data) &&
        langcore::ParseFlatJson(data.data(), data.size(), out))
    {
        return TRUE;
    }

    out.clear();
    return FALSE;
}

static void RejectLangEntry(LPCWSTR lpKey, LPCWSTR lpReason)
{
    WCHAR szMsg[256];

    StringCchPrintfW(szMsg, ARRAYSIZE(szMsg), L"Notbad: translation ignored (%s): %s\n",
                     lpReason, lpKey);
    OutputDebugStringW(szMsg);
}

// Limpa o que nao serve e monta s_stringsById:
//  - valores vazios sao descartados (cairiam em mensagens em branco);
//  - nas strings da stringtable que sao formatos (tem '%'), a traducao precisa
//    ter exatamente as mesmas conversoes do ingles.
static void ValidateLoadedStrings(void)
{
    LangMap::iterator it;
    size_t i;

    for (it = s_strings.begin(); it != s_strings.end(); )
    {
        if (it->second.empty())
            it = s_strings.erase(it);
        else
            ++it;
    }

    for (i = 0; i < ARRAYSIZE(s_idsTable); ++i)
    {
        WCHAR szBase[1024] = {};
        std::wstring base;

        it = s_strings.find(s_idsTable[i].key);
        if (it == s_strings.end())
            continue;

        LoadStringW(g_hInstance, s_idsTable[i].id, szBase, ARRAYSIZE(szBase));
        base = szBase;

        if (base.find(L'%') != std::wstring::npos &&
            !langcore::SameFormatSpecs(base, it->second))
        {
            RejectLangEntry(s_idsTable[i].key, L"printf specifiers differ from English");
            s_strings.erase(it);
            continue;
        }

        s_stringsById[s_idsTable[i].id] = it->second;
    }
}

void LangInit(void)
{
    std::string tag;
    std::vector<std::string> chain;
    WCHAR szLangDir[MAX_PATH] = L"";
    size_t i;

    s_strings.clear();
    s_stringsById.clear();
    s_szLangTag[0] = L'\0';

    GetSystemUiLanguage(tag);               // falhou = tag vazio = so ingles
    GetLangDirectory(szLangDir, ARRAYSIZE(szLangDir));
    chain = langcore::BuildLangChain(tag);

    for (i = 0; i < chain.size(); ++i)
    {
        if (LoadLangCandidate(chain[i], szLangDir, s_strings))
        {
            std::wstring wideTag(chain[i].begin(), chain[i].end());

            StringCchCopyW(s_szLangTag, ARRAYSIZE(s_szLangTag), wideTag.c_str());
            break;
        }
    }

    ValidateLoadedStrings();
}

// Texto traduzido da chave, ou NULL (use o texto do RC).
LPCWSTR LangGetString(LPCWSTR lpKey)
{
    LangMap::const_iterator it;

    if (lpKey == NULL)
        return NULL;

    it = s_strings.find(lpKey);
    return it != s_strings.end() ? it->second.c_str() : NULL;
}

// Texto traduzido de uma string da stringtable, ou NULL.
LPCWSTR LangGetStringById(UINT id)
{
    std::unordered_map<UINT, std::wstring>::const_iterator it = s_stringsById.find(id);

    return it != s_stringsById.end() ? it->second.c_str() : NULL;
}

// ---------------------------------------------------------------------------
// Aplicacao nos menus e dialogos
//
// Nada aqui muda a logica do programa: so troca o TEXTO de itens e controles
// que ja existem. Chave sem traducao = o texto do notbad.rc fica como esta.
// ---------------------------------------------------------------------------

static void SetMenuItemText(HMENU hMenu, UINT position, LPCWSTR lpText)
{
    MENUITEMINFOW mii = {};

    mii.cbSize     = sizeof(mii);
    mii.fMask      = MIIM_STRING;           // preserva ID, check e habilitado
    mii.dwTypeData = (LPWSTR)lpText;
    SetMenuItemInfoW(hMenu, position, TRUE, &mii);
}

// Chave do menu (IDM_POPUP_*) pela identidade do seu primeiro item de comando.
static LPCWSTR FindPopupKey(HMENU hPopup)
{
    int count = GetMenuItemCount(hPopup);
    int i;

    for (i = 0; i < count; ++i)
    {
        MENUITEMINFOW mii = {};
        size_t k;

        mii.cbSize = sizeof(mii);
        mii.fMask  = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;
        if (!GetMenuItemInfoW(hPopup, (UINT)i, TRUE, &mii))
            continue;

        if ((mii.fType & MFT_SEPARATOR) != 0 || mii.hSubMenu != NULL)
            continue;

        for (k = 0; k < ARRAYSIZE(s_menuPopups); ++k)
        {
            if (s_menuPopups[k].id == mii.wID)
                return s_menuPopups[k].key;
        }
        return NULL;
    }

    return NULL;
}

static void ApplyMenuTexts(HMENU hMenu)
{
    int count = GetMenuItemCount(hMenu);
    int i;

    for (i = 0; i < count; ++i)
    {
        MENUITEMINFOW mii = {};
        LPCWSTR lpKey = NULL;
        LPCWSTR lpText;
        size_t k;

        mii.cbSize = sizeof(mii);
        mii.fMask  = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;
        if (!GetMenuItemInfoW(hMenu, (UINT)i, TRUE, &mii))
            continue;

        if ((mii.fType & MFT_SEPARATOR) != 0)
            continue;

        if (mii.hSubMenu != NULL)
        {
            lpKey = FindPopupKey(mii.hSubMenu);
        }
        else
        {
            for (k = 0; k < ARRAYSIZE(s_menuItems); ++k)
            {
                if (s_menuItems[k].id == mii.wID)
                {
                    lpKey = s_menuItems[k].key;
                    break;
                }
            }
        }

        lpText = LangGetString(lpKey);
        if (lpText != NULL)
            SetMenuItemText(hMenu, (UINT)i, lpText);

        if (mii.hSubMenu != NULL)
            ApplyMenuTexts(mii.hSubMenu);
    }
}

// Traduz o menu da janela principal.
void LangApplyMenu(HWND hWnd)
{
    HMENU hMenu;

    if (hWnd == NULL || s_strings.empty())
        return;

    hMenu = GetMenu(hWnd);
    if (hMenu == NULL)
        return;

    ApplyMenuTexts(hMenu);
    DrawMenuBar(hWnd);
}

static void ApplyControlTexts(HWND hDlg, const LangControlEntry* pEntries, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i)
    {
        LPCWSTR lpText = LangGetString(pEntries[i].key);

        if (lpText != NULL)
            SetDlgItemTextW(hDlg, pEntries[i].id, lpText);
    }
}

// Traduz titulo e controles de um dialogo. Chamar no inicio do WM_INITDIALOG.
// O titulo do Find/Replace e definido pelo proprio codigo (IDS_FIND_TITLE e
// IDS_REPLACE_TITLE), entao aqui so os controles.
void LangApplyDialog(HWND hDlg, UINT idd)
{
    LPCWSTR lpCaption = NULL;

    if (hDlg == NULL || s_strings.empty())
        return;

    switch (idd)
    {
    case IDD_ABOUT:
        ApplyControlTexts(hDlg, s_aboutControls, ARRAYSIZE(s_aboutControls));
        lpCaption = LangGetStringById(IDS_ABOUT_TITLE);
        break;

    case IDD_GOTO:
        ApplyControlTexts(hDlg, s_gotoControls, ARRAYSIZE(s_gotoControls));
        lpCaption = LangGetString(L"IDD_GOTO");
        break;

    case IDD_FIND:
        ApplyControlTexts(hDlg, s_findControls, ARRAYSIZE(s_findControls));
        break;

    case IDD_INDENT:
        ApplyControlTexts(hDlg, s_indentControls, ARRAYSIZE(s_indentControls));
        lpCaption = LangGetString(L"IDD_INDENT");
        break;
    }

    if (lpCaption != NULL)
        SetWindowTextW(hDlg, lpCaption);
}

// Idioma carregado ("pt-BR", "en"...). Vazio quando nenhum arquivo de idioma foi
// encontrado e o aplicativo usa so o RC.
LPCWSTR LangCurrentTag(void)
{
    return s_szLangTag;
}
