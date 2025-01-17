// Copyright (c) .NET Foundation and contributors. All rights reserved. Licensed under the Microsoft Reciprocal License. See LICENSE.TXT file in the project root for full license information.

#include "precomp.h"


// Exit macros
#define PathExitOnLastError(x, s, ...) ExitOnLastErrorSource(DUTIL_SOURCE_PATHUTIL, x, s, __VA_ARGS__)
#define PathExitOnLastErrorDebugTrace(x, s, ...) ExitOnLastErrorDebugTraceSource(DUTIL_SOURCE_PATHUTIL, x, s, __VA_ARGS__)
#define PathExitWithLastError(x, s, ...) ExitWithLastErrorSource(DUTIL_SOURCE_PATHUTIL, x, s, __VA_ARGS__)
#define PathExitOnFailure(x, s, ...) ExitOnFailureSource(DUTIL_SOURCE_PATHUTIL, x, s, __VA_ARGS__)
#define PathExitOnRootFailure(x, s, ...) ExitOnRootFailureSource(DUTIL_SOURCE_PATHUTIL, x, s, __VA_ARGS__)
#define PathExitWithRootFailure(x, e, s, ...) ExitWithRootFailureSource(DUTIL_SOURCE_PATHUTIL, x, e, s, __VA_ARGS__)
#define PathExitOnFailureDebugTrace(x, s, ...) ExitOnFailureDebugTraceSource(DUTIL_SOURCE_PATHUTIL, x, s, __VA_ARGS__)
#define PathExitOnNull(p, x, e, s, ...) ExitOnNullSource(DUTIL_SOURCE_PATHUTIL, p, x, e, s, __VA_ARGS__)
#define PathExitOnNullWithLastError(p, x, s, ...) ExitOnNullWithLastErrorSource(DUTIL_SOURCE_PATHUTIL, p, x, s, __VA_ARGS__)
#define PathExitOnNullDebugTrace(p, x, e, s, ...)  ExitOnNullDebugTraceSource(DUTIL_SOURCE_PATHUTIL, p, x, e, s, __VA_ARGS__)
#define PathExitOnInvalidHandleWithLastError(p, x, s, ...) ExitOnInvalidHandleWithLastErrorSource(DUTIL_SOURCE_PATHUTIL, p, x, s, __VA_ARGS__)
#define PathExitOnWin32Error(e, x, s, ...) ExitOnWin32ErrorSource(DUTIL_SOURCE_PATHUTIL, e, x, s, __VA_ARGS__)
#define PathExitOnGdipFailure(g, x, s, ...) ExitOnGdipFailureSource(DUTIL_SOURCE_PATHUTIL, g, x, s, __VA_ARGS__)

#define PATH_GOOD_ENOUGH 64


DAPI_(LPWSTR) PathFile(
    __in_z LPCWSTR wzPath
    )
{
    if (!wzPath)
    {
        return NULL;
    }

    LPWSTR wzFile = const_cast<LPWSTR>(wzPath);
    for (LPWSTR wz = wzFile; *wz; ++wz)
    {
        // valid delineators 
        //     \ => Windows path
        //     / => unix and URL path
        //     : => relative path from mapped root
        if (L'\\' == *wz || L'/' == *wz || (L':' == *wz && wz == wzPath + 1))
        {
            wzFile = wz + 1;
        }
    }

    return wzFile;
}


DAPI_(LPCWSTR) PathExtension(
    __in_z LPCWSTR wzPath
    )
{
    if (!wzPath)
    {
        return NULL;
    }

    // Find the last dot in the last thing that could be a file.
    LPCWSTR wzExtension = NULL;
    for (LPCWSTR wz = wzPath; *wz; ++wz)
    {
        if (L'\\' == *wz || L'/' == *wz || L':' == *wz)
        {
            wzExtension = NULL;
        }
        else if (L'.' == *wz)
        {
            wzExtension = wz;
        }
    }

    return wzExtension;
}


DAPI_(HRESULT) PathGetDirectory(
    __in_z LPCWSTR wzPath,
    __out_z LPWSTR *psczDirectory
    )
{
    HRESULT hr = S_OK;
    size_t cchDirectory = SIZE_T_MAX;

    for (LPCWSTR wz = wzPath; *wz; ++wz)
    {
        // valid delineators:
        //     \ => Windows path
        //     / => unix and URL path
        //     : => relative path from mapped root
        if (L'\\' == *wz || L'/' == *wz || (L':' == *wz && wz == wzPath + 1))
        {
            cchDirectory = static_cast<size_t>(wz - wzPath) + 1;
        }
    }

    if (SIZE_T_MAX == cchDirectory)
    {
        // we were given just a file name, so there's no directory available
        return S_FALSE;
    }

    if (wzPath[0] == L'\"')
    {
        ++wzPath;
        --cchDirectory;
    }

    hr = StrAllocString(psczDirectory, wzPath, cchDirectory);
    PathExitOnFailure(hr, "Failed to copy directory.");

LExit:
    return hr;
}


DAPI_(HRESULT) PathGetParentPath(
    __in_z LPCWSTR wzPath,
    __out_z LPWSTR *psczParent
    )
{
    HRESULT hr = S_OK;
    LPCWSTR wzParent = NULL;

    for (LPCWSTR wz = wzPath; *wz; ++wz)
    {
        if (wz[1] && (L'\\' == *wz || L'/' == *wz))
        {
            wzParent = wz;
        }
    }

    if (wzParent)
    {
        size_t cchPath = static_cast<size_t>(wzParent - wzPath) + 1;

        hr = StrAllocString(psczParent, wzPath, cchPath);
        PathExitOnFailure(hr, "Failed to copy directory.");
    }
    else
    {
        ReleaseNullStr(psczParent);
    }

LExit:
    return hr;
}


DAPI_(HRESULT) PathExpand(
    __out LPWSTR *psczFullPath,
    __in_z LPCWSTR wzRelativePath,
    __in DWORD dwResolveFlags
    )
{
    Assert(wzRelativePath && *wzRelativePath);

    HRESULT hr = S_OK;
    DWORD cch = 0;
    LPWSTR sczExpandedPath = NULL;
    DWORD cchExpandedPath = 0;
    SIZE_T cbSize = 0;

    LPWSTR sczFullPath = NULL;

    //
    // First, expand any environment variables.
    //
    if (dwResolveFlags & PATH_EXPAND_ENVIRONMENT)
    {
        cchExpandedPath = PATH_GOOD_ENOUGH;

        hr = StrAlloc(&sczExpandedPath, cchExpandedPath);
        PathExitOnFailure(hr, "Failed to allocate space for expanded path.");

        cch = ::ExpandEnvironmentStringsW(wzRelativePath, sczExpandedPath, cchExpandedPath);
        if (0 == cch)
        {
            PathExitWithLastError(hr, "Failed to expand environment variables in string: %ls", wzRelativePath);
        }
        else if (cchExpandedPath < cch)
        {
            cchExpandedPath = cch;
            hr = StrAlloc(&sczExpandedPath, cchExpandedPath);
            PathExitOnFailure(hr, "Failed to re-allocate more space for expanded path.");

            cch = ::ExpandEnvironmentStringsW(wzRelativePath, sczExpandedPath, cchExpandedPath);
            if (0 == cch)
            {
                PathExitWithLastError(hr, "Failed to expand environment variables in string: %ls", wzRelativePath);
            }
            else if (cchExpandedPath < cch)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
                PathExitOnRootFailure(hr, "Failed to allocate buffer for expanded path.");
            }
        }

        if (MAX_PATH < cch)
        {
            hr = PathPrefix(&sczExpandedPath); // ignore invald arg from path prefix because this may not be a complete path yet
            if (E_INVALIDARG == hr)
            {
                hr = S_OK;
            }
            PathExitOnFailure(hr, "Failed to prefix long path after expanding environment variables.");

            hr = StrMaxLength(sczExpandedPath, &cbSize);
            PathExitOnFailure(hr, "Failed to get max length of expanded path.");

            cchExpandedPath = (DWORD)min(DWORD_MAX, cbSize);
        }
    }

    //
    // Second, get the full path.
    //
    if (dwResolveFlags & PATH_EXPAND_FULLPATH)
    {
        LPWSTR wzFileName = NULL;
        LPCWSTR wzPath = sczExpandedPath ? sczExpandedPath : wzRelativePath;
        DWORD cchFullPath = max(PATH_GOOD_ENOUGH, cchExpandedPath);

        hr = StrAlloc(&sczFullPath, cchFullPath);
        PathExitOnFailure(hr, "Failed to allocate space for full path.");

        cch = ::GetFullPathNameW(wzPath, cchFullPath, sczFullPath, &wzFileName);
        if (0 == cch)
        {
            PathExitWithLastError(hr, "Failed to get full path for string: %ls", wzPath);
        }
        else if (cchFullPath < cch)
        {
            cchFullPath = cch < MAX_PATH ? cch : cch + 7; // ensure space for "\\?\UNC" prefix if needed
            hr = StrAlloc(&sczFullPath, cchFullPath);
            PathExitOnFailure(hr, "Failed to re-allocate more space for full path.");

            cch = ::GetFullPathNameW(wzPath, cchFullPath, sczFullPath, &wzFileName);
            if (0 == cch)
            {
                PathExitWithLastError(hr, "Failed to get full path for string: %ls", wzPath);
            }
            else if (cchFullPath < cch)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
                PathExitOnRootFailure(hr, "Failed to allocate buffer for full path.");
            }
        }

        if (MAX_PATH < cch)
        {
            hr = PathPrefix(&sczFullPath);
            PathExitOnFailure(hr, "Failed to prefix long path after expanding.");
        }
    }
    else
    {
        sczFullPath = sczExpandedPath;
        sczExpandedPath = NULL;
    }

    hr = StrAllocString(psczFullPath, sczFullPath? sczFullPath : wzRelativePath, 0);
    PathExitOnFailure(hr, "Failed to copy relative path into full path.");

LExit:
    ReleaseStr(sczFullPath);
    ReleaseStr(sczExpandedPath);

    return hr;
}


DAPI_(HRESULT) PathPrefix(
    __inout LPWSTR *psczFullPath
    )
{
    Assert(psczFullPath && *psczFullPath);

    HRESULT hr = S_OK;
    LPWSTR wzFullPath = *psczFullPath;
    SIZE_T cbFullPath = 0;

    if (((L'a' <= wzFullPath[0] && L'z' >= wzFullPath[0]) ||
         (L'A' <= wzFullPath[0] && L'Z' >= wzFullPath[0])) &&
        L':' == wzFullPath[1] &&
        L'\\' == wzFullPath[2]) // normal path
    {
        hr = StrAllocPrefix(psczFullPath, L"\\\\?\\", 4);
        PathExitOnFailure(hr, "Failed to add prefix to file path.");
    }
    else if (L'\\' == wzFullPath[0] && L'\\' == wzFullPath[1]) // UNC
    {
        // ensure that we're not already prefixed
        if (!(L'?' == wzFullPath[2] && L'\\' == wzFullPath[3]))
        {
            hr = StrSize(*psczFullPath, &cbFullPath);
            PathExitOnFailure(hr, "Failed to get size of full path.");

            memmove_s(wzFullPath, cbFullPath, wzFullPath + 1, cbFullPath - sizeof(WCHAR));

            hr = StrAllocPrefix(psczFullPath, L"\\\\?\\UNC", 7);
            PathExitOnFailure(hr, "Failed to add prefix to UNC path.");
        }
    }
    else
    {
        hr = E_INVALIDARG;
        PathExitOnFailure(hr, "Invalid path provided to prefix: %ls.", wzFullPath);
    }

LExit:
    return hr;
}


DAPI_(HRESULT) PathFixedBackslashTerminate(
    __inout_ecount_z(cchPath) LPWSTR wzPath,
    __in SIZE_T cchPath
    )
{
    HRESULT hr = S_OK;
    size_t cchLength = 0;

    hr = ::StringCchLengthW(wzPath, cchPath, &cchLength);
    PathExitOnFailure(hr, "Failed to get length of path.");

    if (cchLength >= cchPath)
    {
        hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    else if (L'\\' != wzPath[cchLength - 1])
    {
        wzPath[cchLength] = L'\\';
        wzPath[cchLength + 1] = L'\0';
    }

LExit:
    return hr;
}


DAPI_(HRESULT) PathBackslashTerminate(
    __inout LPWSTR* psczPath
    )
{
    Assert(psczPath && *psczPath);

    HRESULT hr = S_OK;
    SIZE_T cchPath = 0;
    size_t cchLength = 0;

    hr = StrMaxLength(*psczPath, &cchPath);
    PathExitOnFailure(hr, "Failed to get size of path string.");

    hr = ::StringCchLengthW(*psczPath, cchPath, &cchLength);
    PathExitOnFailure(hr, "Failed to get length of path.");

    if (L'\\' != (*psczPath)[cchLength - 1])
    {
        hr = StrAllocConcat(psczPath, L"\\", 1);
        PathExitOnFailure(hr, "Failed to concat backslash onto string.");
    }

LExit:
    return hr;
}


DAPI_(HRESULT) PathForCurrentProcess(
    __inout LPWSTR *psczFullPath,
    __in_opt HMODULE hModule
    )
{
    HRESULT hr = S_OK;
    DWORD cch = MAX_PATH;

    do
    {
        hr = StrAlloc(psczFullPath, cch);
        PathExitOnFailure(hr, "Failed to allocate string for module path.");

        DWORD cchRequired = ::GetModuleFileNameW(hModule, *psczFullPath, cch);
        if (0 == cchRequired)
        {
            PathExitWithLastError(hr, "Failed to get path for executing process.");
        }
        else if (cchRequired == cch)
        {
            cch = cchRequired + 1;
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            hr = S_OK;
        }
    } while (HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) == hr);

LExit:
    return hr;
}


DAPI_(HRESULT) PathRelativeToModule(
    __inout LPWSTR *psczFullPath,
    __in_opt LPCWSTR wzFileName,
    __in_opt HMODULE hModule
    )
{
    HRESULT hr = PathForCurrentProcess(psczFullPath, hModule);
    PathExitOnFailure(hr, "Failed to get current module path.");

    hr = PathGetDirectory(*psczFullPath, psczFullPath);
    PathExitOnFailure(hr, "Failed to get current module directory.");

    if (wzFileName)
    {
        hr = PathConcat(*psczFullPath, wzFileName, psczFullPath);
        PathExitOnFailure(hr, "Failed to append filename.");
    }

LExit:
    return hr;
}


DAPI_(HRESULT) PathCreateTempFile(
    __in_opt LPCWSTR wzDirectory,
    __in_opt __format_string LPCWSTR wzFileNameTemplate,
    __in DWORD dwUniqueCount,
    __in DWORD dwFileAttributes,
    __out_opt LPWSTR* psczTempFile,
    __out_opt HANDLE* phTempFile
    )
{
    AssertSz(0 < dwUniqueCount, "Must specify a non-zero unique count.");

    HRESULT hr = S_OK;

    LPWSTR sczTempPath = NULL;
    DWORD cchTempPath = MAX_PATH;

    HANDLE hTempFile = INVALID_HANDLE_VALUE;
    LPWSTR scz = NULL;
    LPWSTR sczTempFile = NULL;

    if (wzDirectory && *wzDirectory)
    {
        hr = StrAllocString(&sczTempPath, wzDirectory, 0);
        PathExitOnFailure(hr, "Failed to copy temp path.");
    }
    else
    {
        hr = StrAlloc(&sczTempPath, cchTempPath);
        PathExitOnFailure(hr, "Failed to allocate memory for the temp path.");

        if (!::GetTempPathW(cchTempPath, sczTempPath))
        {
            PathExitWithLastError(hr, "Failed to get temp path.");
        }
    }

    if (wzFileNameTemplate && *wzFileNameTemplate)
    {
        for (DWORD i = 1; i <= dwUniqueCount && INVALID_HANDLE_VALUE == hTempFile; ++i)
        {
            hr = StrAllocFormatted(&scz, wzFileNameTemplate, i);
            PathExitOnFailure(hr, "Failed to allocate memory for file template.");

            hr = StrAllocFormatted(&sczTempFile, L"%s%s", sczTempPath, scz);
            PathExitOnFailure(hr, "Failed to allocate temp file name.");

            hTempFile = ::CreateFileW(sczTempFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_NEW, dwFileAttributes, NULL);
            if (INVALID_HANDLE_VALUE == hTempFile)
            {
                // if the file already exists, just try again
                hr = HRESULT_FROM_WIN32(::GetLastError());
                if (HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) == hr)
                {
                    hr = S_OK;
                }
                PathExitOnFailure(hr, "Failed to create file: %ls", sczTempFile);
            }
        }
    }

    // If we were not able to or we did not try to create a temp file, ask
    // the system to provide us a temp file using its built-in mechanism.
    if (INVALID_HANDLE_VALUE == hTempFile)
    {
        hr = StrAlloc(&sczTempFile, MAX_PATH);
        PathExitOnFailure(hr, "Failed to allocate memory for the temp path");

        if (!::GetTempFileNameW(sczTempPath, L"TMP", 0, sczTempFile))
        {
            PathExitWithLastError(hr, "Failed to create new temp file name.");
        }

        hTempFile = ::CreateFileW(sczTempFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, dwFileAttributes, NULL);
        if (INVALID_HANDLE_VALUE == hTempFile)
        {
            PathExitWithLastError(hr, "Failed to open new temp file: %ls", sczTempFile);
        }
    }

    // If the caller wanted the temp file name or handle, return them here.
    if (psczTempFile)
    {
        hr = StrAllocString(psczTempFile, sczTempFile, 0);
        PathExitOnFailure(hr, "Failed to copy temp file string.");
    }

    if (phTempFile)
    {
        *phTempFile = hTempFile;
        hTempFile = INVALID_HANDLE_VALUE;
    }

LExit:
    if (INVALID_HANDLE_VALUE != hTempFile)
    {
        ::CloseHandle(hTempFile);
    }

    ReleaseStr(scz);
    ReleaseStr(sczTempFile);
    ReleaseStr(sczTempPath);

    return hr;
}


DAPI_(HRESULT) PathCreateTimeBasedTempFile(
    __in_z_opt LPCWSTR wzDirectory,
    __in_z LPCWSTR wzPrefix,
    __in_z_opt LPCWSTR wzPostfix,
    __in_z LPCWSTR wzExtension,
    __deref_opt_out_z LPWSTR* psczTempFile,
    __out_opt HANDLE* phTempFile
    )
{
    HRESULT hr = S_OK;
    BOOL fRetry = FALSE;
    WCHAR wzTempPath[MAX_PATH] = { };
    LPWSTR sczPrefix = NULL;
    LPWSTR sczPrefixFolder = NULL;
    SYSTEMTIME time = { };

    LPWSTR sczTempPath = NULL;
    HANDLE hTempFile = INVALID_HANDLE_VALUE;
    DWORD dwAttempts = 0;

    if (wzDirectory && *wzDirectory)
    {
        hr = PathConcat(wzDirectory, wzPrefix, &sczPrefix);
        PathExitOnFailure(hr, "Failed to combine directory and log prefix.");
    }
    else
    {
        if (!::GetTempPathW(countof(wzTempPath), wzTempPath))
        {
            PathExitWithLastError(hr, "Failed to get temp folder.");
        }

        hr = PathConcat(wzTempPath, wzPrefix, &sczPrefix);
        PathExitOnFailure(hr, "Failed to concatenate the temp folder and log prefix.");
    }

    hr = PathGetDirectory(sczPrefix, &sczPrefixFolder);
    if (S_OK == hr)
    {
        hr = DirEnsureExists(sczPrefixFolder, NULL);
        PathExitOnFailure(hr, "Failed to ensure temp file path exists: %ls", sczPrefixFolder);
    }

    if (!wzPostfix)
    {
        wzPostfix = L"";
    }

    do
    {
        fRetry = FALSE;
        ++dwAttempts;

        ::GetLocalTime(&time);

        // Log format:                         pre YYYY MM  dd  hh  mm  ss post ext
        hr = StrAllocFormatted(&sczTempPath, L"%ls_%04u%02u%02u%02u%02u%02u%ls%ls%ls", sczPrefix, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, wzPostfix, L'.' == *wzExtension ? L"" : L".", wzExtension);
        PathExitOnFailure(hr, "failed to allocate memory for the temp path");

        hTempFile = ::CreateFileW(sczTempPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE == hTempFile)
        {
            // If the file already exists, just try again.
            DWORD er = ::GetLastError();
            if (ERROR_FILE_EXISTS == er || ERROR_ACCESS_DENIED == er)
            {
                ::Sleep(100);

                if (10 > dwAttempts)
                {
                    er = ERROR_SUCCESS;
                    fRetry = TRUE;
                }
            }

            hr = HRESULT_FROM_WIN32(er);
            PathExitOnFailureDebugTrace(hr, "Failed to create temp file: %ls", sczTempPath);
        }
    } while (fRetry);

    if (psczTempFile)
    {
        hr = StrAllocString(psczTempFile, sczTempPath, 0);
        PathExitOnFailure(hr, "Failed to copy temp path to return.");
    }

    if (phTempFile)
    {
        *phTempFile = hTempFile;
        hTempFile = INVALID_HANDLE_VALUE;
    }

LExit:
    ReleaseFile(hTempFile);
    ReleaseStr(sczTempPath);
    ReleaseStr(sczPrefixFolder);
    ReleaseStr(sczPrefix);

    return hr;
}


DAPI_(HRESULT) PathCreateTempDirectory(
    __in_opt LPCWSTR wzDirectory,
    __in __format_string LPCWSTR wzDirectoryNameTemplate,
    __in DWORD dwUniqueCount,
    __out LPWSTR* psczTempDirectory
    )
{
    AssertSz(wzDirectoryNameTemplate && *wzDirectoryNameTemplate, "DirectoryNameTemplate must be specified.");
    AssertSz(0 < dwUniqueCount, "Must specify a non-zero unique count.");

    HRESULT hr = S_OK;

    LPWSTR sczTempPath = NULL;
    DWORD cchTempPath = MAX_PATH;

    LPWSTR scz = NULL;

    if (wzDirectory && *wzDirectory)
    {
        hr = StrAllocString(&sczTempPath, wzDirectory, 0);
        PathExitOnFailure(hr, "Failed to copy temp path.");

        hr = PathBackslashTerminate(&sczTempPath);
        PathExitOnFailure(hr, "Failed to ensure path ends in backslash: %ls", wzDirectory);
    }
    else
    {
        hr = StrAlloc(&sczTempPath, cchTempPath);
        PathExitOnFailure(hr, "Failed to allocate memory for the temp path.");

        if (!::GetTempPathW(cchTempPath, sczTempPath))
        {
            PathExitWithLastError(hr, "Failed to get temp path.");
        }
    }

    for (DWORD i = 1; i <= dwUniqueCount; ++i)
    {
        hr = StrAllocFormatted(&scz, wzDirectoryNameTemplate, i);
        PathExitOnFailure(hr, "Failed to allocate memory for directory name template.");

        hr = StrAllocFormatted(psczTempDirectory, L"%s%s", sczTempPath, scz);
        PathExitOnFailure(hr, "Failed to allocate temp directory name.");

        if (!::CreateDirectoryW(*psczTempDirectory, NULL))
        {
            DWORD er = ::GetLastError();
            if (ERROR_ALREADY_EXISTS == er)
            {
                hr = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
                continue;
            }
            else if (ERROR_PATH_NOT_FOUND == er)
            {
                hr = DirEnsureExists(*psczTempDirectory, NULL);
                break;
            }
            else
            {
                hr = HRESULT_FROM_WIN32(er);
                break;
            }
        }
        else
        {
            hr = S_OK;
            break;
        }
    }
    PathExitOnFailure(hr, "Failed to create temp directory.");

    hr = PathBackslashTerminate(psczTempDirectory);
    PathExitOnFailure(hr, "Failed to ensure temp directory is backslash terminated.");

LExit:
    ReleaseStr(scz);
    ReleaseStr(sczTempPath);

    return hr;
}


DAPI_(HRESULT) PathGetTempPath(
    __out_z LPWSTR* psczTempPath
    )
{
    HRESULT hr = S_OK;
    WCHAR wzTempPath[MAX_PATH + 1] = { };
    DWORD cch = 0;

    cch = ::GetTempPathW(countof(wzTempPath), wzTempPath);
    if (!cch)
    {
        PathExitWithLastError(hr, "Failed to GetTempPath.");
    }
    else if (cch >= countof(wzTempPath))
    {
        PathExitWithRootFailure(hr, E_INSUFFICIENT_BUFFER, "TEMP directory path too long.");
    }

    hr = StrAllocString(psczTempPath, wzTempPath, cch);
    PathExitOnFailure(hr, "Failed to copy TEMP directory path.");

LExit:
    return hr;
}


DAPI_(HRESULT) PathGetSystemTempPath(
    __out_z LPWSTR* psczSystemTempPath
    )
{
    HRESULT hr = S_OK;
    HKEY hKey = NULL;
    WCHAR wzTempPath[MAX_PATH + 1] = { };
    DWORD cch = 0;

    // There is no documented API to get system environment variables, so read them from the registry.
    hr = RegOpen(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\Session Manager\\Environment", KEY_READ, &hKey);
    if (E_FILENOTFOUND != hr)
    {
        PathExitOnFailure(hr, "Failed to open system environment registry key.");

        // Follow documented precedence rules for TMP/TEMP from ::GetTempPath.
        // TODO: values will be expanded with the current environment variables instead of the system environment variables.
        hr = RegReadString(hKey, L"TMP", psczSystemTempPath);
        if (E_FILENOTFOUND != hr)
        {
            PathExitOnFailure(hr, "Failed to get system TMP value.");

            hr = PathBackslashTerminate(psczSystemTempPath);
            PathExitOnFailure(hr, "Failed to backslash terminate system TMP value.");

            ExitFunction();
        }

        hr = RegReadString(hKey, L"TEMP", psczSystemTempPath);
        if (E_FILENOTFOUND != hr)
        {
            PathExitOnFailure(hr, "Failed to get system TEMP value.");

            hr = PathBackslashTerminate(psczSystemTempPath);
            PathExitOnFailure(hr, "Failed to backslash terminate system TEMP value.");

            ExitFunction();
        }
    }

    cch = ::GetSystemWindowsDirectoryW(wzTempPath, countof(wzTempPath));
    if (!cch)
    {
        PathExitWithLastError(hr, "Failed to get Windows directory path.");
    }
    else if (cch >= countof(wzTempPath))
    {
        PathExitWithRootFailure(hr, E_INSUFFICIENT_BUFFER, "Windows directory path too long.");
    }

    hr = PathConcat(wzTempPath, L"TEMP\\", psczSystemTempPath);
    PathExitOnFailure(hr, "Failed to concat Temp directory on Windows directory path.");

LExit:
    ReleaseRegKey(hKey);

    return hr;
}


DAPI_(HRESULT) PathGetKnownFolder(
    __in int csidl,
    __out LPWSTR* psczKnownFolder
    )
{
    HRESULT hr = S_OK;

    hr = StrAlloc(psczKnownFolder, MAX_PATH);
    PathExitOnFailure(hr, "Failed to allocate memory for known folder.");

    hr = ::SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, *psczKnownFolder);
    PathExitOnFailure(hr, "Failed to get known folder path.");

    hr = PathBackslashTerminate(psczKnownFolder);
    PathExitOnFailure(hr, "Failed to ensure known folder path is backslash terminated.");

LExit:
    return hr;
}


DAPI_(BOOL) PathIsAbsolute(
    __in_z LPCWSTR wzPath
    )
{
    return wzPath && wzPath[0] && wzPath[1] && (wzPath[0] == L'\\') || (wzPath[1] == L':');
}


DAPI_(HRESULT) PathConcat(
    __in_opt LPCWSTR wzPath1,
    __in_opt LPCWSTR wzPath2,
    __deref_out_z LPWSTR* psczCombined
    )
{
    return PathConcatCch(wzPath1, 0, wzPath2, 0, psczCombined);
}


DAPI_(HRESULT) PathConcatCch(
    __in_opt LPCWSTR wzPath1,
    __in SIZE_T cchPath1,
    __in_opt LPCWSTR wzPath2,
    __in SIZE_T cchPath2,
    __deref_out_z LPWSTR* psczCombined
    )
{
    HRESULT hr = S_OK;

    if (!wzPath2 || !*wzPath2)
    {
        hr = StrAllocString(psczCombined, wzPath1, cchPath1);
        PathExitOnFailure(hr, "Failed to copy just path1 to output.");
    }
    else if (!wzPath1 || !*wzPath1 || PathIsAbsolute(wzPath2))
    {
        hr = StrAllocString(psczCombined, wzPath2, cchPath2);
        PathExitOnFailure(hr, "Failed to copy just path2 to output.");
    }
    else
    {
        hr = StrAllocString(psczCombined, wzPath1, cchPath1);
        PathExitOnFailure(hr, "Failed to copy path1 to output.");

        hr = PathBackslashTerminate(psczCombined);
        PathExitOnFailure(hr, "Failed to backslashify.");

        hr = StrAllocConcat(psczCombined, wzPath2, cchPath2);
        PathExitOnFailure(hr, "Failed to append path2 to output.");
    }

LExit:
    return hr;
}


DAPI_(HRESULT) PathEnsureQuoted(
    __inout LPWSTR* ppszPath,
    __in BOOL fDirectory
    )
{
    Assert(ppszPath && *ppszPath);

    HRESULT hr = S_OK;
    size_t cchPath = 0;

    hr = ::StringCchLengthW(*ppszPath, STRSAFE_MAX_CCH, &cchPath);
    PathExitOnFailure(hr, "Failed to get the length of the path.");

    // Handle simple special cases.
    if (0 == cchPath || (1 == cchPath && L'"' == (*ppszPath)[0]))
    {
        hr = StrAllocString(ppszPath, L"\"\"", 2);
        PathExitOnFailure(hr, "Failed to allocate a quoted empty string.");

        ExitFunction();
    }

    if (L'"' != (*ppszPath)[0])
    {
        hr = StrAllocPrefix(ppszPath, L"\"", 1);
        PathExitOnFailure(hr, "Failed to allocate an opening quote.");

        // Add a char for the opening quote.
        ++cchPath;
    }

    if (L'"' != (*ppszPath)[cchPath - 1])
    {
        hr = StrAllocConcat(ppszPath, L"\"", 1);
        PathExitOnFailure(hr, "Failed to allocate a closing quote.");

        // Add a char for the closing quote.
        ++cchPath;
    }

    if (fDirectory)
    {
        if (L'\\' != (*ppszPath)[cchPath - 2])
        {
            // Change the last char to a backslash and re-append the closing quote.
            (*ppszPath)[cchPath - 1] = L'\\';

            hr = StrAllocConcat(ppszPath, L"\"", 1);
            PathExitOnFailure(hr, "Failed to allocate another closing quote after the backslash.");
        }
    }

LExit:

    return hr;
}


DAPI_(HRESULT) PathCompare(
    __in_z LPCWSTR wzPath1,
    __in_z LPCWSTR wzPath2,
    __out int* pnResult
    )
{
    HRESULT hr = S_OK;
    LPWSTR sczPath1 = NULL;
    LPWSTR sczPath2 = NULL;

    hr = PathExpand(&sczPath1, wzPath1, PATH_EXPAND_ENVIRONMENT | PATH_EXPAND_FULLPATH);
    PathExitOnFailure(hr, "Failed to expand path1.");

    hr = PathExpand(&sczPath2, wzPath2, PATH_EXPAND_ENVIRONMENT | PATH_EXPAND_FULLPATH);
    PathExitOnFailure(hr, "Failed to expand path2.");

    *pnResult = ::CompareStringW(LOCALE_NEUTRAL, NORM_IGNORECASE, sczPath1, -1, sczPath2, -1);

LExit:
    ReleaseStr(sczPath2);
    ReleaseStr(sczPath1);

    return hr;
}


DAPI_(HRESULT) PathCompress(
    __in_z LPCWSTR wzPath
    )
{
    HRESULT hr = S_OK;
    HANDLE hPath = INVALID_HANDLE_VALUE;

    hPath = ::CreateFileW(wzPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (INVALID_HANDLE_VALUE == hPath)
    {
        PathExitWithLastError(hr, "Failed to open path %ls for compression.", wzPath);
    }

    DWORD dwBytesReturned = 0;
    USHORT usCompressionFormat = COMPRESSION_FORMAT_DEFAULT;
    if (0 == ::DeviceIoControl(hPath, FSCTL_SET_COMPRESSION, &usCompressionFormat, sizeof(usCompressionFormat), NULL, 0, &dwBytesReturned, NULL))
    {
        // ignore compression attempts on file systems that don't support it
        DWORD er = ::GetLastError();
        if (ERROR_INVALID_FUNCTION != er)
        {
            PathExitOnWin32Error(er, hr, "Failed to set compression state for path %ls.", wzPath);
        }
    }

LExit:
    ReleaseFile(hPath);

    return hr;
}

DAPI_(HRESULT) PathGetHierarchyArray(
    __in_z LPCWSTR wzPath,
    __deref_inout_ecount_opt(*pcPathArray) LPWSTR **prgsczPathArray,
    __inout LPUINT pcPathArray
    )
{
    HRESULT hr = S_OK;
    LPWSTR sczPathCopy = NULL;
    LPWSTR sczNewPathCopy = NULL;
    DWORD cArraySpacesNeeded = 0;
    size_t cchPath = 0;

    hr = ::StringCchLengthW(wzPath, STRSAFE_MAX_LENGTH, &cchPath);
    PathExitOnRootFailure(hr, "Failed to get string length of path: %ls", wzPath);

    if (!cchPath)
    {
        ExitFunction1(hr = E_INVALIDARG);
    }

    for (size_t i = 0; i < cchPath; ++i)
    {
        if (wzPath[i] == L'\\')
        {
            ++cArraySpacesNeeded;
        }
    }

    if (wzPath[cchPath - 1] != L'\\')
    {
        ++cArraySpacesNeeded;
    }

    // If it's a UNC path, cut off the first three paths, 2 because it starts with a double backslash, and another because the first ("\\servername\") isn't a path.
    if (wzPath[0] == L'\\' && wzPath[1] == L'\\')
    {
        cArraySpacesNeeded -= 3;
    }

    Assert(cArraySpacesNeeded >= 1);

    hr = MemEnsureArraySize(reinterpret_cast<void **>(prgsczPathArray), cArraySpacesNeeded, sizeof(LPWSTR), 0);
    PathExitOnFailure(hr, "Failed to allocate array of size %u for parent directories", cArraySpacesNeeded);
    *pcPathArray = cArraySpacesNeeded;

    hr = StrAllocString(&sczPathCopy, wzPath, 0);
    PathExitOnFailure(hr, "Failed to allocate copy of original path");

    for (DWORD i = 0; i < cArraySpacesNeeded; ++i)
    {
        hr = StrAllocString((*prgsczPathArray) + cArraySpacesNeeded - 1 - i, sczPathCopy, 0);
        PathExitOnFailure(hr, "Failed to copy path");

        DWORD cchPathCopy = lstrlenW(sczPathCopy);

        // If it ends in a backslash, it's a directory path, so cut off everything the last backslash before we get the directory portion of the path
        if (wzPath[cchPathCopy - 1] == L'\\')
        {
            sczPathCopy[cchPathCopy - 1] = L'\0';
        }
        
        hr = PathGetDirectory(sczPathCopy, &sczNewPathCopy);
        PathExitOnFailure(hr, "Failed to get directory portion of path");

        ReleaseStr(sczPathCopy);
        sczPathCopy = sczNewPathCopy;
        sczNewPathCopy = NULL;
    }

    hr = S_OK;

LExit:
    ReleaseStr(sczPathCopy);

    return hr;
}
