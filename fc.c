/*
 * PROJECT:     ReactOS FC Command
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Comparing files
 * COPYRIGHT:   Copyright 2021 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */
#include "fc.h"

#ifdef __REACTOS__
    #include <conutils.h>
#else
    #include <stdio.h>
    #define StdOut stdout
    #define StdErr stderr
    void ConPuts(FILE *fp, LPCWSTR psz)
    {
        fputws(psz, fp);
    }
    void ConPrintf(FILE *fp, LPCWSTR psz, ...)
    {
        va_list va;
        va_start(va, psz);
        vfwprintf(fp, psz, va);
        va_end(va);
    }
    void ConResPuts(FILE *fp, UINT nID)
    {
        WCHAR sz[MAX_PATH];
        LoadStringW(NULL, nID, sz, MAX_PATH);
        fputws(sz, fp);
    }
    void ConResPrintf(FILE *fp, UINT nID, ...)
    {
        va_list va;
        WCHAR sz[MAX_PATH];
        va_start(va, nID);
        LoadStringW(NULL, nID, sz, MAX_PATH);
        vfwprintf(fp, sz, va);
        va_end(va);
    }
#endif

FCRET NoDifference(VOID)
{
    ConResPuts(StdOut, IDS_NO_DIFFERENCE);
    return FCRET_IDENTICAL;
}

FCRET Different(LPCWSTR file0, LPCWSTR file1)
{
    ConResPrintf(StdOut, IDS_DIFFERENT, file0, file1);
    return FCRET_DIFFERENT;
}

FCRET LongerThan(LPCWSTR file0, LPCWSTR file1)
{
    ConResPrintf(StdOut, IDS_LONGER_THAN, file0, file1);
    return FCRET_DIFFERENT;
}

FCRET OutOfMemory(VOID)
{
    ConResPuts(StdErr, IDS_OUT_OF_MEMORY);
    return FCRET_INVALID;
}

FCRET CannotRead(LPCWSTR file)
{
    ConResPrintf(StdErr, IDS_CANNOT_READ, file);
    return FCRET_INVALID;
}

FCRET InvalidSwitch(VOID)
{
    ConResPuts(StdErr, IDS_INVALID_SWITCH);
    return FCRET_INVALID;
}

FCRET ResyncFailed(VOID)
{
    ConResPuts(StdOut, IDS_RESYNC_FAILED);
    return FCRET_DIFFERENT;
}

VOID PrintCaption(LPCWSTR file)
{
    ConPrintf(StdOut, L"***** %ls\n", file);
}

VOID PrintEndOfDiff(VOID)
{
    ConPuts(StdOut, L"*****\n\n");
}

VOID PrintDots(VOID)
{
    ConPuts(StdOut, L"...\n");
}

VOID PrintLineW(const FILECOMPARE *pFC, DWORD lineno, LPCWSTR psz)
{
    if (pFC->dwFlags & FLAG_N)
        ConPrintf(StdOut, L"%5d:  %ls\n", lineno, psz);
    else
        ConPrintf(StdOut, L"%ls\n", psz);
}
VOID PrintLineA(const FILECOMPARE *pFC, DWORD lineno, LPCSTR psz)
{
    if (pFC->dwFlags & FLAG_N)
        ConPrintf(StdOut, L"%5d:  %hs\n", lineno, psz);
    else
        ConPrintf(StdOut, L"%hs\n", psz);
}

HANDLE DoOpenFileForInput(LPCWSTR file)
{
    HANDLE hFile = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ConResPrintf(StdErr, IDS_CANNOT_OPEN, file);
    }
    return hFile;
}

static FCRET BinaryFileCompare(FILECOMPARE *pFC)
{
    FCRET ret;
    HANDLE hFile0, hFile1, hMapping0 = NULL, hMapping1 = NULL;
    LPBYTE pb0 = NULL, pb1 = NULL;
    LARGE_INTEGER ib, cb0, cb1, cbCommon;
    DWORD cbView, ibView;
    BOOL fDifferent = FALSE;

    hFile0 = DoOpenFileForInput(pFC->file[0]);
    if (hFile0 == INVALID_HANDLE_VALUE)
        return FCRET_CANT_FIND;
    hFile1 = DoOpenFileForInput(pFC->file[1]);
    if (hFile1 == INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile0);
        return FCRET_CANT_FIND;
    }

    do
    {
        if (_wcsicmp(pFC->file[0], pFC->file[1]) == 0)
        {
            ret = NoDifference();
            break;
        }
        if (!GetFileSizeEx(hFile0, &cb0))
        {
            ret = CannotRead(pFC->file[0]);
            break;
        }
        if (!GetFileSizeEx(hFile1, &cb1))
        {
            ret = CannotRead(pFC->file[1]);
            break;
        }
        cbCommon.QuadPart = min(cb0.QuadPart, cb1.QuadPart);
        if (cbCommon.QuadPart > 0)
        {
            hMapping0 = CreateFileMappingW(hFile0, NULL, PAGE_READONLY,
                                           cb0.HighPart, cb0.LowPart, NULL);
            if (hMapping0 == NULL)
            {
                ret = CannotRead(pFC->file[0]);
                break;
            }
            hMapping1 = CreateFileMappingW(hFile1, NULL, PAGE_READONLY,
                                           cb1.HighPart, cb1.LowPart, NULL);
            if (hMapping1 == NULL)
            {
                ret = CannotRead(pFC->file[1]);
                break;
            }

            ret = FCRET_IDENTICAL;
            for (ib.QuadPart = 0; ib.QuadPart < cbCommon.QuadPart; )
            {
                cbView = (DWORD)min(cbCommon.QuadPart - ib.QuadPart, MAX_VIEW_SIZE);
                pb0 = MapViewOfFile(hMapping0, FILE_MAP_READ, ib.HighPart, ib.LowPart, cbView);
                pb1 = MapViewOfFile(hMapping1, FILE_MAP_READ, ib.HighPart, ib.LowPart, cbView);
                if (!pb0 || !pb1)
                {
                    ret = OutOfMemory();
                    break;
                }
                for (ibView = 0; ibView < cbView; ++ib.QuadPart, ++ibView)
                {
                    if (pb0[ibView] == pb1[ibView])
                        continue;

                    fDifferent = TRUE;
                    if (cbCommon.QuadPart > MAXDWORD)
                    {
                        ConPrintf(StdOut, L"%016I64X: %02X %02X\n", ib.QuadPart,
                                  pb0[ibView], pb1[ibView]);
                    }
                    else
                    {
                        ConPrintf(StdOut, L"%08lX: %02X %02X\n", ib.LowPart,
                                  pb0[ibView], pb1[ibView]);
                    }
                }
                UnmapViewOfFile(pb0);
                UnmapViewOfFile(pb1);
                pb0 = pb1 = NULL;
            }
            if (ret != FCRET_IDENTICAL)
                break;
        }

        if (cb0.QuadPart < cb1.QuadPart)
            ret = LongerThan(pFC->file[1], pFC->file[0]);
        else if (cb0.QuadPart > cb1.QuadPart)
            ret = LongerThan(pFC->file[0], pFC->file[1]);
        else if (fDifferent)
            ret = Different(pFC->file[0], pFC->file[1]);
        else
            ret = NoDifference();
    } while (0);

    UnmapViewOfFile(pb0);
    UnmapViewOfFile(pb1);
    CloseHandle(hMapping0);
    CloseHandle(hMapping1);
    CloseHandle(hFile0);
    CloseHandle(hFile1);
    return ret;
}

static FCRET TextFileCompare(FILECOMPARE *pFC)
{
    FCRET ret;
    HANDLE hFile0, hFile1, hMapping0 = NULL, hMapping1 = NULL;
    LARGE_INTEGER cb0, cb1;
    BOOL fUnicode = !!(pFC->dwFlags & FLAG_U);

    hFile0 = DoOpenFileForInput(pFC->file[0]);
    if (hFile0 == INVALID_HANDLE_VALUE)
        return FCRET_CANT_FIND;
    hFile1 = DoOpenFileForInput(pFC->file[1]);
    if (hFile1 == INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile0);
        return FCRET_CANT_FIND;
    }

    do
    {
        if (_wcsicmp(pFC->file[0], pFC->file[1]) == 0)
        {
            ret = NoDifference();
            break;
        }
        if (!GetFileSizeEx(hFile0, &cb0))
        {
            ret = CannotRead(pFC->file[0]);
            break;
        }
        if (!GetFileSizeEx(hFile1, &cb1))
        {
            ret = CannotRead(pFC->file[1]);
            break;
        }
        if (cb0.QuadPart == 0 && cb1.QuadPart == 0)
        {
            ret = NoDifference();
            break;
        }
        hMapping0 = CreateFileMappingW(hFile0, NULL, PAGE_READONLY,
                                       cb0.HighPart, cb0.LowPart, NULL);
        if (hMapping0 == NULL)
        {
            ret = CannotRead(pFC->file[0]);
            break;
        }
        hMapping1 = CreateFileMappingW(hFile1, NULL, PAGE_READONLY,
                                       cb1.HighPart, cb1.LowPart, NULL);
        if (hMapping1 == NULL)
        {
            ret = CannotRead(pFC->file[1]);
            break;
        }

        if (fUnicode)
            ret = TextCompareW(pFC, &hMapping0, &cb0, &hMapping1, &cb1);
        else
            ret = TextCompareA(pFC, &hMapping0, &cb0, &hMapping1, &cb1);
    } while (0);

    CloseHandle(hMapping0);
    CloseHandle(hMapping1);
    CloseHandle(hFile0);
    CloseHandle(hFile1);
    return ret;
}

static BOOL IsBinaryExt(LPCWSTR filename)
{
    // Don't change this array. This is by design.
    // See also: https://docs.microsoft.com/en-us/windows-server/administration/windows-commands/fc
    static const LPCWSTR s_exts[] = { L"EXE", L"COM", L"SYS", L"OBJ", L"LIB", L"BIN" };
    size_t iext;
    LPCWSTR pch, ext, pch0 = wcsrchr(filename, L'\\'), pch1 = wcsrchr(filename, L'/');
    if (!pch0 && !pch1)
        pch = filename;
    else if (!pch0 && pch1)
        pch = pch1;
    else if (pch0 && !pch1)
        pch = pch0;
    else if (pch0 < pch1)
        pch = pch1;
    else
        pch = pch0;

    ext = wcsrchr(pch, L'.');
    if (ext)
    {
        ++ext;
        for (iext = 0; iext < _countof(s_exts); ++iext)
        {
            if (_wcsicmp(ext, s_exts[iext]) == 0)
                return TRUE;
        }
    }
    return FALSE;
}

#define HasWildcard(filename) \
    ((wcschr((filename), L'*') != NULL) || (wcschr((filename), L'?') != NULL))

static FCRET FileCompare(FILECOMPARE *pFC)
{
    ConResPrintf(StdOut, IDS_COMPARING, pFC->file[0], pFC->file[1]);

    if (!(pFC->dwFlags & FLAG_L) &&
        ((pFC->dwFlags & FLAG_B) || IsBinaryExt(pFC->file[0]) || IsBinaryExt(pFC->file[1])))
    {
        return BinaryFileCompare(pFC);
    }
    return TextFileCompare(pFC);
}

static FCRET WildcardFileCompare(FILECOMPARE *pFC)
{
    FCRET ret;

    if (pFC->dwFlags & FLAG_HELP)
    {
        ConResPuts(StdOut, IDS_USAGE);
        return FCRET_INVALID;
    }

    if (!pFC->file[0] || !pFC->file[1])
    {
        ConResPuts(StdErr, IDS_NEEDS_FILES);
        return FCRET_INVALID;
    }

    if (HasWildcard(pFC->file[0]) || HasWildcard(pFC->file[1]))
    {
        // TODO: wildcard
        ConResPuts(StdErr, IDS_CANT_USE_WILDCARD);
    }

    ret = FileCompare(pFC);
    ConPuts(StdOut, L"\n");
    return ret;
}

int wmain(int argc, WCHAR **argv)
{
    FILECOMPARE fc = { .dwFlags = 0, .n = 100, .nnnn = 2 };
    PWCHAR endptr;
    INT i;

#ifdef __REACTOS__
    /* Initialize the Console Standard Streams */
    ConInitStdStreams();
#endif
    for (i = 1; i < argc; ++i)
    {
        if (argv[i][0] != L'/')
        {
            if (!fc.file[0])
                fc.file[0] = argv[i];
            else if (!fc.file[1])
                fc.file[1] = argv[i];
            else
                return InvalidSwitch();
            continue;
        }
        switch (towupper(argv[i][1]))
        {
            case L'A':
                fc.dwFlags |= FLAG_A;
                break;
            case L'B':
                fc.dwFlags |= FLAG_B;
                break;
            case L'C':
                fc.dwFlags |= FLAG_C;
                break;
            case L'L':
                if (_wcsicmp(argv[i], L"/L") == 0)
                {
                    fc.dwFlags |= FLAG_L;
                }
                else if (towupper(argv[i][2]) == L'B')
                {
                    if (iswdigit(argv[i][3]))
                    {
                        fc.dwFlags |= FLAG_LBn;
                        fc.n = wcstoul(&argv[i][3], &endptr, 10);
                        if (endptr == NULL || *endptr != 0)
                            return InvalidSwitch();
                    }
                    else
                    {
                        return InvalidSwitch();
                    }
                }
                break;
            case L'N':
                fc.dwFlags |= FLAG_N;
                break;
            case L'O':
                if (_wcsicmp(argv[i], L"/OFF") == 0 || _wcsicmp(argv[i], L"/OFFLINE") == 0)
                {
                    fc.dwFlags |= FLAG_OFFLINE;
                }
                break;
            case L'T':
                fc.dwFlags |= FLAG_T;
                break;
            case L'U':
                fc.dwFlags |= FLAG_U;
                break;
            case L'W':
                fc.dwFlags |= FLAG_W;
                break;
            case L'0': case L'1': case L'2': case L'3': case L'4':
            case L'5': case L'6': case L'7': case L'8': case L'9':
                fc.nnnn = wcstoul(&argv[i][1], &endptr, 10);
                if (endptr == NULL || *endptr != 0)
                    return InvalidSwitch();
                fc.dwFlags |= FLAG_nnnn;
                break;
            case L'?':
                fc.dwFlags |= FLAG_HELP;
                break;
            default:
                return InvalidSwitch();
        }
    }
    return WildcardFileCompare(&fc);
}

#ifndef __REACTOS__
int main(int argc, char **argv)
{
    INT my_argc;
    LPWSTR *my_argv = CommandLineToArgvW(GetCommandLineW(), &my_argc);
    INT ret = wmain(my_argc, my_argv);
    LocalFree(my_argv);
    return ret;
}
#endif
