#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
extern "C" __declspec(dllimport) int lmms_missing_dep_anchor();
int wmain(int argc, wchar_t** argv)
{
    if (lmms_missing_dep_anchor() != 1) { return 2; }
    if (argc > 1 && std::wcscmp(argv[1], L"--inspect") == 0)
    {
        const wchar_t* plugin = _wgetenv(L"QT_PLUGIN_PATH");
        const wchar_t* sentinel = _wgetenv(L"LMMS_ENV_SENTINEL");
        std::wprintf(L"plugins=%ls\nsentinel=%ls\n", plugin ? plugin : L"", sentinel ? sentinel : L"");
        for (int i = 2; i < argc; ++i) { std::wprintf(L"arg=%ls\n", argv[i]); }
    }
    if (argc > 1 && std::wcscmp(argv[1], L"--tree") == 0)
    {
        wchar_t path[32768];
        GetModuleFileNameW(nullptr, path, 32768);
        std::wstring command = L"\"" + std::wstring(path) + L"\" --hang";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) { return 3; }
        std::wprintf(L"descendant=%lu\n", process.dwProcessId); std::fflush(stdout);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        Sleep(INFINITE);
    }
    if (argc > 1 && std::wcscmp(argv[1], L"--hang") == 0) { Sleep(INFINITE); }
    return 0;
}
#else
int main() { return 0; }
#endif
