// crashlog.h - registrador de crash para diagnostico (ativo em Debug e Release).
// Grava crash.log ao lado do .exe com excecao, endereco e pilha de chamadas
// (funcao + linha para o codigo do Notbad, usando o Notbad.pdb da mesma pasta).
#pragma once

#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>

#pragma comment(lib, "dbghelp.lib")

static WCHAR         g_szCrashLogPath[MAX_PATH] = {};
static WCHAR         g_szCrashLogDir[MAX_PATH]  = {};
static volatile LONG g_lCrashLogCount           = 0;
static BOOL          g_bCrashSymInit            = FALSE;

static void CrashLogFrame(FILE* f, HANDLE hProc, DWORD64 addr)
{
    HMODULE hMod = NULL;
    WCHAR szMod[MAX_PATH] = L"?";
    DWORD64 modOffset = 0;
    char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    SYMBOL_INFO* pSym = (SYMBOL_INFO*)symBuf;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line = {};
    DWORD lineDisp = 0;

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)addr, &hMod) && hMod != NULL)
    {
        GetModuleFileNameW(hMod, szMod, MAX_PATH);
        modOffset = addr - (DWORD64)hMod;
    }

    fprintf(f, "  0x%016llX  %ls+0x%llX", (unsigned long long)addr,
            wcsrchr(szMod, L'\\') != NULL ? wcsrchr(szMod, L'\\') + 1 : szMod,
            (unsigned long long)modOffset);

    pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSym->MaxNameLen = 255;
    if (g_bCrashSymInit && SymFromAddr(hProc, addr, &disp, pSym))
        fprintf(f, "  %s+0x%llX", pSym->Name, (unsigned long long)disp);

    line.SizeOfStruct = sizeof(line);
    if (g_bCrashSymInit && SymGetLineFromAddr64(hProc, addr, &lineDisp, &line))
        fprintf(f, "  [%s:%lu]", line.FileName, line.LineNumber);

    fprintf(f, "\n");
}

static LONG CALLBACK CrashLogHandler(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    FILE* f;
    HANDLE hProc = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();
    CONTEXT ctx;
    STACKFRAME64 sf = {};
    int i;

    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != 0xC0000374 /* heap corruption */ &&
        code != 0xC0000409 /* fail fast */)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedIncrement(&g_lCrashLogCount) > 5)
        return EXCEPTION_CONTINUE_SEARCH;

    if (!g_bCrashSymInit)
    {
        char szDir[MAX_PATH] = {};

        WideCharToMultiByte(CP_ACP, 0, g_szCrashLogDir, -1, szDir, MAX_PATH, NULL, NULL);
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        g_bCrashSymInit = SymInitialize(hProc, szDir, TRUE);
    }

    f = _wfopen(g_szCrashLogPath, L"a");
    if (f == NULL)
        return EXCEPTION_CONTINUE_SEARCH;

    fprintf(f, "=== EXCECAO 0x%08lX em 0x%016llX (thread %lu) ===\n",
            code, (unsigned long long)(DWORD64)ep->ExceptionRecord->ExceptionAddress,
            GetCurrentThreadId());

    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
    {
        fprintf(f, "  acesso invalido: %s endereco 0x%016llX\n",
                ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "LEITURA" :
                ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "ESCRITA" : "EXECUCAO",
                (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    fprintf(f, "Pilha:\n");

    ctx = *ep->ContextRecord;
    sf.AddrPC.Offset    = ctx.Rip;
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrFrame.Mode   = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrStack.Mode   = AddrModeFlat;

    for (i = 0; i < 40; ++i)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProc, hThread, &sf, &ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (sf.AddrPC.Offset == 0)
            break;
        CrashLogFrame(f, hProc, sf.AddrPC.Offset);
    }

    fprintf(f, "\n");
    fclose(f);

    return EXCEPTION_CONTINUE_SEARCH;
}

static void CrashLogInstall(void)
{
    WCHAR* pSlash;

    GetModuleFileNameW(NULL, g_szCrashLogDir, MAX_PATH);
    pSlash = wcsrchr(g_szCrashLogDir, L'\\');
    if (pSlash != NULL)
        *pSlash = L'\0';

    wcscpy_s(g_szCrashLogPath, MAX_PATH, g_szCrashLogDir);
    wcscat_s(g_szCrashLogPath, MAX_PATH, L"\\crash.log");

    AddVectoredExceptionHandler(1, CrashLogHandler);
}
