/*
 * lmms_test_launcher.cpp - start a test with Qt's bin directory on PATH.
 *
 * SetErrorMode in the test itself cannot catch a missing import DLL, because
 * that failure happens before main. This process has no Qt imports. It sets
 * the error mode, prepends the Qt bin directory, and forwards the child status.
 */

#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <stdio.h>
#endif

#ifdef _WIN32

namespace
{

std::wstring quote(const std::wstring& value)
{
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\') { ++slashes; continue; }
        out.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += ch;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}

bool prependEnv(std::wstring& block, const std::wstring& key, const std::wstring& prefix)
{
	const std::wstring needle = key + L"=";
	size_t pos = 0;
	while (pos < block.size())
	{
		const size_t end = block.find(L'\0', pos);
		if (end == std::wstring::npos) { break; }
		const std::wstring entry = block.substr(pos, end - pos);
		if (entry.size() >= needle.size() && _wcsnicmp(entry.c_str(), needle.c_str(), needle.size()) == 0)
		{
			const std::wstring updated = needle + prefix + L";" + entry.substr(needle.size());
			block.replace(pos, end - pos, updated);
			return true;
		}
		pos = end + 1;
	}
	std::wstring entry = needle + prefix;
	entry.push_back(L'\0');
	block.insert(0, entry);
	return true;
}

int fail(const wchar_t* message, unsigned code)
{
	fwprintf(stderr, L"lmms_test_launcher: %ls (%u)\n", message, code);
	return static_cast<int>(code == 0 ? 1 : code);
}

} // namespace

int main()
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr) { return fail(L"CommandLineToArgvW failed", GetLastError()); }

	std::wstring prefix;
	std::wstring plugins;
	DWORD timeoutMs = 180000;
	std::vector<std::wstring> child;
	bool separator = false;
	for (int index = 1; index < argc; ++index)
	{
		const std::wstring arg = argv[index];
		if (!separator && arg == L"--") { separator = true; continue; }
		if (!separator && arg == L"--prefix" && index + 1 < argc) { prefix = argv[++index]; continue; }
		if (!separator && arg == L"--plugins" && index + 1 < argc) { plugins = argv[++index]; continue; }
		if (!separator && arg == L"--timeout" && index + 1 < argc) { timeoutMs = static_cast<DWORD>(_wtoi(argv[++index]) * 1000); continue; }
		if (separator) { child.push_back(arg); }
	}
	LocalFree(argv);
	if (child.empty()) { return fail(L"missing child command", 2); }

	std::wstring command;
	for (const auto& part : child)
	{
		if (!command.empty()) { command += L' '; }
		command += quote(part);
	}

	std::wstring environment;
	wchar_t* current = GetEnvironmentStringsW();
	if (current != nullptr)
	{
		for (wchar_t* cursor = current; *cursor != L'\0'; cursor += wcslen(cursor) + 1)
		{
			environment.append(cursor);
			if (environment.empty()) { environment.push_back(L'\0'); }
	environment.push_back(L'\0');
		}
		FreeEnvironmentStringsW(current);
	}
	if (environment.empty()) { environment.push_back(L'\0'); }
	environment.push_back(L'\0');
	if (!prefix.empty()) { prependEnv(environment, L"PATH", prefix); }
	if (!plugins.empty()) { prependEnv(environment, L"QT_PLUGIN_PATH", plugins); }
	std::vector<std::wstring> entries;
	for (size_t pos = 0; pos < environment.size() && environment[pos] != L'\0';)
	{
		const auto end = environment.find(L'\0', pos);
		entries.push_back(environment.substr(pos, end - pos));
		pos = end + 1;
	}
	std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
		return _wcsicmp(left.c_str(), right.c_str()) < 0;
	});
	environment.clear();
	for (const auto& entry : entries) { environment += entry; environment.push_back(L'\0'); }
	if (environment.empty()) { environment.push_back(L'\0'); }
	environment.push_back(L'\0');

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	for (HANDLE handle : {startup.hStdInput, startup.hStdOutput, startup.hStdError})
	{
		if (handle && handle != INVALID_HANDLE_VALUE) { SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT); }
	}
	PROCESS_INFORMATION process{};
	std::vector<wchar_t> mutableCommand(command.begin(), command.end());
	mutableCommand.push_back(L'\0');
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        const auto error = GetLastError();
        if (job) { CloseHandle(job); }
        return fail(L"Could not create the child cleanup job", error);
    }
	if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
			environment.empty() ? nullptr : environment.data(), nullptr, &startup, &process))
	{
		const auto error = GetLastError();
		CloseHandle(job);
		return fail(L"CreateProcessW failed", error);
	}

    if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == static_cast<DWORD>(-1))
    {
        const auto error = GetLastError();
        TerminateProcess(process.hProcess, 125);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
        return fail(L"Could not start a supervised child", error);
    }
	const DWORD waited = WaitForSingleObject(process.hProcess, timeoutMs);
	if (waited != WAIT_OBJECT_0)
	{
		const DWORD error = GetLastError();
		TerminateJobObject(job, 124);
		WaitForSingleObject(process.hProcess, 5000);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		CloseHandle(job);
		if (waited != WAIT_TIMEOUT) { return fail(L"Waiting for the child failed", error); }
		fwprintf(stderr, L"lmms_test_launcher: child timed out after %lu ms\n", timeoutMs);
		return 124;
	}
	DWORD status = 1;
	GetExitCodeProcess(process.hProcess, &status);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	CloseHandle(job);
	return static_cast<int>(status);
}

#else

int main(int argc, char** argv)
{
	fprintf(stderr, "lmms_test_launcher is only used on Windows\n");
	return argc > 0 && argv != nullptr ? 0 : 1;
}

#endif
