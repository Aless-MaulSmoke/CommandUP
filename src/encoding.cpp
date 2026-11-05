#include "notbad.h"

#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0x00000080
#endif
#ifndef WC_NO_BEST_FIT_CHARS
#define WC_NO_BEST_FIT_CHARS 0x00000400
#endif

// Pagina de codigo fixa do "ANSI": o menu promete Windows-1252, entao o
// arquivo deve ser lido e gravado igual em qualquer maquina, e nao pela
// pagina ANSI configurada no sistema (CP_ACP).
#define NOTBAD_CP_ANSI 1252

static UINT CodePageFor(TextEncoding encoding)
{
    switch (encoding)
    {
    case ENC_UTF8:
    case ENC_UTF8_BOM:
        return CP_UTF8;
    case ENC_OEM:
        return CP_OEMCP;
    case ENC_GB18030:
        return 54936;
    default:
        return NOTBAD_CP_ANSI;
    }
}

static BOOL ReadAllBytes(LPCWSTR lpFileName, BYTE **ppData, DWORD *pcbData)
{
    HANDLE hFile;
    LARGE_INTEGER size;
    BYTE *pData = NULL;
    DWORD total = 0;

    *ppData = NULL;
    *pcbData = 0;

    hFile = CreateFileW(lpFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(hFile, &size) || size.QuadPart > MAX_EDIT_BUFFER)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    if (size.QuadPart > 0)
    {
        pData = (BYTE *)LocalAlloc(LMEM_FIXED, (SIZE_T)size.QuadPart + 1);
        if (pData == NULL)
        {
            CloseHandle(hFile);
            return FALSE;
        }

        while (total < (DWORD)size.QuadPart)
        {
            DWORD read = 0;
            if (!ReadFile(hFile, pData + total, (DWORD)size.QuadPart - total, &read, NULL) || read == 0)
                break;
            total += read;
        }

        pData[total] = 0;
    }
    else
    {
        pData = (BYTE *)LocalAlloc(LMEM_ZEROINIT, 1);
        if (pData == NULL)
        {
            CloseHandle(hFile);
            return FALSE;
        }
    }

    CloseHandle(hFile);

    *ppData = pData;
    *pcbData = total;
    return TRUE;
}

static BOOL LooksLikeUtf8(const BYTE *pData, DWORD cbData)
{
    int i;
    BOOL hasHigh = FALSE;

    for (i = 0; i < (int)cbData; ++i)
    {
        if (pData[i] >= 0x80)
        {
            hasHigh = TRUE;
            break;
        }
    }

    if (!hasHigh)
        return FALSE;

    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                               (LPCSTR)pData, (int)cbData, NULL, 0) != 0;
}

static BOOL HasHighBytes(const BYTE *pData, DWORD cbData)
{
    DWORD i;

    for (i = 0; i < cbData; ++i)
    {
        if (pData[i] >= 0x80)
            return TRUE;
    }

    return FALSE;
}

LPCWSTR EncodingName(TextEncoding encoding)
{
    switch (encoding)
    {
    case ENC_UTF16LE:  return L"UTF-16 LE";
    case ENC_UTF16BE:  return L"UTF-16 BE";
    case ENC_UTF8_BOM:
    {
        LPCWSTR lpName = LangGetStringById(IDS_ENC_UTF8BOM);

        return lpName != NULL ? lpName : L"UTF-8 with BOM";
    }
    case ENC_UTF8:     return L"UTF-8";
    case ENC_GB18030:  return L"GB18030";
    case ENC_OEM:      return L"OEM";
    default:           return L"ANSI (Windows-1252)";
    }
}

// Converte bytes em texto UTF-16 usando a codificacao informada. Um BOM
// compativel com a codificacao e descartado; bytes invalidos viram U+FFFD.
// O buffer devolvido deve ser liberado com LocalFree.
BOOL DecodeText(const BYTE *pData, DWORD cbData, TextEncoding encoding, LPWSTR *ppText)
{
    LPWSTR lpText = NULL;
    DWORD skip = 0;
    int cchWide = 0;

    *ppText = NULL;

    if (encoding == ENC_UTF16LE)
    {
        if (cbData >= 2 && pData[0] == 0xFF && pData[1] == 0xFE)
            skip = 2;
    }
    else if (encoding == ENC_UTF16BE)
    {
        if (cbData >= 2 && pData[0] == 0xFE && pData[1] == 0xFF)
            skip = 2;
    }
    else if (encoding == ENC_UTF8 || encoding == ENC_UTF8_BOM)
    {
        if (cbData >= 3 && pData[0] == 0xEF && pData[1] == 0xBB && pData[2] == 0xBF)
            skip = 3;
    }

    if (encoding == ENC_UTF16LE || encoding == ENC_UTF16BE)
    {
        DWORD cbWide = cbData - skip;
        DWORD i;
        const BYTE *pSrc = pData + skip;

        cchWide = (int)(cbWide / 2);
        lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)cchWide + 1) * sizeof(WCHAR));
        if (lpText == NULL)
            return FALSE;

        if (encoding == ENC_UTF16LE)
        {
            memcpy(lpText, pSrc, (SIZE_T)cchWide * sizeof(WCHAR));
        }
        else
        {
            for (i = 0; i < (DWORD)cchWide; ++i)
                lpText[i] = (WCHAR)((pSrc[i * 2] << 8) | pSrc[i * 2 + 1]);
        }

        lpText[cchWide] = L'\0';
    }
    else
    {
        UINT codePage = CodePageFor(encoding);

        cchWide = MultiByteToWideChar(codePage, 0, (LPCSTR)(pData + skip),
                                      (int)(cbData - skip), NULL, 0);
        if (cchWide < 0)
            cchWide = 0;

        lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)cchWide + 1) * sizeof(WCHAR));
        if (lpText == NULL)
            return FALSE;

        if (cchWide > 0)
        {
            MultiByteToWideChar(codePage, 0, (LPCSTR)(pData + skip),
                                (int)(cbData - skip), lpText, cchWide);
        }

        lpText[cchWide] = L'\0';
    }

    *ppText = lpText;
    return TRUE;
}

BOOL LoadTextFile(LPCWSTR lpFileName, LPWSTR *ppText, TextEncoding *pEncoding)
{
    BYTE *pData = NULL;
    DWORD cbData = 0;
    LPWSTR lpText = NULL;
    // Padrao UTF-8: arquivos vazios e ASCII puro (identicos em ANSI e UTF-8)
    // continuam UTF-8, para que acentos digitados depois nao sejam perdidos.
    TextEncoding encoding = ENC_UTF8;

    *ppText = NULL;
    *pEncoding = ENC_UTF8;

    if (!ReadAllBytes(lpFileName, &pData, &cbData))
        return FALSE;

    if (cbData >= 3 && pData[0] == 0xEF && pData[1] == 0xBB && pData[2] == 0xBF)
    {
        encoding = ENC_UTF8_BOM;
    }
    else if (cbData >= 2 && pData[0] == 0xFF && pData[1] == 0xFE)
    {
        encoding = ENC_UTF16LE;
    }
    else if (cbData >= 2 && pData[0] == 0xFE && pData[1] == 0xFF)
    {
        encoding = ENC_UTF16BE;
    }
    else if (cbData >= 2 && pData[0] != 0 && pData[1] == 0)
    {
        encoding = ENC_UTF16LE;
    }
    else if (cbData >= 2 && pData[0] == 0 && pData[1] != 0)
    {
        encoding = ENC_UTF16BE;
    }
    else if (LooksLikeUtf8(pData, cbData))
    {
        encoding = ENC_UTF8;
    }
    else if (HasHighBytes(pData, cbData))
    {
        // Bytes altos que nao formam UTF-8 valido: texto ANSI (Windows-1252).
        encoding = ENC_ANSI;
    }

    if (!DecodeText(pData, cbData, encoding, &lpText))
    {
        LocalFree(pData);
        return FALSE;
    }

    LocalFree(pData);

    *ppText = lpText;
    *pEncoding = encoding;
    return TRUE;
}

// Converte texto UTF-16 em bytes na codificacao informada (com BOM quando a
// codificacao o exige). O buffer devolvido deve ser liberado com LocalFree.
BOOL EncodeText(LPCWSTR lpText, TextEncoding encoding, BYTE **ppBytes, DWORD *pcbBytes)
{
    int cchText = lstrlenW(lpText);
    BYTE *pBytes = NULL;
    DWORD cbBytes = 0;

    *ppBytes = NULL;
    *pcbBytes = 0;

    if (encoding == ENC_UTF16LE)
    {
        cbBytes = (DWORD)(cchText * sizeof(WCHAR));
        pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, cbBytes + 2);
        if (pBytes == NULL)
            return FALSE;
        pBytes[0] = 0xFF;
        pBytes[1] = 0xFE;
        memcpy(pBytes + 2, lpText, cbBytes);
        cbBytes += 2;
    }
    else if (encoding == ENC_UTF16BE)
    {
        int i;
        cbBytes = (DWORD)(cchText * sizeof(WCHAR));
        pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, cbBytes + 2);
        if (pBytes == NULL)
            return FALSE;
        pBytes[0] = 0xFE;
        pBytes[1] = 0xFF;
        for (i = 0; i < cchText; ++i)
        {
            pBytes[2 + i * 2] = (BYTE)((lpText[i] >> 8) & 0xFF);
            pBytes[2 + i * 2 + 1] = (BYTE)(lpText[i] & 0xFF);
        }
        cbBytes += 2;
    }
    else
    {
        // Sem WC_ERR_INVALID_CHARS: com a flag, um unico surrogate invalido
        // faz a conversao retornar 0 e o arquivo era gravado vazio. Sem ela,
        // o Windows substitui por U+FFFD.
        UINT codePage = CodePageFor(encoding);
        DWORD flags = 0;
        int cb;

        cb = WideCharToMultiByte(codePage, flags, lpText, cchText, NULL, 0, NULL, NULL);
        if (cb < 0)
            cb = 0;

        pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, (SIZE_T)cb + 4);
        if (pBytes == NULL)
            return FALSE;

        if (encoding == ENC_UTF8_BOM)
        {
            pBytes[0] = 0xEF;
            pBytes[1] = 0xBB;
            pBytes[2] = 0xBF;
            cbBytes = 3;
        }

        if (cb > 0)
        {
            WideCharToMultiByte(codePage, flags, lpText, cchText,
                                (LPSTR)(pBytes + cbBytes), cb, NULL, NULL);
        }

        cbBytes += (DWORD)cb;
    }

    *ppBytes = pBytes;
    *pcbBytes = cbBytes;
    return TRUE;
}

// Diz se o texto pode ser gravado na codificacao sem trocar nenhum caractere.
// So ANSI/OEM (que nao cobrem todo o Unicode) e UTF-8 com surrogate invalido
// podem perder informacao; UTF-16 guarda qualquer sequencia de WCHAR.
BOOL CanEncodeLossless(LPCWSTR lpText, TextEncoding encoding)
{
    int cchText = lstrlenW(lpText);

    if (cchText == 0)
        return TRUE;

    switch (encoding)
    {
    case ENC_ANSI:
    case ENC_OEM:
    {
        // A conversao precisa ocorrer de verdade: lpUsedDefaultChar nao e
        // confiavel numa chamada que so consulta o tamanho.
        BOOL usedDefault = FALSE;
        int cap = cchText * 2 + 2;
        char *pBuf = (char *)LocalAlloc(LMEM_FIXED, (SIZE_T)cap);

        if (pBuf == NULL)
            return TRUE;

        WideCharToMultiByte(CodePageFor(encoding), WC_NO_BEST_FIT_CHARS,
                            lpText, cchText, pBuf, cap, NULL, &usedDefault);
        LocalFree(pBuf);
        return !usedDefault;
    }

    case ENC_UTF8:
    case ENC_UTF8_BOM:
        return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                   lpText, cchText, NULL, 0, NULL, NULL) != 0;

    default:
        return TRUE;
    }
}

static BOOL IsByteOrientedEncoding(TextEncoding encoding)
{
    return encoding == ENC_ANSI || encoding == ENC_UTF8 || encoding == ENC_UTF8_BOM;
}

// "Reinterpretar": descobre como ficaria o texto se os bytes que ele ocupa em
// `from` fossem lidos como `to`, e devolve o resultado em *ppNew (LocalFree).
// Retorna FALSE quando a reinterpretacao nao faz sentido ou nao e segura:
//  - so vale entre ANSI, UTF-8 e UTF-8 BOM; com UTF-16 os bytes de um lado nao
//    formam texto valido do outro, entao so existe conversao;
//  - o texto atual precisa caber em `from` sem perda, senao os bytes ja nao
//    representam o que esta na tela;
//  - ao ler como UTF-8, os bytes precisam ser UTF-8 valido (um arquivo ANSI com
//    acentos nunca e, e isso e o que distingue "converter" de "reinterpretar").
BOOL ReinterpretText(LPCWSTR lpText, TextEncoding from, TextEncoding to, LPWSTR *ppNew)
{
    int cchText = lstrlenW(lpText);
    UINT cpFrom;
    UINT cpTo;
    BYTE *pBytes = NULL;
    int cb;
    int cchNew;
    LPWSTR lpNew;

    *ppNew = NULL;

    if (from == to || !IsByteOrientedEncoding(from) || !IsByteOrientedEncoding(to))
        return FALSE;

    if (cchText == 0 || !CanEncodeLossless(lpText, from))
        return FALSE;

    cpFrom = CodePageFor(from);
    cpTo = CodePageFor(to);

    // Bytes "puros", sem BOM: o BOM nao e conteudo do texto.
    cb = WideCharToMultiByte(cpFrom, 0, lpText, cchText, NULL, 0, NULL, NULL);
    if (cb <= 0)
        return FALSE;

    pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, (SIZE_T)cb);
    if (pBytes == NULL)
        return FALSE;

    if (WideCharToMultiByte(cpFrom, 0, lpText, cchText, (LPSTR)pBytes, cb, NULL, NULL) != cb)
    {
        LocalFree(pBytes);
        return FALSE;
    }

    if (cpTo == CP_UTF8 &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)pBytes, cb, NULL, 0) == 0)
    {
        LocalFree(pBytes);
        return FALSE;
    }

    cchNew = MultiByteToWideChar(cpTo, 0, (LPCSTR)pBytes, cb, NULL, 0);
    if (cchNew <= 0)
    {
        LocalFree(pBytes);
        return FALSE;
    }

    lpNew = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)cchNew + 1) * sizeof(WCHAR));
    if (lpNew == NULL)
    {
        LocalFree(pBytes);
        return FALSE;
    }

    MultiByteToWideChar(cpTo, 0, (LPCSTR)pBytes, cb, lpNew, cchNew);
    lpNew[cchNew] = L'\0';

    LocalFree(pBytes);
    *ppNew = lpNew;
    return TRUE;
}

BOOL SaveTextFile(LPCWSTR lpFileName, LPCWSTR lpText, TextEncoding encoding)
{
    HANDLE hFile;
    BOOL ok = TRUE;
    BYTE *pBytes = NULL;
    DWORD cbBytes = 0;

    if (!EncodeText(lpText, encoding, &pBytes, &cbBytes))
        return FALSE;

    hFile = CreateFileW(lpFileName, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        LocalFree(pBytes);
        return FALSE;
    }

    if (cbBytes > 0)
    {
        DWORD written = 0;
        if (!WriteFile(hFile, pBytes, cbBytes, &written, NULL) || written != cbBytes)
            ok = FALSE;
    }

    CloseHandle(hFile);
    LocalFree(pBytes);

    return ok;
}
