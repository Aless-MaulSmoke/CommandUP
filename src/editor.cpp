#include "notbad.h"
#include <string.h>
#include <string>
#include <ole2.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>

static BOOL s_bUpdatingHighlight = FALSE;

static void GetEditSelection(HWND hEdit, int* pStart, int* pEnd)
{
    CHARRANGE cr = {};

    // EM_GETSEL devolve so 16 bits por posicao (quebra acima de 64K chars).
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    *pStart = (int)cr.cpMin;
    *pEnd = (int)cr.cpMax;
}

static int GetEditLineStart(HWND hEdit, int index)
{
    int line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, index, 0);
    int start;

    if (line < 0)
        return 0;

    start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    return start < 0 ? 0 : start;
}

static WCHAR GetEditCharAt(HWND hEdit, int index)
{
    TEXTRANGEW range = {};
    WCHAR szChar[2] = {};

    if (index < 0)
        return L'\0';

    range.chrg.cpMin = index;
    range.chrg.cpMax = index + 1;
    range.lpstrText = szChar;
    SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);

    return szChar[0];
}

static void GetLineIndent(HWND hEdit, int line, LPWSTR lpBuffer, int cchBuffer)
{
    TEXTRANGEW range = {};
    int start;
    int length;
    int count;

    lpBuffer[0] = L'\0';
    if (cchBuffer <= 0)
        return;

    start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (start < 0)
        return;

    length = (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
    if (length <= 0)
        return;

    if (length > cchBuffer - 1)
        length = cchBuffer - 1;

    range.chrg.cpMin = start;
    range.chrg.cpMax = start + length;
    range.lpstrText = lpBuffer;
    SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);
    lpBuffer[length] = L'\0';

    count = 0;
    while (count < length && (lpBuffer[count] == L' ' || lpBuffer[count] == L'\t'))
        ++count;
    lpBuffer[count] = L'\0';
}

static int GetTabWidth(void)
{
    return g_settings.tabWidth > 0 ? g_settings.tabWidth : 4;
}

// Colunas visuais de uma sequencia de espacos/tabs, expandindo cada tab ate
// o proximo multiplo da largura configurada.
static int GetIndentColumns(const WCHAR* lpText, int cchText, int tabWidth)
{
    int column = 0;
    int i;

    for (i = 0; i < cchText; ++i)
    {
        if (lpText[i] == L'\t')
            column += tabWidth - (column % tabWidth);
        else
            column += 1;
    }

    return column;
}

// Monta a indentacao no estilo escolhido (espacos por padrao ou tabs).
static int BuildIndentString(int columns, int tabWidth, BOOL useTabs,
                             LPWSTR lpBuffer, int cchBuffer)
{
    int length = 0;

    if (cchBuffer <= 0)
        return 0;

    if (useTabs)
    {
        while (columns >= tabWidth && length < cchBuffer - 1)
        {
            lpBuffer[length++] = L'\t';
            columns -= tabWidth;
        }
    }

    while (columns > 0 && length < cchBuffer - 1)
    {
        lpBuffer[length++] = L' ';
        --columns;
    }

    lpBuffer[length] = L'\0';
    return length;
}

void EditorNew(void)
{
    DocCreate();
}

// Abertura por arrastar e soltar: se o arquivo ja esta aberto, ativa a aba
// existente em vez de criar outra. Se a aba tem alteracoes nao salvas,
// pergunta se deve reabrir o arquivo descartando-as; "No" apenas mantem a aba
// ativa com as alteracoes.
BOOL EditorOpenDropped(LPCWSTR lpFileName)
{
    Document* pDoc = DocFindByPath(lpFileName);
    WCHAR szFormat[512];
    WCHAR szName[MAX_PATH];
    WCHAR szMessage[1024];

    if (pDoc == NULL)
        return EditorOpen(lpFileName);

    // Primeiro mostra a aba, para o usuario ver de qual arquivo se trata.
    DocActivate(pDoc);

    if (!SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0))
        return TRUE;

    DocGetDisplayName(pDoc, szName, ARRAYSIZE(szName));
    LoadStringResource(IDS_REOPEN_DIRTY, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat, szName);

    if (MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME,
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return TRUE;

    // DocLoad so troca o texto depois de ler o arquivo com sucesso; se a
    // leitura falhar, mostra o erro e a aba continua como estava.
    return DocLoad(pDoc, pDoc->szFilePath);
}

BOOL EditorOpen(LPCWSTR lpFileName)
{
    Document* pDoc = DocCreate();

    if (pDoc == NULL)
        return FALSE;

    if (!DocLoad(pDoc, lpFileName))
    {
        DocDestroy(pDoc);

        if (g_documents.empty())
            DocCreate();

        return FALSE;
    }

    return TRUE;
}

BOOL EditorSave(BOOL bSaveAs)
{
    return DocSave(g_pActiveDoc, bSaveAs);
}

BOOL EditorSaveFile(LPCWSTR lpFileName)
{
    return DocSaveFile(g_pActiveDoc, lpFileName);
}

BOOL EditorQuerySaveChanges(void)
{
    return DocQuerySaveChanges(g_pActiveDoc);
}

void EditorSetModified(BOOL bModified)
{
    if (g_hWndEdit != NULL)
        SendMessageW(g_hWndEdit, EM_SETMODIFY, bModified, 0);
}

BOOL EditorIsModified(void)
{
    if (g_hWndEdit == NULL)
        return FALSE;
    return (BOOL)SendMessageW(g_hWndEdit, EM_GETMODIFY, 0, 0);
}

// O zoom (Ctrl+roda) pertence ao RichEdit, ou seja, a cada aba. Como a fonte,
// ele deve valer para todas as abas: guarda-se o ultimo zoom escolhido e ele e
// replicado nas demais. 0/0 = zoom desligado (100%).
static int s_zoomNum = 0;
static int s_zoomDen = 0;

static void EditorReadZoom(HWND hEdit, int* pNum, int* pDen)
{
    int num = 0;
    int den = 0;

    SendMessageW(hEdit, EM_GETZOOM, (WPARAM)&num, (LPARAM)&den);
    if (num <= 0 || den <= 0)
    {
        num = 0;
        den = 0;
    }

    *pNum = num;
    *pDen = den;
}

// Aplica o zoom global a um editor, se ele estiver diferente.
void EditorApplyGlobalZoomTo(HWND hEdit)
{
    int num;
    int den;

    if (hEdit == NULL || !IsWindow(hEdit))
        return;

    EditorReadZoom(hEdit, &num, &den);
    if (num == s_zoomNum && den == s_zoomDen)
        return;

    SendMessageW(hEdit, EM_SETZOOM, (WPARAM)s_zoomNum, (LPARAM)s_zoomDen);
}

// Adota o zoom de hSource como zoom global e replica nas outras abas.
void EditorSyncZoomFrom(HWND hSource)
{
    size_t i;

    if (hSource == NULL || !IsWindow(hSource))
        return;

    EditorReadZoom(hSource, &s_zoomNum, &s_zoomDen);

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (g_documents[i]->hEdit != hSource)
            EditorApplyGlobalZoomTo(g_documents[i]->hEdit);
    }
}

void EditorSetFont(HFONT hFont)
{
    size_t i;

    if (hFont == NULL)
        return;

    // Guarda o zoom atual (o da aba ativa) antes de mexer na fonte.
    EditorSyncZoomFrom(g_hWndEdit);

    for (i = 0; i < g_documents.size(); ++i)
        SendMessageW(g_documents[i]->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // A fonte e o zoom valem para todas as abas, inclusive as ocultas.
    for (i = 0; i < g_documents.size(); ++i)
        EditorApplyGlobalZoomTo(g_documents[i]->hEdit);

    // A largura do tab depende da largura do espaco da fonte.
    EditorApplyTabWidth();

    // O WM_SETFONT devolve o formato de caractere do texto todo ao padrao e
    // apaga o negrito/italico do highlighting. Reinicia a varredura: a aba
    // ativa e refeita agora e as ocultas se ajustam em DocActivate.
    if (g_settings.codeHighlighting)
    {
        EditorCancelUniversalHighlight();

        for (i = 0; i < g_documents.size(); ++i)
            g_documents[i]->bCodeHighlightSynced = FALSE;

        if (g_pActiveDoc != NULL)
        {
            EditorSyncUniversalHighlight(g_pActiveDoc);
            EditorUpdateCurrentLineHighlight(g_pActiveDoc->hEdit);
        }
    }
}

// Ajusta a largura visual do caractere Tab (em N espacos da fonte atual)
// via ITextDocument::DefaultTabStop. Nao mexe no texto, no undo nem no
// estado de modificado.
void EditorApplyTabWidthTo(HWND hEdit)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    HDC hdc;
    HGDIOBJ oldFont;
    SIZE size = {};
    int dpi;

    if (hEdit == NULL)
        return;

    hdc = GetDC(hEdit);
    if (hdc == NULL)
        return;

    oldFont = SelectObject(hdc, g_hFont != NULL ? (HGDIOBJ)g_hFont
                                                : GetStockObject(SYSTEM_FIXED_FONT));
    GetTextExtentPoint32W(hdc, L" ", 1, &size);
    dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    SelectObject(hdc, oldFont);
    ReleaseDC(hEdit, hdc);

    if (size.cx <= 0 || dpi <= 0)
        return;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            pTextDoc->SetDefaultTabStop((float)(size.cx * GetTabWidth()) * 72.0f / (float)dpi);
            pTextDoc->Release();
        }
        pOle->Release();
    }
}

void EditorApplyTabWidth(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
        EditorApplyTabWidthTo(g_documents[i]->hEdit);
}

// Reaplica ao editor ativo todas as configuracoes de indentacao carregadas
// do registro (largura do tab e estilo espacos/tabs). Chamado no startup e
// apos o dialogo de indentacao.
void EditorApplyIndentation(void)
{
    size_t i;

    EditorApplyTabWidth();

    for (i = 0; i < g_documents.size(); ++i)
        g_documents[i]->bIndentBlockValid = FALSE;

    EditorUpdateCurrentLineHighlight(g_hWndEdit);
    InvalidateLineNumbers();
    UpdateStatusBar();
}

// Numero logico da linha (paragrafo) que contem charIndex, 1-based.
// EM_GETLINECOUNT/EM_LINEINDEX contam linhas fisicas (quebras de word wrap),
// mas a numeracao exibida deve pular as continuacoes. O TOM entrega o indice
// de paragrafo exato. Retorna 0 em caso de falha.
int EditorGetParagraphNumber(HWND hEdit, int charIndex)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pRange = NULL;
    long para = 0;

    if (hEdit == NULL || charIndex < 0)
        return 0;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            if (SUCCEEDED(pTextDoc->Range(charIndex, charIndex, &pRange)) && pRange != NULL)
            {
                pRange->GetIndex(tomParagraph, &para);
                pRange->Release();
            }
            pTextDoc->Release();
        }
        pOle->Release();
    }

    return (int)para;
}

// Posicao do inicio do paragrafo que contem charIndex (0 se falhar).
int EditorGetParagraphStart(HWND hEdit, int charIndex)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pRange = NULL;
    long start = 0;
    long delta = 0;

    if (hEdit == NULL || charIndex < 0)
        return 0;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            if (SUCCEEDED(pTextDoc->Range(charIndex, charIndex, &pRange)) && pRange != NULL)
            {
                if (SUCCEEDED(pRange->StartOf(tomParagraph, 0, &delta)))
                    pRange->GetStart(&start);
                pRange->Release();
            }
            pTextDoc->Release();
        }
        pOle->Release();
    }

    return (int)start;
}

int EditorGetParagraphCount(HWND hEdit)
{
    GETTEXTLENGTHEX gtl = {};
    int length;
    int count;

    if (hEdit == NULL)
        return 1;

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;
    length = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (length < 0)
        length = 0;

    count = EditorGetParagraphNumber(hEdit, length);
    return count > 0 ? count : 1;
}

int EditorGetParagraphStartByNumber(HWND hEdit, int lineNumber)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pRange = NULL;
    long start = 0;
    long moved = 0;

    if (hEdit == NULL)
        return 0;
    if (lineNumber < 1)
        lineNumber = 1;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            if (SUCCEEDED(pTextDoc->Range(0, 0, &pRange)) && pRange != NULL)
            {
                if (SUCCEEDED(pRange->Move(tomParagraph, lineNumber - 1, &moved)))
                    pRange->GetStart(&start);
                pRange->Release();
            }
            pTextDoc->Release();
        }
        pOle->Release();
    }

    return (int)start;
}

static int EditorIndentColumnsAt(HWND hEdit, int start, int textLen, BOOL* pbBlank)
{
    TEXTRANGEW tr = {};
    WCHAR szBuf[1024];
    int tabWidth = g_settings.tabWidth > 0 ? g_settings.tabWidth : 4;
    int got;
    int col = 0;
    int i;

    if (pbBlank != NULL)
        *pbBlank = TRUE;

    if (hEdit == NULL || start < 0 || start >= textLen)
        return 0;

    tr.chrg.cpMin = start;
    tr.chrg.cpMax = start + (LONG)(ARRAYSIZE(szBuf) - 1);
    if (tr.chrg.cpMax > textLen)
        tr.chrg.cpMax = textLen;
    tr.lpstrText = szBuf;

    got = (int)SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    if (got <= 0)
        return 0;

    for (i = 0; i < got; ++i)
    {
        WCHAR ch = szBuf[i];

        if (ch == L' ')
            ++col;
        else if (ch == L'\t')
            col = (col / tabWidth + 1) * tabWidth;
        else
            break;
    }

    if (pbBlank != NULL)
        *pbBlank = (i >= got) || (szBuf[i] == L'\r' || szBuf[i] == L'\n');

    return col;
}

void EditorUpdateIndentBlock(HWND hEdit, int caret, int textLen)
{
    Document* pDoc;
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pIter = NULL;
    ITextRange* pProbe = NULL;
    long para = 0;
    long anchorPara;
    long firstPara;
    long lastPara;
    long paraCount;
    long start = 0;
    long anchorStart = 0;
    long lineStart = 0;
    long moved = 0;
    int base;
    int indent;
    int guard;
    int kind = 0;
    BOOL blank = FALSE;
    BOOL caretBlank;
    BOOL found = FALSE;
    BOOL foundForward = FALSE;
    BOOL isHeader = FALSE;
    BOOL isCloser = FALSE;
    BOOL noBlock = FALSE;

    pDoc = DocFromEdit(hEdit);
    if (pDoc == NULL || hEdit == NULL || textLen < 0 || !g_settings.autoIndent)
        return;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) == 0 || pOle == NULL)
        return;
    if (FAILED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) ||
        pTextDoc == NULL)
    {
        pOle->Release();
        return;
    }
    pOle->Release();

    if (FAILED(pTextDoc->Range(caret, caret, &pIter)) || pIter == NULL)
    {
        pTextDoc->Release();
        return;
    }

    pIter->GetIndex(tomParagraph, &para);
    pIter->StartOf(tomParagraph, 0, &moved);
    pIter->GetStart(&start);

    base = EditorIndentColumnsAt(hEdit, (int)start, textLen, &blank);
    caretBlank = blank;
    anchorPara = para;
    anchorStart = start;

    if (blank && base == 0)
    {
        if (SUCCEEDED(pTextDoc->Range(start, start, &pProbe)) && pProbe != NULL)
        {
            guard = 0;
            while (guard++ < 256)
            {
                moved = 0;
                if (FAILED(pProbe->Move(tomParagraph, -1, &moved)) || moved == 0)
                    break;
                pProbe->GetStart(&lineStart);
                indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
                if (!blank)
                {
                    pProbe->GetIndex(tomParagraph, &anchorPara);
                    anchorStart = lineStart;
                    base = indent;
                    found = TRUE;
                    break;
                }
            }
            pProbe->Release();
            pProbe = NULL;
        }

        if (!found)
        {
            if (SUCCEEDED(pTextDoc->Range(start, start, &pProbe)) && pProbe != NULL)
            {
                guard = 0;
                while (guard++ < 256)
                {
                    moved = 0;
                    if (FAILED(pProbe->Move(tomParagraph, 1, &moved)) || moved == 0)
                        break;
                    pProbe->GetStart(&lineStart);
                    indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
                    if (!blank)
                    {
                        pProbe->GetIndex(tomParagraph, &anchorPara);
                        anchorStart = lineStart;
                        base = indent;
                        found = TRUE;
                        foundForward = TRUE;
                        break;
                    }
                }
                pProbe->Release();
                pProbe = NULL;
            }
        }

        if (!found)
            base = 0;
    }

    paraCount = EditorGetParagraphCount(hEdit);

    pIter->SetRange(anchorStart, anchorStart);
    guard = 0;
    while (guard++ < 4096)
    {
        moved = 0;
        if (FAILED(pIter->Move(tomParagraph, 1, &moved)) || moved == 0)
            break;
        pIter->GetStart(&lineStart);
        indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
        if (blank)
            continue;
        isHeader = (indent > base);
        break;
    }

    if (!isHeader)
    {
        pIter->SetRange(anchorStart, anchorStart);
        guard = 0;
        while (guard++ < 4096)
        {
            moved = 0;
            if (FAILED(pIter->Move(tomParagraph, -1, &moved)) || moved == 0)
                break;
            pIter->GetStart(&lineStart);
            indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
            if (blank)
                continue;
            isCloser = (indent > base);
            break;
        }
    }

    if (isHeader)
        kind = 1;
    else if (isCloser)
        kind = 2;

    // Linha vazia (sem indentacao) que herdou a ancora de uma linha vizinha:
    // - ancora anterior que fecha um bloco (kind 2): a linha vazia vem depois
    //   do fim do bloco, portanto nao pertence a ele;
    // - ancora posterior que abre um bloco (kind 1): a linha vazia vem antes
    //   do inicio do bloco, portanto tambem nao pertence a ele.
    // Nesses casos a propria linha vazia passa a ser a ancora, no mesmo nivel
    // da vizinha. Em nivel 0 nenhum bloco e destacado; em nivel > 0 vale o bloco
    // pai. Uma linha vazia logo apos o cabecalho do bloco (kind 1 com ancora
    // anterior) continua dentro dele.
    if (caretBlank && found && (kind == 2 || (kind == 1 && foundForward)))
    {
        anchorPara = para;
        anchorStart = start;
        kind = 0;
    }

    if (pDoc->bIndentBlockValid &&
        pDoc->indentBlockParaCount == (int)paraCount &&
        pDoc->indentBlockBase == base &&
        pDoc->indentBlockKind == kind &&
        (pDoc->indentBlockFirst == 0 ||
         (para >= pDoc->indentBlockFirst && para <= pDoc->indentBlockLast)) &&
        (kind == 0 || pDoc->indentBlockAnchorPara == (int)anchorPara))
    {
        pIter->Release();
        pTextDoc->Release();
        return;
    }

    firstPara = anchorPara;
    lastPara = anchorPara;

    if (kind == 1)
    {
        lastPara = paraCount;

        pIter->SetRange(anchorStart, anchorStart);
        guard = 0;
        while (guard++ < 4096)
        {
            moved = 0;
            if (FAILED(pIter->Move(tomParagraph, 1, &moved)) || moved == 0)
                break;
            pIter->GetStart(&lineStart);
            indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
            if (blank)
                continue;
            if (indent <= base)
            {
                pIter->GetIndex(tomParagraph, &lastPara);
                break;
            }
        }
    }
    else if (kind == 2)
    {
        firstPara = 1;
        lastPara = anchorPara;

        pIter->SetRange(anchorStart, anchorStart);
        guard = 0;
        while (guard++ < 4096)
        {
            moved = 0;
            if (FAILED(pIter->Move(tomParagraph, -1, &moved)) || moved == 0)
                break;
            pIter->GetStart(&lineStart);
            indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
            pIter->GetIndex(tomParagraph, &firstPara);
            if (!blank && indent <= base)
                break;
        }
    }
    else if (base == 0)
    {
        noBlock = TRUE;
    }
    else
    {
        pIter->SetRange(anchorStart, anchorStart);
        guard = 0;
        while (guard++ < 4096)
        {
            moved = 0;
            if (FAILED(pIter->Move(tomParagraph, -1, &moved)) || moved == 0)
                break;
            pIter->GetStart(&lineStart);
            indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
            pIter->GetIndex(tomParagraph, &firstPara);
            if (!blank && indent < base)
                break;
        }

        pIter->SetRange(anchorStart, anchorStart);
        guard = 0;
        while (guard++ < 4096)
        {
            moved = 0;
            if (FAILED(pIter->Move(tomParagraph, 1, &moved)) || moved == 0)
                break;
            pIter->GetStart(&lineStart);
            indent = EditorIndentColumnsAt(hEdit, (int)lineStart, textLen, &blank);
            pIter->GetIndex(tomParagraph, &lastPara);
            if (!blank && indent < base)
                break;
        }
    }

    if (noBlock)
    {
        firstPara = 0;
        lastPara = 0;
    }
    else
    {
        if (caretBlank)
        {
            if (para < firstPara)
                firstPara = para;
            if (para > lastPara)
                lastPara = para;
        }

        if (firstPara < 1)
            firstPara = 1;
        if (lastPara > paraCount)
            lastPara = paraCount;
        if (lastPara < firstPara)
            lastPara = firstPara;
    }

    if (!pDoc->bIndentBlockValid ||
        pDoc->indentBlockFirst != (int)firstPara ||
        pDoc->indentBlockLast != (int)lastPara)
    {
        pDoc->indentBlockFirst = (int)firstPara;
        pDoc->indentBlockLast = (int)lastPara;
        if (pDoc == g_pActiveDoc)
            InvalidateLineNumbers();
    }

    pDoc->indentBlockBase = base;
    pDoc->indentBlockParaCount = (int)paraCount;
    pDoc->indentBlockKind = kind;
    pDoc->indentBlockAnchorPara = (kind == 0) ? 0 : (int)anchorPara;
    pDoc->bIndentBlockValid = TRUE;

    pIter->Release();
    pTextDoc->Release();
}

// No RichEdit o estilo sozinho nao controla a quebra: e preciso mandar
// EM_SETTARGETDEVICE com lParam 0 (quebra na janela) ou 1 (sem quebra).
void EditorApplyWordWrapTo(HWND hEdit, BOOL bWrap)
{
    LONG_PTR style;

    if (hEdit == NULL)
        return;

    style = GetWindowLongPtrW(hEdit, GWL_STYLE);
    if (bWrap)
        style &= ~(WS_HSCROLL | ES_AUTOHSCROLL);
    else
        style |= WS_HSCROLL | ES_AUTOHSCROLL;

    SetWindowLongPtrW(hEdit, GWL_STYLE, style);
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, bWrap ? 0 : 1);
    SetWindowPos(hEdit, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    InvalidateRect(hEdit, NULL, TRUE);
}

void EditorApplyWordWrap(BOOL bWrap)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
        EditorApplyWordWrapTo(g_documents[i]->hEdit, bWrap);
}

void EditorSelectAll(void)
{
    if (g_hWndEdit == NULL)
        return;

    SendMessageW(g_hWndEdit, EM_SETSEL, 0, -1);
    SetFocus(g_hWndEdit);
}

void EditorPlaceCaretAtPoint(HWND hEdit, POINT pt)
{
    RECT rc;
    LRESULT position;

    if (hEdit == NULL)
        return;

    GetClientRect(hEdit, &rc);
    if (!PtInRect(&rc, pt))
        return;

    position = SendMessageW(hEdit, EM_CHARFROMPOS, 0, (LPARAM)&pt);
    if (position < 0)
        return;

    SendMessageW(hEdit, EM_SETSEL, position, position);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    InvalidateLineNumbers();
    UpdateStatusBar();
}

// Ao reativar a janela com um clique sobre o texto, o primeiro clique tambem
// posiciona o cursor no local apontado. So age quando o botao esquerdo esta
// pressionado e o ponto esta dentro do editor, para nao mover o cursor em
// ativacoes por teclado, menus, abas ou barra de status.
void EditorPlaceCaretAtClick(HWND hEdit)
{
    POINT pt;

    if (hEdit == NULL)
        return;

    if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0)
        return;

    if (!GetCursorPos(&pt))
        return;

    ScreenToClient(hEdit, &pt);
    EditorPlaceCaretAtPoint(hEdit, pt);
}

void EditorInsertTimeDate(void)
{
    SYSTEMTIME st;
    WCHAR szDate[128];
    WCHAR szTime[128];
    WCHAR szResult[256];

    if (g_hWndEdit == NULL)
        return;

    GetLocalTime(&st);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL,
                   szTime, ARRAYSIZE(szTime));
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL,
                   szDate, ARRAYSIZE(szDate));

    StringCchPrintfW(szResult, ARRAYSIZE(szResult), L"%s %s", szTime, szDate);

    SendMessageW(g_hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)szResult);
}

void EditorSetReadOnly(BOOL bReadOnly)
{
    if (g_hWndEdit != NULL)
        SendMessageW(g_hWndEdit, EM_SETREADONLY, bReadOnly, 0);
}

void EditorUpdateEncodingStatus(void)
{
    UpdateStatusBar();
}

// Pergunta o que fazer quando "converter" e "reinterpretar" dariam resultados
// diferentes. Retorna IDYES (converter), IDNO (reinterpretar) ou IDCANCEL.
static int AskEncodingChoice(TextEncoding target)
{
    WCHAR szFormat[512];
    WCHAR szMessage[1024];

    LoadStringResource(IDS_ENC_ASK, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat,
                     EncodingName(target), EncodingName(target));

    return MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME,
                       MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
}

static BOOL ConfirmLossyEncoding(TextEncoding target)
{
    WCHAR szFormat[512];
    WCHAR szMessage[1024];

    LoadStringResource(IDS_ENC_LOSS, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat, EncodingName(target));

    return MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME,
                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK;
}

// Troca o texto do documento mantendo, de forma aproximada, a selecao e a
// rolagem. O documento fica marcado como modificado (asterisco na aba): o que
// aparece na tela mudou e ainda nao foi salvo, e sumir com o asterisco aqui
// faria parecer que o arquivo foi gravado. Sem historico de desfazer.
static void ReplaceTextAfterReinterpret(Document* pDoc, LPCWSTR lpNew)
{
    CHARRANGE cr = {};
    POINT scroll = {};

    SendMessageW(pDoc->hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    SendMessageW(pDoc->hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);

    EditorClearCurrentLineHighlight(pDoc);
    SetWindowTextW(pDoc->hEdit, lpNew);

    // Trocar o texto pode redefinir a largura do tab do RichEdit.
    EditorApplyTabWidthTo(pDoc->hEdit);

    // O comprimento pode ter mudado; o RichEdit limita a selecao ao fim.
    SendMessageW(pDoc->hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(pDoc->hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);

    SendMessageW(pDoc->hEdit, EM_SETMODIFY, TRUE, 0);
    SendMessageW(pDoc->hEdit, EM_EMPTYUNDOBUFFER, 0, 0);

    EditorUpdateCurrentLineHighlight(pDoc->hEdit);
    EditorRestartUniversalHighlight(pDoc, TRUE);
}

// Format > Encoding. Muda a codificacao do documento ativo:
//  - Converter: o texto na tela nao muda; so a forma de gravar. O documento
//    passa a modificado, pois os bytes em disco serao outros.
//  - Reinterpretar: os bytes do arquivo sao lidos de outro modo (o texto na
//    tela muda) e o documento fica modificado. So e oferecido para arquivo com
//    caminho que esteja sem edicoes ou que ja tenha sido reinterpretado desde
//    a ultima gravacao (para poder voltar), quando o resultado difere do texto
//    atual; nos demais casos so existe converter.
void EditorSetEncoding(TextEncoding encoding)
{
    Document* pDoc = g_pActiveDoc;
    LPWSTR lpText = NULL;
    LPWSTR lpNew = NULL;
    int length;
    BOOL bModified;
    BOOL bReinterpret = FALSE;

    if (pDoc == NULL || pDoc->hEdit == NULL || pDoc->encoding == encoding)
        return;

    bModified = (BOOL)SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0);
    length = GetWindowTextLengthW(pDoc->hEdit);

    if (length > 0)
    {
        lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (lpText == NULL)
            return;
        GetWindowTextW(pDoc->hEdit, lpText, length + 1);
    }

    if (lpText != NULL)
    {
        if (pDoc->bHasPath && (!bModified || pDoc->bReinterpreted) &&
            ReinterpretText(lpText, pDoc->encoding, encoding, &lpNew) &&
            lstrcmpW(lpText, lpNew) != 0)
        {
            int answer = AskEncodingChoice(encoding);

            if (answer != IDYES && answer != IDNO)
            {
                LocalFree(lpNew);
                LocalFree(lpText);
                return;
            }

            bReinterpret = (answer == IDNO);
        }

        // Ao converter, o texto atual precisa caber na nova codificacao.
        if (!bReinterpret && !CanEncodeLossless(lpText, encoding) &&
            !ConfirmLossyEncoding(encoding))
        {
            if (lpNew != NULL)
                LocalFree(lpNew);
            LocalFree(lpText);
            return;
        }
    }

    if (bReinterpret)
    {
        ReplaceTextAfterReinterpret(pDoc, lpNew);
        pDoc->bReinterpreted = TRUE;
    }
    else if (length > 0 || pDoc->bHasPath)
    {
        // Documento novo e vazio nao vira "sujo" so por trocar de codificacao.
        SendMessageW(pDoc->hEdit, EM_SETMODIFY, TRUE, 0);
    }

    pDoc->encoding = encoding;

    if (lpNew != NULL)
        LocalFree(lpNew);
    if (lpText != NULL)
        LocalFree(lpText);

    DocUpdateTabTitle(pDoc);
    InvalidateLineNumbers();
    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
}

// ---------------------------------------------------------------------------
// Modo Insert / Overwrite (tecla Insert). O estado fica em Document::bOverwrite.
// No Overwrite o caret vira um sublinhado (como nos editores antigos): tem a
// largura do caractere sob o cursor e a mesma espessura da barra do modo
// Insert, na base da linha. O caractere digitado substitui esse caractere; no
// Insert volta a barra fina.
// ---------------------------------------------------------------------------
static HWND s_hCaretOwner = NULL;   // editor cujo caret foi recriado aqui
static int  s_caretWidth  = 0;
static int  s_caretHeight = 0;

// Esquece o caret recriado: o RichEdit refaz o proprio caret no foco e na
// troca de fonte, entao a forma precisa ser reaplicada depois.
void EditorResetCaretShape(void)
{
    s_hCaretOwner = NULL;
    s_caretWidth = 0;
    s_caretHeight = 0;
}

static int GetNormalCaretWidth(void)
{
    DWORD width = 0;

    if (!SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0) || width < 1)
        width = 1;

    return (int)width;
}

// Largura e altura do caret e deslocamento vertical dele dentro da linha. No
// Overwrite a largura e a do caractere sob o cursor (um espaco no fim da
// linha, em Tab ou no fim do texto), a altura e a espessura da barra do modo
// Insert e o deslocamento leva o sublinhado ate a base da linha.
static BOOL GetCaretMetrics(HWND hEdit, int caret, BOOL bOverwrite,
                            int* pWidth, int* pHeight, int* pYOffset)
{
    int lineHeight;

    HDC hdc;
    HGDIOBJ oldFont;
    TEXTMETRICW tm = {};
    SIZE size = {};
    WCHAR chars[2] = {};
    int count = 1;
    BOOL bRealChar = TRUE;
    int zoomNum = 0;
    int zoomDen = 0;

    // Com zoom (Ctrl+roda) o texto e desenhado em outra escala; sem isso o
    // caret ficava com o tamanho da fonte a 100%.
    SendMessageW(hEdit, EM_GETZOOM, (WPARAM)&zoomNum, (LPARAM)&zoomDen);
    if (zoomNum <= 0 || zoomDen <= 0)
    {
        zoomNum = 1;
        zoomDen = 1;
    }

    hdc = GetDC(hEdit);
    if (hdc == NULL)
        return FALSE;

    oldFont = SelectObject(hdc, g_hFont != NULL ? (HGDIOBJ)g_hFont
                                                : GetStockObject(SYSTEM_FIXED_FONT));
    GetTextMetricsW(hdc, &tm);

    if (bOverwrite)
    {
        chars[0] = GetEditCharAt(hEdit, caret);

        if (chars[0] >= 0xD800 && chars[0] <= 0xDBFF)
        {
            chars[1] = GetEditCharAt(hEdit, caret + 1);
            if (chars[1] >= 0xDC00 && chars[1] <= 0xDFFF)
                count = 2;
        }

        if (chars[0] < L' ')    // inclui o fim do texto (NUL), CR, LF e Tab
        {
            chars[0] = L' ';
            count = 1;
            bRealChar = FALSE;
        }

        GetTextExtentPoint32W(hdc, chars, count, &size);
        *pWidth = size.cx > 1 ? (int)size.cx : (int)tm.tmAveCharWidth;
        *pWidth = MulDiv(*pWidth, zoomNum, zoomDen);

        // A largura exata do caractere na tela (ja com zoom) e a distancia
        // entre ele e o proximo, quando ambos estao na mesma linha.
        if (bRealChar)
        {
            POINT p1 = {};
            POINT p2 = {};

            SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&p1, caret);
            SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&p2, caret + count);
            if (p2.y == p1.y && p2.x > p1.x)
                *pWidth = p2.x - p1.x;
        }
    }
    else
    {
        *pWidth = GetNormalCaretWidth();
    }

    lineHeight = MulDiv((int)tm.tmHeight, zoomNum, zoomDen);

    if (bOverwrite)
    {
        // Sublinhado: mesma espessura da barra vertical do modo Insert (nao
        // escala com o zoom, como a barra), colado na base da linha.
        *pHeight = GetNormalCaretWidth();
        if (*pHeight > lineHeight)
            *pHeight = lineHeight;
        *pYOffset = lineHeight - *pHeight;
    }
    else
    {
        *pHeight = lineHeight;
        *pYOffset = 0;
    }

    SelectObject(hdc, oldFont);
    ReleaseDC(hEdit, hdc);

    return *pWidth > 0 && *pHeight > 0;
}

// Aplica a forma do caret conforme o modo do documento. Chamado quando a
// selecao ou o texto mudam, no foco e depois de a tecla Insert alternar o modo.
// No modo Insert so age se antes houve um caret em bloco a desfazer.
void EditorUpdateCaretShape(HWND hEdit)
{
    Document* pDoc;
    POINT pt = {};
    int start;
    int end;
    int width;
    int height;
    int yOffset = 0;
    BOOL bOverwrite;

    if (hEdit == NULL || GetFocus() != hEdit)
        return;

    pDoc = DocFromEdit(hEdit);
    if (pDoc == NULL)
        return;

    bOverwrite = pDoc->bOverwrite;
    if (!bOverwrite && s_hCaretOwner != hEdit)
        return;

    // Com texto selecionado nao ha caret visivel.
    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return;

    if (!GetCaretMetrics(hEdit, start, bOverwrite, &width, &height, &yOffset))
        return;

    // COMENTADO OU REMOVIDO: Este IF impedia o cursor de ser reaplicado 
    // quando você digitava letras seguidas de mesmo tamanho físico.
    /*
    if (s_hCaretOwner == hEdit && s_caretWidth == width && s_caretHeight == height)
        return;
    */

    SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&pt, start);
    pt.y += yOffset;    // Overwrite: sublinhado na base da linha

    // Area em que o caret pode aparecer: a interseccao entre o retangulo de
    // formatacao e a area cliente. Com zoom ou rolagem o caractere pode estar
    // na borda ou fora dela, e um caret nessa posicao era desenhado por cima
    // da barra de rolagem e de outras areas.
    BOOL bVisible;
    {
        RECT rcView = {};
        RECT rcClient = {};

        SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&rcView);
        GetClientRect(hEdit, &rcClient);
        IntersectRect(&rcView, &rcView, &rcClient);

        bVisible = pt.x >= rcView.left && pt.x < rcView.right &&
                   pt.y >= rcView.top && pt.y + height <= rcView.bottom;

        // Na borda direita o bloco e encurtado para nao ultrapassar a area.
        if (bVisible && pt.x + width > rcView.right)
            width = rcView.right - pt.x;
        if (width < 1)
            width = 1;
    }

    // CreateCaret destroi o caret anterior do RichEdit e o recria com o nosso tamanho
    if (!CreateCaret(hEdit, NULL, width, height))
        return;

    // Fora da area visivel o caret fica criado, mas oculto e sem posicao,
    // ate o caractere voltar a vista (o timer reaplica quando a rolagem muda).
    if (!bVisible)
    {
        s_hCaretOwner = hEdit;
        s_caretWidth = width;
        s_caretHeight = height;
        return;
    }

    SetCaretPos(pt.x, pt.y);
    ShowCaret(hEdit);

    if (bOverwrite)
    {
        s_hCaretOwner = hEdit;
        s_caretWidth = width;
        s_caretHeight = height;
    }
    else
    {
        EditorResetCaretShape();
    }
}

// Modo Overwrite: troca o caractere sob o cursor pelo digitado em UMA operacao,
// sem selecionar antes e sem deixar o RichEdit tratar o WM_CHAR. Antes, a
// selecao do caractere + a insercao geravam notificacoes e repinturas em
// sequencia (destaque da linha, caret, numeros de linha) e a tela piscava.
// Retorna TRUE quando o caractere foi tratado aqui (o WM_CHAR deve ser
// descartado); FALSE deixa o caminho antigo/padrao agir (pares substitutos,
// controle, selecao ativa, fim de linha ou do texto).
BOOL EditorTryOverwriteChar(HWND hEdit, WCHAR ch)
{
    WCHAR current;
    WCHAR text[2] = {};
    CHARRANGE cr;
    NMHDR nm = {};
    HWND hParent;
    DWORD oldMask;
    int start;
    int end;
    int length = 1;
    int id;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    if (ch < L' ' || ch == 0x7F)
        return FALSE;

    if (ch >= 0xD800 && ch <= 0xDFFF)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return FALSE;

    current = GetEditCharAt(hEdit, start);
    if (current == L'\0' || current == L'\r' || current == L'\n')
        return FALSE;

    if (current >= 0xD800 && current <= 0xDBFF)
    {
        WCHAR next = GetEditCharAt(hEdit, start + 1);
        if (next >= 0xDC00 && next <= 0xDFFF)
            length = 2;
    }

    text[0] = ch;
    cr.cpMin = start;
    cr.cpMax = start + length;

    // Sem notificacoes durante a troca: o RichEdit nao avisa a janela principal
    // com o texto/selecao pela metade. Elas sao enviadas uma vez no final.
    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)text);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, (LPARAM)oldMask);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);

    hParent = GetParent(hEdit);
    if (hParent != NULL)
    {
        id = GetDlgCtrlID(hEdit);

        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, EN_CHANGE), (LPARAM)hEdit);

        nm.hwndFrom = hEdit;
        nm.idFrom = (UINT_PTR)id;
        nm.code = EN_SELCHANGE;
        SendMessageW(hParent, WM_NOTIFY, (WPARAM)id, (LPARAM)&nm);
    }

    return TRUE;
}

// Chamado antes de o RichEdit processar um caractere digitado no modo
// Overwrite: seleciona o caractere sob o cursor para que o digitado o
// substitua. No fim da linha ou do texto nada e selecionado (insere normal).
void EditorHandleOverwriteChar(HWND hEdit, WCHAR ch)
{
    WCHAR current;
    int start;
    int end;
    int length = 1;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return;

    if (ch < L' ' || ch == 0x7F)
        return;

    // A segunda metade de um par substituto completa o par ja digitado.
    if (ch >= 0xDC00 && ch <= 0xDFFF)
        return;

    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return;

    current = GetEditCharAt(hEdit, start);
    if (current == L'\0' || current == L'\r' || current == L'\n')
        return;

    if (current >= 0xD800 && current <= 0xDBFF)
    {
        WCHAR next = GetEditCharAt(hEdit, start + 1);
        if (next >= 0xDC00 && next <= 0xDFFF)
            length = 2;
    }

    SendMessageW(hEdit, EM_SETSEL, start, start + length);
}

// O TranslateMessage ja enfileirou o WM_CHAR da tecla antes de o WM_KEYDOWN
// chegar aqui; se nao for removido, o RichEdit age sobre ele depois de nos.
static void RemovePendingChar(HWND hEdit, WCHAR ch)
{
    MSG chMsg;

    if (PeekMessageW(&chMsg, hEdit, WM_CHAR, WM_CHAR, PM_NOREMOVE) &&
        chMsg.wParam == (WPARAM)ch)
    {
        PeekMessageW(&chMsg, hEdit, WM_CHAR, WM_CHAR, PM_REMOVE);
    }
}

// O RichEdit 5.0 trata o Enter ja no WM_KEYDOWN (insere a quebra de linha
// sozinho), por isso o auto-indent precisa agir aqui e nao no WM_CHAR.
static BOOL EditorHandleEnter(HWND hEdit)
{
    WCHAR szIndent[256];
    WCHAR szInsert[1024];
    int start;
    int end;
    int line;
    int caret;
    int columns;
    int indentLength;

    if (!g_settings.autoIndent)
        return FALSE;

    // Shift/Ctrl/Alt+Enter mantem o comportamento padrao do RichEdit.
    if ((GetKeyState(VK_SHIFT) | GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU)) & 0x8000)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);

    // Com area marcada o Enter se comporta como se a auto-indentacao
    // estivesse desligada: o RichEdit troca a selecao por uma quebra de
    // linha simples e o cursor nao e posicionado em indentacao.
    if (start != end)
        return FALSE;

    line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, start, 0);
    if (line < 0)
        line = 0;
    GetLineIndent(hEdit, line, szIndent, ARRAYSIZE(szIndent));

    // Reaplica a indentacao no estilo configurado, para que uma linha nova
    // nao misture tabs e espacos com o restante do arquivo.
    columns = GetIndentColumns(szIndent, lstrlenW(szIndent), GetTabWidth());
    indentLength = BuildIndentString(columns, GetTabWidth(),
                                     g_settings.indentWithTabs,
                                     szIndent, ARRAYSIZE(szIndent));

    StringCchPrintfW(szInsert, ARRAYSIZE(szInsert), L"\r%s", szIndent);
    caret = start + 1 + indentLength;

    RemovePendingChar(hEdit, L'\r');

    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)szInsert);
    SendMessageW(hEdit, EM_SETSEL, caret, caret);
    InvalidateLineNumbers();
    UpdateStatusBar();
    return TRUE;
}

// Home com auto-indent ativo: primeiro vai ao inicio do texto da linha
// (apos a indentacao); estando ali, o Home padrao leva ao inicio real.
static BOOL EditorHandleHome(HWND hEdit)
{
    WCHAR szLine[2048];
    int start;
    int end;
    int line;
    int lineStart;
    int lineLen;
    int indent;
    int target;

    if (!g_settings.autoIndent)
        return FALSE;

    if ((GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU)) & 0x8000)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return FALSE;

    line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, start, 0);
    if (line < 0)
        return FALSE;

    lineStart = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (lineStart < 0)
        return FALSE;

    lineLen = (int)SendMessageW(hEdit, EM_LINELENGTH, lineStart, 0);
    if (lineLen <= 0)
        return FALSE;
    if (lineLen > (int)ARRAYSIZE(szLine) - 1)
        lineLen = (int)ARRAYSIZE(szLine) - 1;

    *(WORD*)szLine = (WORD)lineLen;
    lineLen = (int)SendMessageW(hEdit, EM_GETLINE, line, (LPARAM)szLine);
    if (lineLen <= 0)
        return FALSE;

    for (indent = 0; indent < lineLen; ++indent)
    {
        if (szLine[indent] != L' ' && szLine[indent] != L'\t')
            break;
    }

    target = lineStart + indent;
    if (start == target || (start == lineStart && indent == 0))
        return FALSE;

    // Com Shift (sem selecao previa) estende a selecao ate o destino.
    if (GetKeyState(VK_SHIFT) & 0x8000)
        SendMessageW(hEdit, EM_SETSEL, start, target);
    else
        SendMessageW(hEdit, EM_SETSEL, target, target);

    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    UpdateStatusBar();
    return TRUE;
}

// Operacoes em lote: sem notificacoes (EN_CHANGE/EN_SELCHANGE por linha) e
// sem repintura intermediaria, para nao piscar nem ficar lento.
static DWORD EditorBeginBatch(HWND hEdit)
{
    DWORD oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    return oldMask;
}

static void EditorEndBatch(HWND hEdit, DWORD oldMask)
{
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, (LPARAM)oldMask);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static ITextDocument* EditorGetTextDocument(HWND hEdit)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;

    if (hEdit == NULL)
        return NULL;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (FAILED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)))
            pTextDoc = NULL;
        pOle->Release();
    }

    return pTextDoc;
}

// Extremidade ativa da selecao (a que se move com o mouse/Shift+setas). Numa
// selecao feita de baixo para cima ela e o inicio, nao o fim; a linha em
// destaque acompanha essa extremidade. EM_EXGETSEL so informa min/max.
static int EditorGetActiveEnd(HWND hEdit, int selStart, int selEnd)
{
    ITextDocument* pTextDoc;
    ITextSelection* pSel = NULL;
    long flags = 0;
    int active = selEnd;

    if (selEnd <= selStart)
        return selEnd;

    pTextDoc = EditorGetTextDocument(hEdit);
    if (pTextDoc == NULL)
        return selEnd;

    if (SUCCEEDED(pTextDoc->GetSelection(&pSel)) && pSel != NULL)
    {
        if (SUCCEEDED(pSel->GetFlags(&flags)) && (flags & tomSelStartActive))
            active = selStart;
        pSel->Release();
    }

    pTextDoc->Release();
    return active;
}

static void EditorSetTextRangeBackColor(ITextRange* pRange, long color)
{
    ITextFont* pFont = NULL;

    if (pRange == NULL)
        return;

    if (SUCCEEDED(pRange->GetFont(&pFont)) && pFont != NULL)
    {
        pFont->SetBackColor(color);
        pFont->Release();
    }
}

static void EditorSetTextRangeForeColor(ITextRange* pRange, long color)
{
    ITextFont* pFont = NULL;

    if (pRange == NULL)
        return;

    if (SUCCEEDED(pRange->GetFont(&pFont)) && pFont != NULL)
    {
        pFont->SetForeColor(color);
        pFont->Release();
    }
}

// Pinta (ou devolve ao normal) o trecho da linha em destaque: fundo
// NOTBAD_HIGHLIGHT_COLOR e texto NOTBAD_ACTIVE_TEXT_COLOR; normal = fundo
// automatico e texto NOTBAD_TEXT_COLOR.
static void EditorPaintLineRange(ITextRange* pRange, BOOL bActive)
{
    EditorSetTextRangeBackColor(pRange, bActive ? (long)NOTBAD_HIGHLIGHT_COLOR : (long)tomAutoColor);
    EditorSetTextRangeForeColor(pRange, bActive ? (long)NOTBAD_ACTIVE_TEXT_COLOR : (long)NOTBAD_TEXT_COLOR);
}

// Idem para o trecho selecionado, so na cor do texto. O fundo do texto marcado
// e desenhado pelo proprio RichEdit (cor de selecao do sistema): pintar esse
// fundo como formato de caractere briga com o desenho da selecao e deixava
// trechos marcados e nao marcados na mesma linha.
static void EditorPaintSelectionRange(ITextRange* pRange, BOOL bActive)
{
    EditorSetTextRangeForeColor(pRange, bActive ? (long)NOTBAD_ACTIVE_TEXT_COLOR : (long)NOTBAD_TEXT_COLOR);
}

// Reaplica as cores ativas na linha em destaque e na selecao. Usada
// quando algo (troca de fonte, cor padrao) devolveu todo o texto ao cinza.
static void EditorReassertActiveTextColor(Document* pDoc)
{
    if (pDoc == NULL)
        return;

    EditorPaintLineRange(pDoc->pHighlightRange, TRUE);
    EditorPaintSelectionRange(pDoc->pSelectionRange, TRUE);
}

// Cor do texto do documento: NOTBAD_TEXT_COLOR vira a cor padrao do RichEdit
// (vale para o texto carregado e para o digitado). O WM_SETFONT tambem mexe no
// formato padrao, por isso isto roda depois dele. Nao toca no undo, no estado
// de modificado nem nas notificacoes.
void EditorApplyTextColorTo(HWND hEdit)
{
    CHARFORMATW cf = {};
    ITextDocument* pTextDoc;
    DWORD oldMask;
    BOOL modified;

    if (hEdit == NULL)
        return;

    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = NOTBAD_TEXT_COLOR;   // dwEffects = 0: sem CFE_AUTOCOLOR

    pTextDoc = EditorGetTextDocument(hEdit);
    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);

    if (pTextDoc != NULL)
        pTextDoc->Undo(tomSuspend, NULL);

    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    // SCF_ALL tambem cobriu a linha em destaque e a selecao.
    EditorReassertActiveTextColor(DocFromEdit(hEdit));

    if (pTextDoc != NULL)
    {
        pTextDoc->Undo(tomResume, NULL);
        pTextDoc->Release();
    }

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
}

void EditorClearCurrentLineHighlight(Document* pDoc)
{
    ITextDocument* pTextDoc;
    DWORD oldMask;
    BOOL modified;
    HWND hEdit;

    if (pDoc == NULL || pDoc->hEdit == NULL || s_bUpdatingHighlight)
        return;

    // O texto vai ser trocado: os tamanhos guardados deixam de valer.
    pDoc->fgPrevSelLen = 0;
    pDoc->fgPrevTextLen = -1;

    if (pDoc->pHighlightRange == NULL && pDoc->pSelectionRange == NULL)
        return;

    if (!IsWindow(pDoc->hEdit))
    {
        if (pDoc->pHighlightRange != NULL)
            pDoc->pHighlightRange->Release();
        if (pDoc->pSelectionRange != NULL)
            pDoc->pSelectionRange->Release();
        pDoc->pHighlightRange = NULL;
        pDoc->pSelectionRange = NULL;
        return;
    }

    s_bUpdatingHighlight = TRUE;
    hEdit = pDoc->hEdit;
    pTextDoc = EditorGetTextDocument(hEdit);
    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);

    if (pTextDoc != NULL)
        pTextDoc->Undo(tomSuspend, NULL);

    if (pDoc->pSelectionRange != NULL)
    {
        EditorPaintSelectionRange(pDoc->pSelectionRange, FALSE);
        pDoc->pSelectionRange->Release();
        pDoc->pSelectionRange = NULL;
    }

    if (pDoc->pHighlightRange != NULL)
    {
        EditorPaintLineRange(pDoc->pHighlightRange, FALSE);
        pDoc->pHighlightRange->Release();
        pDoc->pHighlightRange = NULL;
    }

    if (pTextDoc != NULL)
    {
        pTextDoc->Undo(tomResume, NULL);
        pTextDoc->Release();
    }

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
    s_bUpdatingHighlight = FALSE;
}

// Limites [inicio, fim) de um ITextRange; FALSE se nulo ou vazio.
static BOOL EditorRangeBounds(ITextRange* pRange, long* pStart, long* pEnd)
{
    *pStart = 0;
    *pEnd = 0;

    if (pRange == NULL)
        return FALSE;

    if (FAILED(pRange->GetStart(pStart)) || FAILED(pRange->GetEnd(pEnd)))
    {
        *pStart = 0;
        *pEnd = 0;
        return FALSE;
    }

    return *pEnd > *pStart;
}

// Diferenca simetrica de dois intervalos [a1,a2) e [b1,b2): devolve ate 2
// intervalos em out (pares inicio/fim) e a quantidade.
static int EditorSymDiff(long a1, long a2, long b1, long b2, long out[4])
{
    BOOL aEmpty = a2 <= a1;
    BOOL bEmpty = b2 <= b1;
    int n = 0;

    if (aEmpty && bEmpty)
        return 0;

    if (aEmpty)
    {
        out[0] = b1;
        out[1] = b2;
        return 1;
    }

    if (bEmpty)
    {
        out[0] = a1;
        out[1] = a2;
        return 1;
    }

    if (a2 <= b1 || b2 <= a1)
    {
        out[0] = a1;
        out[1] = a2;
        out[2] = b1;
        out[3] = b2;
        return 2;
    }

    if (a1 != b1)
    {
        out[2 * n] = a1 < b1 ? a1 : b1;
        out[2 * n + 1] = a1 < b1 ? b1 : a1;
        ++n;
    }

    if (a2 != b2)
    {
        out[2 * n] = a2 < b2 ? a2 : b2;
        out[2 * n + 1] = a2 < b2 ? b2 : a2;
        ++n;
    }

    return n;
}

// Pinta [a,b) conforme o estado desejado de cada caractere: linha em destaque
// (fundo + texto ativo) > selecao (texto ativo) > normal. O trecho e cortado nas bordas da
// linha (ls,le) e da selecao (ss,se) para cada pedaco receber uma cor so.
static void EditorPaintSpan(ITextDocument* pTextDoc, long a, long b,
                            long ls, long le, long ss, long se)
{
    long cuts[6];
    long v[4];
    int n = 0;
    int i;
    int j;

    if (b <= a)
        return;

    v[0] = ls;
    v[1] = le;
    v[2] = ss;
    v[3] = se;

    cuts[n++] = a;
    for (i = 0; i < 4; ++i)
    {
        if (v[i] > a && v[i] < b)
            cuts[n++] = v[i];
    }
    cuts[n++] = b;

    for (i = 1; i < n; ++i)
    {
        long key = cuts[i];

        for (j = i - 1; j >= 0 && cuts[j] > key; --j)
            cuts[j + 1] = cuts[j];
        cuts[j + 1] = key;
    }

    for (i = 0; i + 1 < n; ++i)
    {
        ITextRange* pRange = NULL;
        long x = cuts[i];
        long y = cuts[i + 1];

        if (y <= x)
            continue;

        if (SUCCEEDED(pTextDoc->Range(x, y, &pRange)) && pRange != NULL)
        {
            if (x >= ls && x < le)
            {
                EditorPaintLineRange(pRange, TRUE);
            }
            else if (x >= ss && x < se)
            {
                EditorSetTextRangeBackColor(pRange, (long)tomAutoColor);
                EditorPaintSelectionRange(pRange, TRUE);
            }
            else
            {
                EditorPaintLineRange(pRange, FALSE);
            }
            pRange->Release();
        }
    }
}

// Caminho rapido: o texto nao mudou, so a linha em destaque e/ou a selecao.
// Em vez de apagar e repintar a selecao inteira a cada movimento do mouse (o
// que fazia a tela piscar), so os caracteres que entram ou saem da selecao /
// da linha sao repintados, e cada um uma unica vez, ja na cor final.
// (ls,le) = nova linha em destaque (0,0 se desligada); (ss,se) = nova selecao
// (vazia se nao ha). Os ITextRange de rastreamento sao trocados sem pintar.
static void EditorMoveActiveColors(HWND hEdit, Document* pDoc,
                                   long ls, long le, long ss, long se)
{
    ITextDocument* pTextDoc = EditorGetTextDocument(hEdit);
    ITextRange* pRange = NULL;
    long ols = 0, ole = 0, oss = 0, ose = 0;
    long d[4];
    int n;
    int i;
    BOOL hadSel;
    DWORD oldMask;
    BOOL modified;

    if (pTextDoc == NULL)
        return;

    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);
    pTextDoc->Undo(tomSuspend, NULL);

    hadSel = pDoc->pSelectionRange != NULL;
    EditorRangeBounds(pDoc->pHighlightRange, &ols, &ole);
    EditorRangeBounds(pDoc->pSelectionRange, &oss, &ose);

    n = EditorSymDiff(oss, ose, ss, se, d);
    for (i = 0; i < n; ++i)
        EditorPaintSpan(pTextDoc, d[2 * i], d[2 * i + 1], ls, le, ss, se);

    n = EditorSymDiff(ols, ole, ls, le, d);
    for (i = 0; i < n; ++i)
        EditorPaintSpan(pTextDoc, d[2 * i], d[2 * i + 1], ls, le, ss, se);

    if (pDoc->pHighlightRange != NULL)
    {
        pDoc->pHighlightRange->Release();
        pDoc->pHighlightRange = NULL;
    }
    if (pDoc->pSelectionRange != NULL)
    {
        pDoc->pSelectionRange->Release();
        pDoc->pSelectionRange = NULL;
    }

    if (le > ls && SUCCEEDED(pTextDoc->Range(ls, le, &pRange)) && pRange != NULL)
    {
        pDoc->pHighlightRange = pRange;
        pRange = NULL;
    }
    if (se > ss && SUCCEEDED(pTextDoc->Range(ss, se, &pRange)) && pRange != NULL)
    {
        pDoc->pSelectionRange = pRange;
        pRange = NULL;
    }

    // Selecao desfeita: o ponto de insercao nao pode ficar com o formato de
    // "marcado" (ver o passo 5 de EditorUpdateCurrentLineHighlight).
    if (hadSel && se <= ss)
    {
        ITextSelection* pSel = NULL;

        if (SUCCEEDED(pTextDoc->GetSelection(&pSel)) && pSel != NULL)
        {
            EditorPaintLineRange(pSel, le > ls);
            pSel->Release();
        }
    }

    pTextDoc->Undo(tomResume, NULL);
    pTextDoc->Release();

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
}

// Realce lexico universal baseado em look-behind. Tres regras, todas locais:
//   1) palavra imediatamente antes de '(', '[' ou '{'  -> NEGRITO (o que executa/nomeia)
//   2) palavra imediatamente antes de um '=' isolado   -> NEGRITO + ITALICO (o que e definido)
//   3) palavra imediatamente antes de um ':' isolado   -> ITALICO (propriedade/chave, ex.: CSS)
// O trecho [0, applyFrom) serve apenas de contexto para o look-behind e nao
// recebe estilo.

static BOOL EditorIsWordChar(wchar_t ch)
{
    return iswalnum(ch) || ch == L'_' || ch == L'-';
}

// Palavra imediatamente antes de pos. Pula espacos e tabs e, se skipLineBreaks,
// tambem CR/LF. Descarta: nada antes, caractere que nao e de palavra (ex.: ") {"),
// um '-' isolado e numeros puros ou sinalizados ("123", "-45", mas nao "meu-id-123").
// Retorna TRUE com a palavra em [*pStart, *pEnd).
static BOOL EditorFindWordBefore(const std::wstring& text, size_t pos, BOOL skipLineBreaks, int* pStart, int* pEnd)
{
    int idx;
    int wordEnd;
    int wordStart;
    int digitCount = 0;
    BOOL isPureNumber = TRUE;

    if (pos == 0 || pos > text.length())
        return FALSE;

    idx = (int)pos - 1;
    while (idx >= 0 && (text[idx] == L' ' || text[idx] == L'\t' ||
                        (skipLineBreaks && (text[idx] == L'\r' || text[idx] == L'\n'))))
    {
        --idx;
    }

    if (idx < 0)
        return FALSE;

    wordEnd = idx + 1;
    wordStart = idx;
    while (wordStart >= 0 && EditorIsWordChar(text[wordStart]))
        --wordStart;
    ++wordStart;

    // O caractere logo antes nao e de palavra: evita falsos positivos como ") {".
    if (wordStart >= wordEnd)
        return FALSE;

    // Hifen isolado nao e palavra.
    if (wordStart + 1 == wordEnd && text[wordStart] == L'-')
        return FALSE;

    // Trava de numeros: "123" e "-45" sao descartados; "meu-id-123" nao.
    for (int k = wordStart; k < wordEnd; ++k)
    {
        if (iswdigit(text[k]))
            ++digitCount;
        else if (text[k] == L'-' && k == wordStart)
            continue;
        else
        {
            isPureNumber = FALSE;
            break;
        }
    }
    if (isPureNumber && digitCount > 0)
        return FALSE;

    *pStart = wordStart;
    *pEnd = wordEnd;
    return TRUE;
}

// '=' isolado = atribuicao/definicao. Fica de fora tudo em que um vizinho colado
// o transforma em outro operador: ==, ===, !=, !==, <=, >=, <=>, =>, =~, +=, -=,
// *=, /=, %=, &=, |=, ^=, .=, ??=, :=, <<=, >>=. O que vem depois pode ser
// qualquer outro caractere ("x=-1", "x=!y", "x=&y" continuam sendo atribuicao).
static BOOL EditorIsPlainAssign(const std::wstring& text, size_t i)
{
    wchar_t prev = (i > 0) ? text[i - 1] : 0;
    wchar_t next = (i + 1 < text.length()) ? text[i + 1] : 0;

    if (prev != 0 && wcschr(L"=!<>+-*/%&|^~?:.", prev) != NULL)
        return FALSE;
    if (next == L'=' || next == L'>' || next == L'~')
        return FALSE;

    return TRUE;
}

// ':' isolado = separador chave/valor ("color: red", "min-width: 600px", "nome: x").
// Fica de fora: "::" (Foo::bar, a::before), ":=", "?:" (Elvis), ":/" e ":\\"
// (http://, C:\\caminho). Horas como "12:30" caem na trava de numeros.
static BOOL EditorIsPlainColon(const std::wstring& text, size_t i)
{
    wchar_t prev = (i > 0) ? text[i - 1] : 0;
    wchar_t next = (i + 1 < text.length()) ? text[i + 1] : 0;

    if (prev == L':' || prev == L'?')
        return FALSE;
    if (next == L':' || next == L'=' || next == L'/' || next == L'\\')
        return FALSE;

    return TRUE;
}

static void EditorParseAndHighlightRange(ITextDocument* pTextDoc, ITextRange* pWordRange, const std::wstring& text, long startOffset, size_t scanLen, size_t applyFrom)
{
    if (pTextDoc == NULL || pWordRange == NULL || scanLen == 0)
        return;

    for (size_t i = 0; i < scanLen; ++i)
    {
        wchar_t ch = text[i];
        BOOL skipLineBreaks;
        BOOL bold = TRUE;
        BOOL italic = FALSE;
        int wordStart = 0;
        int wordEnd = 0;

        if (i < applyFrom)
            continue;

        if (ch == L'(' || ch == L'[' || ch == L'{')
        {
            skipLineBreaks = TRUE;   // permite "class Foo\n{" e "else\n{"
        }
        else if (ch == L'=' && EditorIsPlainAssign(text, i))
        {
            skipLineBreaks = FALSE;  // "= x" no inicio de linha nao pega a linha anterior
            italic = TRUE;           // definicao: negrito + italico
        }
        else if (ch == L':' && EditorIsPlainColon(text, i))
        {
            skipLineBreaks = FALSE;
            bold = FALSE;            // chave/propriedade: so italico
            italic = TRUE;
        }
        else
        {
            continue;
        }

        if (!EditorFindWordBefore(text, i, skipLineBreaks, &wordStart, &wordEnd))
            continue;

        pWordRange->SetRange(startOffset + (long)wordStart, startOffset + (long)wordEnd);

        ITextFont* pFont = NULL;
        if (SUCCEEDED(pWordRange->GetFont(&pFont)) && pFont != NULL)
        {
            if (bold)
                pFont->SetBold(tomTrue);
            if (italic)
                pFont->SetItalic(tomTrue);
            pFont->Release();
        }
    }
}

// Executa a varredura linear minimalista na linha modificada.
// Coloca em negrito palavras seguidas imediatamente por (, [ ou {, em negrito +
// italico as seguidas por um = isolado e so em italico as seguidas por um : isolado
static void EditorApplyUniversalHighlight(ITextDocument* pTextDoc, long start, long end)
{
    if (pTextDoc == NULL || end <= start)
        return;

    ITextRange* pRange = NULL;
    if (FAILED(pTextDoc->Range(start, end, &pRange)) || pRange == NULL)
        return;

    BSTR bstrText = NULL;
    if (SUCCEEDED(pRange->GetText(&bstrText)) && bstrText != NULL)
    {
        std::wstring text(bstrText);
        
        ITextRange* pWordRange = NULL;
        if (SUCCEEDED(pTextDoc->Range(0, 0, &pWordRange)) && pWordRange != NULL)
        {
            // Executa exatamente a mesma lógica, limitada ao escopo da linha atual
            EditorParseAndHighlightRange(pTextDoc, pWordRange, text, start, text.length(), 0);
            pWordRange->Release();
        }
        SysFreeString(bstrText);
    }
    pRange->Release();
}

// ---------------------------------------------------------------------------
// Highlighting lazy/paginado do arquivo inteiro.
//
// O texto e processado em blocos (paginas) pelo timer da janela principal, em
// vez de varrer o arquivo todo de uma vez e congelar a interface. Cada bloco
// traz um pequeno contexto anterior para o look-behind nao perder palavras que
// comecem antes do limite da pagina.
// ---------------------------------------------------------------------------
#define CODE_HIGHLIGHT_CHUNK      8192
#define CODE_HIGHLIGHT_CONTEXT    1024
#define CODE_HIGHLIGHT_ALIGN_MAX  512
#define CODE_HIGHLIGHT_TIMER_MS   15

static Document* s_pCodeHighlightDoc = NULL;

void EditorCancelUniversalHighlightFor(Document* pDoc)
{
    if (s_pCodeHighlightDoc == NULL)
        return;
    if (pDoc != NULL && s_pCodeHighlightDoc != pDoc)
        return;

    s_pCodeHighlightDoc = NULL;

    if (g_hWndMain != NULL)
        KillTimer(g_hWndMain, NOTBAD_CODE_HIGHLIGHT_TIMER);
}

void EditorCancelUniversalHighlight(void)
{
    EditorCancelUniversalHighlightFor(NULL);
}

void EditorScheduleUniversalHighlight(Document* pDoc)
{
    if (!g_settings.codeHighlighting || pDoc == NULL || pDoc->hEdit == NULL ||
        !IsWindow(pDoc->hEdit) || pDoc->bCodeHighlightDone)
    {
        return;
    }

    s_pCodeHighlightDoc = pDoc;

    if (g_hWndMain != NULL)
        SetTimer(g_hWndMain, NOTBAD_CODE_HIGHLIGHT_TIMER, CODE_HIGHLIGHT_TIMER_MS, NULL);
}

void EditorRestartUniversalHighlight(Document* pDoc, BOOL bClear)
{
    if (pDoc == NULL)
        return;

    EditorCancelUniversalHighlightFor(pDoc);

    pDoc->codeHighlightNext = 0;
    pDoc->bCodeHighlightDone = FALSE;
    pDoc->bCodeHighlightCleared = FALSE;
    pDoc->bCodeHighlightParaValid = FALSE;
    pDoc->bCodeHighlightTextDirty = TRUE;
    pDoc->bCodeHighlightSynced = TRUE;

    if (bClear && pDoc->hEdit != NULL && IsWindow(pDoc->hEdit))
        EditorClearUniversalHighlight(pDoc->hEdit);

    EditorScheduleUniversalHighlight(pDoc);
}

// Reanalisa o estado do realce de um documento que acabou de receber o foco.
// Alternar o highlighting (Ctrl+Shift+K) so processa o documento ativo; os
// demais ficam marcados como nao sincronizados e se ajustam aqui, quando a aba
// volta a ficar visivel:
//  - nao sincronizado: reinicia a varredura. Com o realce desligado limpa o
//    negrito/italico que ficou gravado no texto; ligado, a primeira passada
//    do timer ja limpa o texto antigo antes de realcar.
//  - sincronizado: apenas retoma a varredura lazy, se estava incompleta.
void EditorSyncUniversalHighlight(Document* pDoc)
{
    if (pDoc == NULL || pDoc->hEdit == NULL || !IsWindow(pDoc->hEdit))
        return;

    if (!pDoc->bCodeHighlightSynced)
    {
        EditorRestartUniversalHighlight(pDoc, !g_settings.codeHighlighting);
        return;
    }

    EditorScheduleUniversalHighlight(pDoc);
}

void EditorInvalidateUniversalHighlight(Document* pDoc)
{
    if (!g_settings.codeHighlighting || pDoc == NULL)
        return;

    // A edicao invalida o trecho realcado da linha atual; ele sera limpo e
    // recalculado por EditorUpdateCurrentLineHighlight.
    pDoc->bCodeHighlightTextDirty = TRUE;

    // Depois da primeira passada completa o formato acompanha as edicoes; a
    // linha/paragrafo alterado e recalculado pelo destaque da linha atual.
    if (pDoc->bCodeHighlightDone)
        return;

    // Reinicia a varredura sem limpar o documento inteiro: limpar a cada
    // tecla durante a primeira passada causava tremor e travamento na edicao.
    pDoc->codeHighlightNext = 0;
    EditorScheduleUniversalHighlight(pDoc);
}

// Processa um bloco. Retorna TRUE enquanto ainda houver texto a varrer.
BOOL EditorProcessUniversalHighlightChunk(void)
{
    Document* pDoc = s_pCodeHighlightDoc;
    ITextDocument* pTextDoc;
    ITextRange* pRange = NULL;
    ITextRange* pWordRange = NULL;
    BSTR bstrText = NULL;
    GETTEXTLENGTHEX gtl = { GTL_DEFAULT | GTL_NUMCHARS, 1200 };
    long textLen;
    long start;
    long end;
    long ctxStart;
    DWORD oldMask;
    BOOL modified;

    if (!g_settings.codeHighlighting)
    {
        EditorCancelUniversalHighlight();
        return FALSE;
    }

    if (pDoc == NULL || pDoc->hEdit == NULL || !IsWindow(pDoc->hEdit))
    {
        s_pCodeHighlightDoc = NULL;
        return FALSE;
    }

    if (pDoc->bCodeHighlightDone)
    {
        s_pCodeHighlightDoc = NULL;
        return FALSE;
    }

    textLen = (long)SendMessageW(pDoc->hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);

    if (textLen <= 0)
    {
        s_pCodeHighlightDoc = NULL;
        return FALSE;
    }

    if (pDoc->codeHighlightNext >= textLen)
    {
        pDoc->bCodeHighlightDone = TRUE;
        s_pCodeHighlightDoc = NULL;
        return FALSE;
    }

    if (!pDoc->bCodeHighlightCleared)
    {
        EditorClearUniversalHighlight(pDoc->hEdit);
        pDoc->bCodeHighlightCleared = TRUE;
    }

    start = pDoc->codeHighlightNext;
    if (start < 0)
        start = 0;

    end = start + CODE_HIGHLIGHT_CHUNK;
    if (end > textLen)
        end = textLen;

    ctxStart = start - CODE_HIGHLIGHT_CONTEXT;
    if (ctxStart < 0)
        ctxStart = 0;

    pTextDoc = EditorGetTextDocument(pDoc->hEdit);
    if (pTextDoc == NULL)
    {
        s_pCodeHighlightDoc = NULL;
        return FALSE;
    }

    oldMask = (DWORD)SendMessageW(pDoc->hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0);
    pTextDoc->Undo(tomSuspend, NULL);

    if (SUCCEEDED(pTextDoc->Range(ctxStart, end, &pRange)) && pRange != NULL &&
        SUCCEEDED(pRange->GetText(&bstrText)) && bstrText != NULL)
    {
        std::wstring text(bstrText);
        size_t applyFrom = (size_t)(start - ctxStart);
        size_t scanLen = text.length();

        // Recua o fim do bloco ate a ultima separacao (espaco/quebra/tab)
        // dentro de um limite, para nao cortar um token no meio. Assim o
        // proximo bloco sempre comeca em uma fronteira limpa.
        if (end < textLen && scanLen > applyFrom)
        {
            size_t keep = scanLen;
            size_t minKeep = (keep > CODE_HIGHLIGHT_ALIGN_MAX)
                                 ? keep - CODE_HIGHLIGHT_ALIGN_MAX : 0;

            if (minKeep < applyFrom)
                minKeep = applyFrom;

            while (keep > minKeep && !iswspace(text[keep - 1]))
                --keep;

            if (keep > applyFrom && keep < scanLen && iswspace(text[keep - 1]))
            {
                long alignedEnd = ctxStart + (long)keep;
                if (alignedEnd > start)
                    end = alignedEnd;
            }
        }

        scanLen = (size_t)(end - ctxStart);
        if (scanLen > text.length())
            scanLen = text.length();

        if (scanLen > applyFrom)
        {
            if (SUCCEEDED(pTextDoc->Range(0, 0, &pWordRange)) && pWordRange != NULL)
            {
                EditorParseAndHighlightRange(pTextDoc, pWordRange, text, ctxStart,
                                             scanLen, applyFrom);
                pWordRange->Release();
            }
        }
    }

    if (bstrText != NULL)
        SysFreeString(bstrText);
    if (pRange != NULL)
        pRange->Release();

    pTextDoc->Undo(tomResume, NULL);
    pTextDoc->Release();

    SendMessageW(pDoc->hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(pDoc->hEdit, EM_SETEVENTMASK, 0, (LPARAM)oldMask);

    pDoc->codeHighlightNext = end;

    if (end >= textLen)
    {
        pDoc->bCodeHighlightDone = TRUE;
        s_pCodeHighlightDoc = NULL;
        return FALSE;
    }

    return TRUE;
}

// Zera negrito/italico do formato de quem digita. Formatar um intervalo nao
// atualiza o formato em cache do ponto de insercao: sem isto, o texto digitado
// depois de desligar o highlighting (ou de limpar o realce) herdaria o estilo
// da palavra onde o cursor estava. Em selecao nao vazia, apenas repete a limpeza.
static void EditorClearSelectionStyle(ITextDocument* pTextDoc)
{
    ITextSelection* pSel = NULL;
    ITextFont* pFont = NULL;

    if (pTextDoc == NULL)
        return;

    if (SUCCEEDED(pTextDoc->GetSelection(&pSel)) && pSel != NULL)
    {
        if (SUCCEEDED(pSel->GetFont(&pFont)) && pFont != NULL)
        {
            pFont->SetBold(tomFalse);
            pFont->SetItalic(tomFalse);
            pFont->Release();
        }
        pSel->Release();
    }
}

// Remove todo o negrito e italico aplicados pelo highlighting de codigo, sem entrar no
// historico de desfazer nem marcar o documento como modificado.
void EditorClearUniversalHighlight(HWND hEdit)
{
    Document* pDoc = DocFromEdit(hEdit);
    ITextDocument* pTextDoc = EditorGetTextDocument(hEdit);
    GETTEXTLENGTHEX gtl = { GTL_DEFAULT | GTL_NUMCHARS, 1200 };
    ITextRange* pWholeRange = NULL;
    long textLen;
    DWORD oldMask;
    BOOL modified;
    // WM_SETREDRAW(TRUE) em uma janela oculta faz o DefWindowProc ligar o
    // WS_VISIBLE: o RichEdit de uma aba inativa reapareceria sobre o ativo.
    // Em editor oculto so formatamos o texto, sem mexer em redraw/visibilidade.
    BOOL bVisible = (GetWindowLongPtrW(hEdit, GWL_STYLE) & WS_VISIBLE) != 0;

    if (pDoc != NULL)
    {
        pDoc->bCodeHighlightParaValid = FALSE;
        pDoc->bCodeHighlightTextDirty = TRUE;
    }

    if (pTextDoc == NULL)
        return;

    textLen = (long)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (textLen <= 0)
    {
        // Nao ha texto a limpar, mas o cursor pode ter ficado com negrito/italico.
        oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
        modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);
        pTextDoc->Undo(tomSuspend, NULL);
        EditorClearSelectionStyle(pTextDoc);
        pTextDoc->Undo(tomResume, NULL);
        SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, (LPARAM)oldMask);
        pTextDoc->Release();
        return;
    }

    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);
    if (bVisible)
        SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    pTextDoc->Undo(tomSuspend, NULL);

    if (SUCCEEDED(pTextDoc->Range(0, textLen, &pWholeRange)) && pWholeRange != NULL)
    {
        ITextFont* pWholeFont = NULL;

        if (SUCCEEDED(pWholeRange->GetFont(&pWholeFont)) && pWholeFont != NULL)
        {
            pWholeFont->SetBold(tomFalse);
            pWholeFont->SetItalic(tomFalse);
            pWholeFont->Release();
        }
        pWholeRange->Release();
    }

    // O cursor tambem precisa perder o estilo, ou o proximo texto digitado o herda.
    EditorClearSelectionStyle(pTextDoc);

    pTextDoc->Undo(tomResume, NULL);
    pTextDoc->Release();

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, (LPARAM)oldMask);
    if (bVisible)
    {
        SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }
}

// Atualiza a linha em destaque (fundo NOTBAD_HIGHLIGHT_COLOR) e a cor do texto
// ativo: a linha em destaque e a selecao ficam em NOTBAD_ACTIVE_TEXT_COLOR; o
// resto do texto fica em NOTBAD_TEXT_COLOR. O RichEdit nao tem cor propria para
// "texto selecionado", entao a cor e aplicada no trecho (ITextRange, como o
// fundo do destaque) e devolvida ao cinza quando a selecao ou a linha mudam.
void EditorUpdateCurrentLineHighlight(HWND hEdit)
{
    Document* pDoc = DocFromEdit(hEdit);
    ITextDocument* pTextDoc;
    ITextRange* pRange = NULL;
    GETTEXTLENGTHEX gtl = {};
    int selStart;
    int selEnd;
    int caret;
    int para;
    int start;
    int nextStart;
    int end;
    int textLen;
    int storyEnd;
    int insLen;
    int lineCaret;
    int prevSelLen;
    BOOL needInsFix;
    BOOL grayedSel = FALSE;
    BOOL grayedLine = FALSE;
    int prevTextLen;
    BOOL wantLine;
    BOOL wantSel;
    BOOL lineSame;
    BOOL selSame;
    BOOL insInside;
    BOOL codeDirty;
    long oldStart = 0;
    long oldEnd = 0;
    DWORD oldMask;
    BOOL modified;

    if (hEdit == NULL || pDoc == NULL || s_bUpdatingHighlight)
        return;

    s_bUpdatingHighlight = TRUE;

    GetEditSelection(hEdit, &selStart, &selEnd);
    caret = selEnd;
    lineCaret = EditorGetActiveEnd(hEdit, selStart, selEnd);

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;
    textLen = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (textLen < 0)
        textLen = 0;

    EditorUpdateIndentBlock(hEdit, lineCaret, textLen);

    para = EditorGetParagraphNumber(hEdit, lineCaret);
    start = EditorGetParagraphStart(hEdit, lineCaret);
    nextStart = EditorGetParagraphStartByNumber(hEdit, para + 1);

    storyEnd = textLen + 1;
    end = nextStart > start ? nextStart : storyEnd;
    if (end > storyEnd)
        end = storyEnd;
    if (end <= start)
        end = start + 1;

    wantLine = g_settings.highlightCurrentLine;
    wantSel = selEnd > selStart;

    lineSame = wantLine ? FALSE : (pDoc->pHighlightRange == NULL);
    if (wantLine && pDoc->pHighlightRange != NULL &&
        SUCCEEDED(pDoc->pHighlightRange->GetStart(&oldStart)) &&
        SUCCEEDED(pDoc->pHighlightRange->GetEnd(&oldEnd)) &&
        oldStart == start && oldEnd == end)
    {
        lineSame = TRUE;
    }

    selSame = wantSel ? FALSE : (pDoc->pSelectionRange == NULL);
    if (wantSel && pDoc->pSelectionRange != NULL &&
        SUCCEEDED(pDoc->pSelectionRange->GetStart(&oldStart)) &&
        SUCCEEDED(pDoc->pSelectionRange->GetEnd(&oldEnd)) &&
        oldStart == selStart && oldEnd == selEnd)
    {
        selSame = TRUE;
    }

    insLen = 0;
    if (pDoc->fgPrevTextLen >= 0)
        insLen = (textLen - pDoc->fgPrevTextLen) + pDoc->fgPrevSelLen;
    if (insLen < 0)
        insLen = 0;
    if (insLen > caret)
        insLen = caret;

    insInside = wantLine && lineSame && pDoc->pHighlightRange != NULL &&
                (caret - insLen) >= start && caret <= end;

    prevSelLen = pDoc->fgPrevSelLen;
    prevTextLen = pDoc->fgPrevTextLen;
    pDoc->fgPrevTextLen = textLen;
    pDoc->fgPrevSelLen = wantSel ? (selEnd - selStart) : 0;

    needInsFix = insLen > 0;
    (void)prevSelLen;

    codeDirty = g_settings.codeHighlighting && pDoc->bCodeHighlightTextDirty;

    if (lineSame && selSame && !needInsFix)
    {
        s_bUpdatingHighlight = FALSE;
        return;
    }

    if (!needInsFix && prevTextLen >= 0 && prevTextLen == textLen)
    {
        EditorMoveActiveColors(hEdit, pDoc,
                               wantLine ? start : 0, wantLine ? end : 0,
                               wantSel ? selStart : 0, wantSel ? selEnd : 0);
        s_bUpdatingHighlight = FALSE;
        return;
    }

    pTextDoc = EditorGetTextDocument(hEdit);
    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);

    if (pTextDoc != NULL)
        pTextDoc->Undo(tomSuspend, NULL);

    if (!selSame && pDoc->pSelectionRange != NULL)
    {
        EditorPaintSelectionRange(pDoc->pSelectionRange, FALSE);
        pDoc->pSelectionRange->Release();
        pDoc->pSelectionRange = NULL;
        grayedSel = TRUE;
    }

    if (!lineSame && pDoc->pHighlightRange != NULL)
    {
        EditorPaintLineRange(pDoc->pHighlightRange, FALSE);
        pDoc->pHighlightRange->Release();
        pDoc->pHighlightRange = NULL;
        grayedLine = TRUE;
    }

    if (needInsFix && pTextDoc != NULL)
    {
        if (SUCCEEDED(pTextDoc->Range(caret - insLen, caret, &pRange)) && pRange != NULL)
        {
            EditorPaintLineRange(pRange, insInside);
            pRange->Release();
            pRange = NULL;
        }
    }

    if (lineSame && (grayedSel || needInsFix))
        EditorPaintLineRange(pDoc->pHighlightRange, TRUE);
    if (selSame && (grayedLine || needInsFix))
        EditorPaintSelectionRange(pDoc->pSelectionRange, TRUE);

    if (!lineSame && wantLine && pTextDoc != NULL && end > start)
    {
        if (SUCCEEDED(pTextDoc->Range(start, end, &pRange)) && pRange != NULL)
        {
            EditorPaintLineRange(pRange, TRUE);
            pDoc->pHighlightRange = pRange;
            pRange = NULL;
        }
    }

    // ===================================================================
    // HIGH LIGHTING UNIVERSAL MINIMALISTA (LINHA ATUAL)
    // ===================================================================
    if (g_settings.codeHighlighting && pTextDoc != NULL && end > start)
    {
        int hlStart = start;
        BOOL samePara;
        BOOL covered = FALSE;

        // Ao editar, o paragrafo anterior tambem pode ter mudado (Enter divide
        // a linha; apagar um Enter junta duas). Reaplica os dois. Usar o
        // caractere anterior evita depender da numeracao 0/1-based do TOM.
        if (codeDirty && start > 0)
        {
            int prevStart = EditorGetParagraphStart(hEdit, start - 1);
            if (prevStart >= 0 && prevStart < hlStart)
                hlStart = prevStart;
        }

        samePara = pDoc->bCodeHighlightParaValid &&
                   pDoc->codeHighlightParaStart == hlStart &&
                   pDoc->codeHighlightParaEnd == end;

        if (!codeDirty)
        {
            // Sem edicao: se a varredura lazy ja cobriu o trecho, ou se ele
            // acabou de ser realcado, nao precisa limpar/reaplicar de novo.
            if (pDoc->bCodeHighlightDone)
                covered = TRUE;
            else if (pDoc->codeHighlightNext > start)
                covered = TRUE;
            else if (samePara)
                covered = TRUE;
        }

        if (!covered)
        {
            // Limpa negrito e italico anteriores do bloco para recalcular
            ITextRange* pCleanRange = NULL;
            if (SUCCEEDED(pTextDoc->Range(hlStart, end, &pCleanRange)) && pCleanRange != NULL)
            {
                ITextFont* pFont = NULL;
                if (SUCCEEDED(pCleanRange->GetFont(&pFont)) && pFont != NULL)
                {
                    pFont->SetBold(tomFalse);
                    pFont->SetItalic(tomFalse);
                    pFont->Release();
                }
                pCleanRange->Release();
            }

            // Aplica o Highlighting inteligente no trecho
            EditorApplyUniversalHighlight(pTextDoc, hlStart, end);

            pDoc->codeHighlightParaStart = hlStart;
            pDoc->codeHighlightParaEnd = end;
            pDoc->bCodeHighlightParaValid = TRUE;
            pDoc->bCodeHighlightTextDirty = FALSE;
        }
    }
    // ===================================================================

    if (!selSame && wantSel && pTextDoc != NULL)
    {
        if (SUCCEEDED(pTextDoc->Range(selStart, selEnd, &pRange)) && pRange != NULL)
        {
            EditorPaintSelectionRange(pRange, TRUE);
            pDoc->pSelectionRange = pRange;
            pRange = NULL;
        }
    }

    if (!wantSel && (needInsFix || !selSame) && pTextDoc != NULL)
    {
        ITextSelection* pSel = NULL;

        if (SUCCEEDED(pTextDoc->GetSelection(&pSel)) && pSel != NULL)
        {
            EditorPaintLineRange(pSel, wantLine);
            pSel->Release();
        }
    }

    if (pTextDoc != NULL)
    {
        pTextDoc->Undo(tomResume, NULL);
        pTextDoc->Release();
    }

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
    s_bUpdatingHighlight = FALSE;
}


void EditorApplyCurrentLineHighlight(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
        EditorUpdateCurrentLineHighlight(g_documents[i]->hEdit);
}

// Substitui um trecho sem pintar a selecao intermediaria. Sem isso, o
// Delete/Backspace de indentacao pisca marcando os caracteres antes de apagar.
static void EditorReplaceSilently(HWND hEdit, int start, int end, LPCWSTR lpText)
{
    CHARRANGE cr = { start, end };

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)lpText);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

// Texto interno do RichEdit (quebras so com CR), alinhado aos indices de
// EM_EXGETSEL/EM_EXSETSEL. Quem chama libera com LocalFree.
static LPWSTR EditorGetText(HWND hEdit, int* pcch)
{
    GETTEXTLENGTHEX gtl = {};
    GETTEXTEX gt = {};
    LPWSTR lpText;
    int length;
    int copied;

    *pcch = 0;

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;    // CP_UTF16
    length = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (length < 0)
        length = 0;

    lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpText == NULL)
        return NULL;

    gt.cb = (DWORD)(((SIZE_T)length + 1) * sizeof(WCHAR));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    copied = (int)SendMessageW(hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)lpText);
    if (copied < 0 || copied > length)
        copied = length;
    lpText[copied] = L'\0';

    *pcch = copied;
    return lpText;
}

// Tab / Shift+Tab em bloco. As linhas sao paragrafos logicos (nao as linhas
// visuais do word wrap) e o bloco inteiro e trocado por uma unica
// substituicao: um unico passo de undo e sem piscar.
static BOOL EditorIndentBlock(HWND hEdit, BOOL shift)
{
    LPWSTR lpText;
    int cchText;
    int start;
    int end;
    int effEnd;
    int rangeStart;
    int rangeEnd;
    int tabWidth = GetTabWidth();
    WCHAR szUnit[64];
    int unitLength;
    std::vector<int> lineStarts;
    std::vector<int> removes;
    std::wstring block;
    int pos;
    int newStart;
    int newEnd;
    DWORD oldMask;
    size_t i;

    unitLength = BuildIndentString(tabWidth, tabWidth, g_settings.indentWithTabs,
                                   szUnit, ARRAYSIZE(szUnit));

    lpText = EditorGetText(hEdit, &cchText);
    if (lpText == NULL)
        return TRUE;

    GetEditSelection(hEdit, &start, &end);
    if (start < 0)
        start = 0;
    if (end > cchText)
        end = cchText;
    if (start > end)
        start = end;

    // Selecao que termina no inicio de uma linha nao inclui essa linha.
    effEnd = end;
    if (end > start && end > 0 && lpText[end - 1] == L'\r')
        --effEnd;

    rangeStart = start;
    while (rangeStart > 0 && lpText[rangeStart - 1] != L'\r')
        --rangeStart;

    rangeEnd = effEnd;
    while (rangeEnd < cchText && lpText[rangeEnd] != L'\r')
        ++rangeEnd;

    pos = rangeStart;
    for (;;)
    {
        int lineEnd = pos;
        int remove = 0;

        while (lineEnd < rangeEnd && lpText[lineEnd] != L'\r')
            ++lineEnd;

        lineStarts.push_back(pos);

        if (!shift)
        {
            block.append(szUnit, (size_t)unitLength);
        }
        else
        {
            if (pos < lineEnd && lpText[pos] == L'\t')
            {
                remove = 1;
            }
            else
            {
                while (remove < tabWidth && pos + remove < lineEnd &&
                       lpText[pos + remove] == L' ')
                    ++remove;
            }
        }

        removes.push_back(remove);
        block.append(lpText + pos + remove, (size_t)(lineEnd - pos - remove));

        if (lineEnd >= rangeEnd)
            break;

        block.push_back(L'\r');
        pos = lineEnd + 1;
    }

    // Selecao resultante.
    if (!shift)
    {
        newStart = start + unitLength;
        newEnd = end + unitLength * (int)lineStarts.size();
    }
    else
    {
        // Cada ponta recua so o que foi removido ANTES dela (nunca alem do
        // inicio da propria linha), senao a selecao vaza para a linha de cima.
        newStart = start;
        newEnd = end;
        for (i = 0; i < lineStarts.size(); ++i)
        {
            int before = start - lineStarts[i];

            if (before < 0)
                before = 0;
            if (before > removes[i])
                before = removes[i];
            newStart -= before;

            before = end - lineStarts[i];
            if (before < 0)
                before = 0;
            if (before > removes[i])
                before = removes[i];
            newEnd -= before;
        }

        if (newStart < 0)
            newStart = 0;
        if (newEnd < newStart)
            newEnd = newStart;
    }

    LocalFree(lpText);

    {
        BOOL changed = FALSE;

        for (i = 0; i < removes.size(); ++i)
        {
            if (!shift || removes[i] > 0)
            {
                changed = TRUE;
                break;
            }
        }

        // Shift+Tab sem nada para remover: so preserva a selecao.
        if (!changed)
            return TRUE;
    }

    oldMask = EditorBeginBatch(hEdit);

    SendMessageW(hEdit, EM_SETSEL, rangeStart, rangeEnd);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)block.c_str());
    SendMessageW(hEdit, EM_SETSEL, newStart, newEnd);

    EditorEndBatch(hEdit, oldMask);

    if (DocFromEdit(hEdit) != NULL)
        DocUpdateTabTitle(DocFromEdit(hEdit));
    UpdateWindowTitle();
    InvalidateLineNumbers();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    return TRUE;
}

// Backspace com auto-indent ativo: dentro da indentacao (so espacos/tabs entre
// o inicio da linha e o cursor) recua um nivel inteiro: um Tab, ou os espacos
// ate o multiplo anterior da largura do tab. Fora disso, Backspace normal.
static BOOL EditorHandleBackspace(HWND hEdit)
{
    WCHAR szBefore[512] = {};
    TEXTRANGEW range = {};
    int start;
    int end;
    int paraStart;
    int count;
    int tabWidth;
    int remove;

    if (!g_settings.autoIndent)
        return FALSE;

    if ((GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU)) & 0x8000)
        return FALSE;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start != end || start <= 0)
        return FALSE;

    paraStart = EditorGetParagraphStart(hEdit, start);
    count = start - paraStart;
    if (count <= 0 || count >= (int)ARRAYSIZE(szBefore))
        return FALSE;

    range.chrg.cpMin = paraStart;
    range.chrg.cpMax = start;
    range.lpstrText = szBefore;
    if ((int)SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range) != count)
        return FALSE;

    for (int i = 0; i < count; ++i)
    {
        if (szBefore[i] != L' ' && szBefore[i] != L'\t')
            return FALSE;
    }

    if (szBefore[count - 1] == L'\t')
    {
        remove = 1;
    }
    else
    {
        int spaces = 0;

        while (spaces < count && szBefore[count - 1 - spaces] == L' ')
            ++spaces;

        tabWidth = GetTabWidth();
        remove = spaces % tabWidth;
        if (remove == 0)
            remove = tabWidth;
        if (remove > spaces)
            remove = spaces;
    }

    // O WM_CHAR(Backspace) ja foi enfileirado pelo TranslateMessage.
    RemovePendingChar(hEdit, L'\b');

    EditorReplaceSilently(hEdit, start - remove, start, L"");
    InvalidateLineNumbers();
    UpdateStatusBar();
    return TRUE;
}

// Tab sem selecao: insere a indentacao no estilo configurado (espacos por
// padrao), avancando ate o proximo tab stop visual.
static BOOL EditorHandleTabAtCaret(HWND hEdit)
{
    WCHAR szBefore[4096];
    WCHAR szSpaces[64];
    TEXTRANGEW range = {};
    int start;
    int end;
    int paraStart;
    int length;
    int spaces;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    RemovePendingChar(hEdit, L'\t');

    if (g_settings.indentWithTabs)
    {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\t");
        InvalidateLineNumbers();
        UpdateStatusBar();
        return TRUE;
    }

    GetEditSelection(hEdit, &start, &end);
    paraStart = EditorGetParagraphStart(hEdit, start);
    length = start - paraStart;
    if (length <= 0)
    {
        spaces = GetTabWidth();
    }
    else
    {
        if (length > (int)ARRAYSIZE(szBefore) - 1)
            length = (int)ARRAYSIZE(szBefore) - 1;

        range.chrg.cpMin = paraStart;
        range.chrg.cpMax = paraStart + length;
        range.lpstrText = szBefore;
        SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);
        szBefore[length] = L'\0';

        spaces = GetTabWidth() - (GetIndentColumns(szBefore, length, GetTabWidth()) %
                                 GetTabWidth());
    }

    if (spaces < 1)
        spaces = GetTabWidth();
    if (spaces > (int)ARRAYSIZE(szSpaces) - 1)
        spaces = (int)ARRAYSIZE(szSpaces) - 1;

    for (int i = 0; i < spaces; ++i)
        szSpaces[i] = L' ';
    szSpaces[spaces] = L'\0';

    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)szSpaces);
    InvalidateLineNumbers();
    UpdateStatusBar();
    return TRUE;
}

// Delete para frente dentro da indentacao feita com espacos: consome os
// espacos ate o proximo nivel, como o Delete faz com um tab. So vale quando
// o auto-indent esta ligado e o estilo configurado e espacos.
static int GetForwardIndentDeleteCount(HWND hEdit, int start)
{
    WCHAR szPrefix[512];
    TEXTRANGEW range = {};
    int paraStart;
    int column;
    int remove;
    int spaces;
    int tabWidth = GetTabWidth();

    if (!g_settings.autoIndent || g_settings.indentWithTabs)
        return 0;

    paraStart = EditorGetParagraphStart(hEdit, start);
    column = start - paraStart;
    if (column < 0 || column >= (int)ARRAYSIZE(szPrefix))
        return 0;

    if (column > 0)
    {
        int i;

        range.chrg.cpMin = paraStart;
        range.chrg.cpMax = start;
        range.lpstrText = szPrefix;
        if ((int)SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range) != column)
            return 0;

        for (i = 0; i < column; ++i)
        {
            if (szPrefix[i] != L' ')
                return 0;
        }
    }

    remove = tabWidth - (column % tabWidth);
    if (remove <= 0)
        remove = tabWidth;

    spaces = 0;
    while (spaces < remove && GetEditCharAt(hEdit, start + spaces) == L' ')
        ++spaces;

    return spaces;
}

// Delete explicito: o RichEdit deixa de apagar o caractere a direita quando
// a tecla e consumida por outros caminhos. Aqui a acao e garantida tanto
// para selecao quanto para o caractere sob o cursor.
static BOOL EditorHandleDelete(HWND hEdit)
{
    int start;
    int end;
    int length;
    int deleteEnd;
    int indentDelete;
    WCHAR ch;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start < 0)
        start = 0;

    if (start == end)
    {
        indentDelete = GetForwardIndentDeleteCount(hEdit, start);
        if (indentDelete > 0)
        {
            deleteEnd = start + indentDelete;
        }
        else
        {
            GETTEXTLENGTHEX gtl = {};
            gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
            gtl.codepage = 1200;
            length = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
            if (length <= start)
                return TRUE;

            deleteEnd = start + 1;
            ch = GetEditCharAt(hEdit, start);
            if (ch >= 0xD800 && ch <= 0xDBFF)
            {
                WCHAR next = GetEditCharAt(hEdit, start + 1);
                if (next >= 0xDC00 && next <= 0xDFFF)
                    deleteEnd = start + 2;
            }
        }
    }
    else
    {
        deleteEnd = end;
    }

    EditorReplaceSilently(hEdit, start, deleteEnd, L"");
    InvalidateLineNumbers();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    return TRUE;
}

BOOL EditorHandleKeyDown(HWND hEdit, WPARAM wParam)
{
    BOOL shift;
    BOOL ctrl;
    BOOL alt;
    int start;
    int end;

    if (hEdit == NULL)
        return FALSE;

    if (wParam == VK_RETURN)
        return EditorHandleEnter(hEdit);

    if (wParam == VK_HOME)
        return EditorHandleHome(hEdit);

    if (wParam == VK_BACK)
        return EditorHandleBackspace(hEdit);

    if (wParam == VK_DELETE)
    {
        ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // Deixa Ctrl+Delete/Alt+Delete para o RichEdit (apagar palavra etc.).
        if (ctrl || alt)
            return FALSE;

        // Shift+Delete e o atalho padrao de recortar: precisa copiar para a
        // area de transferencia antes de remover a selecao.
        if (shift)
        {
            GetEditSelection(hEdit, &start, &end);
            if (start == end)
                return TRUE;

            SendMessageW(hEdit, WM_CUT, 0, 0);
            InvalidateLineNumbers();
            UpdateStatusBar();
            UpdateEditMenuState(g_hWndMain);
            return TRUE;
        }

        return EditorHandleDelete(hEdit);
    }

    if (wParam == VK_INSERT)
    {
        ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (alt)
            return FALSE;

        // Shift+Insert cola e Ctrl+Insert copia (atalhos padrao de teclado).
        if (shift && !ctrl)
        {
            SendMessageW(hEdit, WM_PASTE, 0, 0);
            InvalidateLineNumbers();
            UpdateStatusBar();
            UpdateEditMenuState(g_hWndMain);
            return TRUE;
        }

        if (ctrl && !shift)
        {
            SendMessageW(hEdit, WM_COPY, 0, 0);
            UpdateEditMenuState(g_hWndMain);
            return TRUE;
        }

        // Insert sozinho alterna Insert/Overwrite e troca a forma do caret.
        if (!shift && !ctrl)
        {
            Document* pDoc = DocFromEdit(hEdit);

            if (pDoc != NULL)
            {
                pDoc->bOverwrite = !pDoc->bOverwrite;
                EditorUpdateCaretShape(hEdit);
            }
            return TRUE;
        }

        return FALSE;
    }

    if (wParam != VK_TAB)
        return FALSE;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    GetEditSelection(hEdit, &start, &end);

    if (!shift && start == end)
        return EditorHandleTabAtCaret(hEdit);

    // Sem remover o WM_CHAR('\t') o RichEdit trocaria a selecao por um Tab
    // logo depois do nosso tratamento.
    RemovePendingChar(hEdit, L'\t');

    return EditorIndentBlock(hEdit, shift);
}

void EditorConvertTabs(BOOL bTabsToSpaces)
{
    int length;
    int tabWidth;
    int i;
    int o = 0;
    LPWSTR lpSrc;
    LPWSTR lpDst;
    SIZE_T capacity;
    int selStart;
    int selEnd;

    if (g_hWndEdit == NULL)
        return;

    {
        // Contagem em caracteres, com quebras de linha como o RichEdit as
        // guarda (CR), igual ao que EM_GETTEXTEX devolve abaixo.
        GETTEXTLENGTHEX gtl = {};
        gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
        gtl.codepage = 1200;    // CP_UTF16
        length = (int)SendMessageW(g_hWndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    }
    if (length <= 0)
        return;

    tabWidth = GetTabWidth();
    capacity = (SIZE_T)length + 1;
    if (bTabsToSpaces)
    {
        if ((SIZE_T)length > (SIZE_T)(-1) / (SIZE_T)tabWidth)
            return;
        capacity = (SIZE_T)length * (SIZE_T)tabWidth + 1;
    }

    lpSrc = (LPWSTR)LocalAlloc(LMEM_FIXED, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpSrc == NULL)
        return;

    lpDst = (LPWSTR)LocalAlloc(LMEM_FIXED, capacity * sizeof(WCHAR));
    if (lpDst == NULL)
    {
        LocalFree(lpSrc);
        return;
    }

    {
        GETTEXTEX gt = {};
        gt.cb = (DWORD)(((SIZE_T)length + 1) * sizeof(WCHAR));
        gt.flags = GT_DEFAULT;
        gt.codepage = 1200;     // CP_UTF16
        length = (int)SendMessageW(g_hWndEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)lpSrc);
        lpSrc[length] = L'\0';
    }

    i = 0;
    while (i < length)
    {
        int indentEnd = i;

        while (indentEnd < length &&
               (lpSrc[indentEnd] == L' ' || lpSrc[indentEnd] == L'\t'))
        {
            ++indentEnd;
        }

        if (bTabsToSpaces)
        {
            int k;
            for (k = i; k < indentEnd; ++k)
            {
                if (lpSrc[k] == L'\t')
                {
                    int s;
                    for (s = 0; s < tabWidth; ++s)
                        lpDst[o++] = L' ';
                }
                else
                {
                    lpDst[o++] = lpSrc[k];
                }
            }
        }
        else
        {
            int k = i;
            while (k < indentEnd)
            {
                if (lpSrc[k] == L'\t')
                {
                    lpDst[o++] = L'\t';
                    ++k;
                }
                else
                {
                    int run = 0;
                    int tabs;
                    int rem;
                    int s;
                    while (k + run < indentEnd && lpSrc[k + run] == L' ')
                        ++run;
                    tabs = run / tabWidth;
                    rem = run % tabWidth;
                    for (s = 0; s < tabs; ++s)
                        lpDst[o++] = L'\t';
                    for (s = 0; s < rem; ++s)
                        lpDst[o++] = L' ';
                    k += run;
                }
            }
        }

        i = indentEnd;

        while (i < length && lpSrc[i] != L'\r' && lpSrc[i] != L'\n')
            lpDst[o++] = lpSrc[i++];

        if (i < length && lpSrc[i] == L'\r')
        {
            lpDst[o++] = lpSrc[i++];
            if (i < length && lpSrc[i] == L'\n')
                lpDst[o++] = lpSrc[i++];
        }
        else if (i < length && lpSrc[i] == L'\n')
        {
            lpDst[o++] = lpSrc[i++];
        }
    }

    lpDst[o] = L'\0';

    // Nada a converter: nao substituir o texto (evita undo/modificado a toa)
    // e avisar, para a pessoa saber que o comando rodou.
    if (o == length && memcmp(lpSrc, lpDst, (size_t)o * sizeof(WCHAR)) == 0)
    {
        LocalFree(lpSrc);
        LocalFree(lpDst);
        MessageBoxW(g_hWndMain,
                    bTabsToSpaces ? L"There are no tabs in the indentation of this tab."
                                  : L"There are no runs of spaces long enough to convert to tabs.",
                    NOTBAD_NAME, MB_OK | MB_ICONINFORMATION);
        return;
    }

    GetEditSelection(g_hWndEdit, &selStart, &selEnd);

    // Troca unica, sem notificacoes nem repintura intermediaria, mantendo a
    // posicao de rolagem (um passo de undo e sem piscar).
    int firstVisible = (int)SendMessageW(g_hWndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    DWORD oldMask = EditorBeginBatch(g_hWndEdit);

    SendMessageW(g_hWndEdit, EM_SETSEL, 0, -1);
    SendMessageW(g_hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)lpDst);

    int newStart = selStart;
    int newEnd = selEnd;
    if (newStart > o)
        newStart = o;
    if (newEnd > o)
        newEnd = o;
    SendMessageW(g_hWndEdit, EM_SETSEL, newStart, newEnd);

    {
        int nowVisible = (int)SendMessageW(g_hWndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
        if (nowVisible != firstVisible)
            SendMessageW(g_hWndEdit, EM_LINESCROLL, 0, firstVisible - nowVisible);
    }

    EditorEndBatch(g_hWndEdit, oldMask);

    LocalFree(lpSrc);
    LocalFree(lpDst);

    if (g_pActiveDoc != NULL)
        DocUpdateTabTitle(g_pActiveDoc);
    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    InvalidateLineNumbers();
}
