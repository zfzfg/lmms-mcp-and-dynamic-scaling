// Console launcher: forwards the host's stdio directly to the bundled runtime.
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
int wmain(int argc, wchar_t** argv)
{
	std::wstring executable(32768, L'\0');
	const DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
	if (!count || count == executable.size()) { return 1; }
	executable.resize(count);
	auto root = std::filesystem::path(executable).parent_path() / LMMS_MCP_PACKAGE_RELATIVE;
	auto quote = [](const std::wstring& value) {
		std::wstring result = L"\"";
		unsigned slashes = 0;
		for (wchar_t ch : value)
		{
			if (ch == L'\\')
			{
				++slashes;
				continue;
			}
			if (ch == L'\"') { result.append(slashes * 2 + 1, L'\\'); }
			else { result.append(slashes, L'\\'); }
			slashes = 0;
			result += ch;
		}
		result.append(slashes * 2, L'\\');
		return result + L"\"";
	};
	const auto runtime = root / "bin/node.exe";
	std::wstring command = quote(runtime.wstring()) + L" " + quote((root / "dist/src/index.js").wstring());
	for (int i = 1; i < argc; ++i)
	{
		command += L" " + quote(argv[i]);
	}
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(runtime.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
			&startup, &process))
	{
		return 1;
	}
	CloseHandle(process.hThread);
	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD status = 1;
	GetExitCodeProcess(process.hProcess, &status);
	CloseHandle(process.hProcess);
	return static_cast<int>(status);
}
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
int main(int argc, char** argv)
{
	std::string executable(32768, '\0');
#ifdef __APPLE__
	uint32_t count = static_cast<uint32_t>(executable.size());
	if (_NSGetExecutablePath(executable.data(), &count)) { return 1; }
	executable.resize(std::char_traits<char>::length(executable.c_str()));
#else
	auto count = readlink("/proc/self/exe", executable.data(), executable.size());
	if (count <= 0 || static_cast<size_t>(count) == executable.size()) { return 1; }
	executable.resize(count);
#endif
	const auto root = std::filesystem::canonical(executable).parent_path() / LMMS_MCP_PACKAGE_RELATIVE;
	std::string runtime = (root / "bin/node").string(), script = (root / "dist/src/index.js").string();
	std::vector<char*> arguments{runtime.data(), script.data()};
	for (int i = 1; i < argc; ++i)
	{
		arguments.push_back(argv[i]);
	}
	arguments.push_back(nullptr);
	execv(runtime.c_str(), arguments.data());
	return 1;
}
#endif
