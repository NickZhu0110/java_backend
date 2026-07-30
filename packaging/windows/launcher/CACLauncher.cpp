#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kWindowTitle[] = L"CAC Platform";
constexpr wchar_t kMutexName[] =
    L"Local\\CACPlatform.PortableLauncher.v0.6.10.Windows.x64.CPU";
constexpr INTERNET_PORT kBackendPort = 6006;
constexpr DWORD kBackendStartupTimeoutSeconds = 120;
constexpr DWORD kBackendShutdownTimeoutMilliseconds = 20000;

struct LauncherError {
    explicit LauncherError(std::wstring value) : message(std::move(value)) {}
    std::wstring message;
};

[[noreturn]] void Fail(const std::wstring& message) {
    throw LauncherError(message);
}

std::wstring Win32ErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);

    std::wstring result =
        length != 0 && buffer != nullptr ? std::wstring(buffer, length)
                                         : L"Unknown Windows error";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n' ||
            result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

std::wstring WithLastError(const std::wstring& action, DWORD error = GetLastError()) {
    return action + L"\n\nWindows error " + std::to_wstring(error) + L": " +
           Win32ErrorText(error);
}

void ShowError(const std::wstring& message) {
    MessageBoxW(nullptr,
                message.c_str(),
                kWindowTitle,
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

void ShowWarning(const std::wstring& message) {
    MessageBoxW(nullptr,
                message.c_str(),
                kWindowTitle,
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
}

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) : value_(value) {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const {
        return value_;
    }

    [[nodiscard]] bool valid() const {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() {
        const HANDLE value = value_;
        value_ = nullptr;
        return value;
    }

    void reset(HANDLE replacement = nullptr) {
        if (valid()) {
            CloseHandle(value_);
        }
        value_ = replacement;
    }

private:
    HANDLE value_ = nullptr;
};

void AppendLauncherLog(const fs::path& logPath,
                       const std::wstring& message) noexcept {
    SYSTEMTIME timestamp{};
    GetLocalTime(&timestamp);

    wchar_t prefix[64]{};
    if (swprintf_s(prefix,
                   L"%04u-%02u-%02u %02u:%02u:%02u.%03u [PID %lu] ",
                   timestamp.wYear,
                   timestamp.wMonth,
                   timestamp.wDay,
                   timestamp.wHour,
                   timestamp.wMinute,
                   timestamp.wSecond,
                   timestamp.wMilliseconds,
                   GetCurrentProcessId()) < 0) {
        return;
    }

    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    const int byteCount = WideCharToMultiByte(CP_UTF8,
                                               0,
                                               line.data(),
                                               static_cast<int>(line.size()),
                                               nullptr,
                                               0,
                                               nullptr,
                                               nullptr);
    if (byteCount <= 0) {
        return;
    }

    std::vector<char> utf8(static_cast<std::size_t>(byteCount));
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            line.data(),
                            static_cast<int>(line.size()),
                            utf8.data(),
                            byteCount,
                            nullptr,
                            nullptr) != byteCount) {
        return;
    }

    UniqueHandle log(CreateFileW(logPath.c_str(),
                                 FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE |
                                     FILE_SHARE_DELETE,
                                 nullptr,
                                 OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr));
    if (!log.valid()) {
        return;
    }

    DWORD written = 0;
    WriteFile(log.get(),
              utf8.data(),
              static_cast<DWORD>(utf8.size()),
              &written,
              nullptr);
}

class UniqueInternetHandle {
public:
    UniqueInternetHandle() = default;
    explicit UniqueInternetHandle(HINTERNET value) : value_(value) {}

    ~UniqueInternetHandle() {
        if (value_ != nullptr) {
            WinHttpCloseHandle(value_);
        }
    }

    UniqueInternetHandle(const UniqueInternetHandle&) = delete;
    UniqueInternetHandle& operator=(const UniqueInternetHandle&) = delete;

    [[nodiscard]] HINTERNET get() const {
        return value_;
    }

    [[nodiscard]] bool valid() const {
        return value_ != nullptr;
    }

private:
    HINTERNET value_ = nullptr;
};

class WinsockSession {
public:
    WinsockSession() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            Fail(L"Windows networking initialization failed.\n\nWinsock error " +
                 std::to_wstring(result));
        }
        active_ = true;
    }

    ~WinsockSession() {
        if (active_) {
            WSACleanup();
        }
    }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

private:
    bool active_ = false;
};

struct PackagePaths {
    fs::path root;
    fs::path data;
    fs::path logs;
    fs::path temp;
    fs::path qtExecutable;
    fs::path qtPlatformPlugin;
    fs::path backendJar;
    fs::path javaExecutable;
    fs::path cacPythonExecutable;
    fs::path vmtkPythonExecutable;
    fs::path localCli;
    fs::path recalculateScript;
    fs::path convertScript;
    fs::path vesselScript;
    fs::path segmentCacsRoot;
    fs::path model;
};

PackagePaths BuildPackagePaths(const fs::path& root) {
    PackagePaths paths{};
    paths.root = root;
    paths.data = root / L"data";
    paths.logs = paths.data / L"logs";
    paths.temp = paths.data / L"temp";
    paths.qtExecutable = root / L"app" / L"qt-cac-app.exe";
    paths.qtPlatformPlugin =
        root / L"app" / L"platforms" / L"qwindows.dll";
    paths.backendJar = root / L"backend" / L"cac-backend.jar";
    paths.javaExecutable =
        root / L"runtime" / L"jre" / L"bin" / L"java.exe";
    paths.cacPythonExecutable =
        root / L"runtime" / L"python-cac" / L"python.exe";
    paths.vmtkPythonExecutable =
        root / L"runtime" / L"python-vmtk" / L"python.exe";
    paths.localCli =
        root / L"inference" / L"scripts" / L"local_cac_cli.py";
    paths.recalculateScript =
        root / L"inference" / L"scripts" / L"recalculate_agatston.py";
    paths.convertScript =
        root / L"inference" / L"scripts" / L"convert_case_to_raw_volume.py";
    paths.vesselScript =
        root / L"inference" / L"vessel" / L"vmtk_straighten_vessel.py";
    paths.segmentCacsRoot = root / L"inference" / L"segment-cacs";
    paths.model = root / L"inference" / L"model" /
                  L"SegmentCACS_0001619_unet.pt";
    return paths;
}

fs::path ExecutableDirectory() {
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            Fail(WithLastError(L"Could not determine the launcher location."));
        }
        if (length < buffer.size() - 1) {
            return fs::path(std::wstring(buffer.data(), length)).parent_path();
        }
        if (buffer.size() >= 32768) {
            Fail(L"The launcher path is too long.");
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool IsRegularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && !error;
}

bool IsDirectory(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(path, error) && !error;
}

void ValidatePackage(const PackagePaths& paths) {
    struct Requirement {
        const wchar_t* label;
        const fs::path* path;
        bool directory;
    };

    const std::vector<Requirement> requirements{
        {L"Qt application", &paths.qtExecutable, false},
        {L"Qt Windows platform plugin", &paths.qtPlatformPlugin, false},
        {L"backend JAR", &paths.backendJar, false},
        {L"private Java runtime", &paths.javaExecutable, false},
        {L"CAC Python runtime", &paths.cacPythonExecutable, false},
        {L"VMTK Python runtime", &paths.vmtkPythonExecutable, false},
        {L"local inference CLI", &paths.localCli, false},
        {L"recalculation script", &paths.recalculateScript, false},
        {L"volume conversion script", &paths.convertScript, false},
        {L"vessel straightening script", &paths.vesselScript, false},
        {L"SEGMENT-CACS source", &paths.segmentCacsRoot, true},
        {L"SEGMENT-CACS checkpoint", &paths.model, false},
    };

    std::wostringstream missing;
    for (const Requirement& requirement : requirements) {
        const bool present = requirement.directory
                                 ? IsDirectory(*requirement.path)
                                 : IsRegularFile(*requirement.path);
        if (!present) {
            missing << L"\n- " << requirement.label << L": "
                    << requirement.path->wstring();
        }
    }

    if (!missing.str().empty()) {
        Fail(L"The portable package is incomplete. These required assets are "
             L"missing or unreadable:" +
             missing.str() +
             L"\n\nRe-extract the complete release ZIP; do not run the "
             L"launcher from inside the ZIP.");
    }
}

void CreateWritableDataDirectories(const PackagePaths& paths) {
    const std::vector<fs::path> directories{
        paths.data,
        paths.data / L"db",
        paths.data / L"jobs",
        paths.logs,
        paths.data / L"cache",
        paths.data / L"qt-cache",
        paths.temp,
        paths.data / L"vessel-straightening",
    };

    for (const fs::path& directory : directories) {
        std::error_code error;
        fs::create_directories(directory, error);
        if (error || !IsDirectory(directory)) {
            Fail(L"Could not create a writable portable data directory:\n\n" +
                 directory.wstring() +
                 L"\n\nExtract the package to a folder where your Windows "
                 L"account has write access.");
        }
    }

    const fs::path probe =
        paths.temp /
        (L".cac-launcher-write-test-" +
         std::to_wstring(GetCurrentProcessId()) + L".tmp");
    UniqueHandle writeProbe(CreateFileW(
        probe.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr));
    if (!writeProbe.valid()) {
        Fail(WithLastError(
            L"The package data directory is not writable:\n\n" +
            paths.data.wstring() +
            L"\n\nExtract the package to a user-writable folder."));
    }
}

void EnsureBackendPortIsAvailable() {
    WinsockSession winsock;
    const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        Fail(L"Could not create a TCP socket for the port 6006 preflight check.");
    }

    BOOL exclusive = TRUE;
    setsockopt(socketHandle,
               SOL_SOCKET,
               SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive),
               sizeof(exclusive));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kBackendPort);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(socketHandle,
             reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        closesocket(socketHandle);
        if (error == WSAEADDRINUSE || error == WSAEACCES) {
            Fail(L"Port 6006 is already occupied.\n\nClose the other CAC "
                 L"launcher/backend or the application using this port, then "
                 L"try again. No existing process was stopped.");
        }
        Fail(L"Could not verify that port 6006 is available.\n\nWinsock error " +
             std::to_wstring(error));
    }

    closesocket(socketHandle);
}

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    }
};

using EnvironmentMap =
    std::map<std::wstring, std::wstring, CaseInsensitiveLess>;

bool StartsWithIgnoreCase(const std::wstring& value,
                          const std::wstring& prefix) {
    return value.size() >= prefix.size() &&
           _wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) == 0;
}

EnvironmentMap CurrentEnvironment() {
    EnvironmentMap variables;
    wchar_t* block = GetEnvironmentStringsW();
    if (block == nullptr) {
        Fail(WithLastError(L"Could not read the Windows environment."));
    }

    for (const wchar_t* cursor = block; *cursor != L'\0';
         cursor += std::wcslen(cursor) + 1) {
        const std::wstring entry(cursor);
        const std::size_t separator = entry.find(L'=');
        // Entries beginning with '=' are cmd.exe drive-state pseudo variables.
        if (separator == std::wstring::npos || separator == 0) {
            continue;
        }
        variables[entry.substr(0, separator)] = entry.substr(separator + 1);
    }
    FreeEnvironmentStringsW(block);
    return variables;
}

std::wstring WindowsDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH + 1);
    const UINT length =
        GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        Fail(WithLastError(L"Could not determine the Windows directory."));
    }
    return std::wstring(buffer.data(), length);
}

std::vector<wchar_t> BuildChildEnvironment(const PackagePaths& paths) {
    EnvironmentMap variables = CurrentEnvironment();

    for (auto iterator = variables.begin(); iterator != variables.end();) {
        const std::wstring& name = iterator->first;
        if (StartsWithIgnoreCase(name, L"CAC_") ||
            StartsWithIgnoreCase(name, L"SEGMENT_CACS_") ||
            StartsWithIgnoreCase(name, L"SPRING_") ||
            StartsWithIgnoreCase(name, L"QT_")) {
            iterator = variables.erase(iterator);
        } else {
            ++iterator;
        }
    }

    const std::vector<std::wstring> externalRuntimeVariables{
        L"JAVA_HOME",
        L"JDK_HOME",
        L"JAVA_TOOL_OPTIONS",
        L"_JAVA_OPTIONS",
        L"JDK_JAVA_OPTIONS",
        L"PYTHONHOME",
        L"PYTHONPATH",
        L"VIRTUAL_ENV",
        L"CONDA_PREFIX",
        L"CONDA_DEFAULT_ENV",
        L"QTDIR",
        L"Qt6_DIR",
        L"VTK_DIR",
    };
    for (const std::wstring& name : externalRuntimeVariables) {
        variables.erase(name);
    }

    const fs::path appDirectory = paths.root / L"app";
    const fs::path javaBin = paths.root / L"runtime" / L"jre" / L"bin";
    const fs::path cacPython = paths.root / L"runtime" / L"python-cac";
    const fs::path vmtkPython = paths.root / L"runtime" / L"python-vmtk";
    const std::wstring windows = WindowsDirectory();

    const std::vector<fs::path> controlledPathEntries{
        appDirectory,
        javaBin,
        cacPython,
        cacPython / L"Scripts",
        vmtkPython,
        vmtkPython / L"Library" / L"bin",
        vmtkPython / L"Scripts",
        fs::path(windows) / L"System32",
        fs::path(windows),
    };

    std::wstring controlledPath;
    for (const fs::path& entry : controlledPathEntries) {
        if (!controlledPath.empty()) {
            controlledPath.push_back(L';');
        }
        controlledPath.append(entry.wstring());
    }

    variables[L"Path"] = controlledPath;
    variables[L"TEMP"] = paths.temp.wstring();
    variables[L"TMP"] = paths.temp.wstring();
    variables[L"PYTHONDONTWRITEBYTECODE"] = L"1";
    variables[L"PYTHONNOUSERSITE"] = L"1";
    variables[L"PYTHONUTF8"] = L"1";

    variables[L"CAC_PACKAGE_ROOT"] = paths.root.wstring();
    variables[L"CAC_DATA_ROOT"] = paths.data.wstring();
    variables[L"CAC_PYTHON_EXECUTABLE"] =
        paths.cacPythonExecutable.wstring();
    variables[L"CAC_VMTK_PYTHON_EXECUTABLE"] =
        paths.vmtkPythonExecutable.wstring();
    variables[L"CAC_VESSEL_STRAIGHTENING_SCRIPT"] =
        paths.vesselScript.wstring();
    variables[L"SEGMENT_CACS_ROOT"] = paths.segmentCacsRoot.wstring();
    variables[L"SEGMENT_CACS_MODEL_PATH"] = paths.model.wstring();
    variables[L"CAC_LOCAL_CLI_PATH"] = paths.localCli.wstring();
    variables[L"CAC_RECALCULATE_SCRIPT_PATH"] =
        paths.recalculateScript.wstring();
    variables[L"CAC_CONVERT_SCRIPT_PATH"] = paths.convertScript.wstring();
    variables[L"CAC_INFERENCE_DEVICE"] = L"cpu";
    variables[L"CAC_CPU_ONLY_RELEASE"] = L"1";
    variables[L"CAC_BACKEND_PORT"] = L"6006";
    variables[L"CAC_BACKEND_URL"] = L"http://127.0.0.1:6006";
    variables[L"CAC_WEBSOCKET_URL"] =
        L"ws://127.0.0.1:6006/ws/jobs";
    variables[L"SPRING_PROFILES_ACTIVE"] = L"local-windows";
    variables[L"QT_PLUGIN_PATH"] = appDirectory.wstring();
    variables[L"QT_QPA_PLATFORM_PLUGIN_PATH"] =
        (appDirectory / L"platforms").wstring();

    std::vector<wchar_t> block;
    for (const auto& [name, value] : variables) {
        block.insert(block.end(), name.begin(), name.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::wstring QuoteCommandLineArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes =
        argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return argument;
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::vector<wchar_t> BuildCommandLine(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments) {
    std::wstring command = QuoteCommandLineArgument(executable.wstring());
    for (const std::wstring& argument : arguments) {
        command.push_back(L' ');
        command.append(QuoteCommandLineArgument(argument));
    }

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    return mutableCommand;
}

UniqueHandle OpenBackendLog(const fs::path& logPath) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    UniqueHandle log(CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!log.valid()) {
        Fail(WithLastError(L"Could not open the backend log:\n\n" +
                           logPath.wstring()));
    }
    return log;
}

struct BackendProcess {
    UniqueHandle process;
    UniqueHandle job;
    DWORD pid = 0;
};

BackendProcess StartBackend(const PackagePaths& paths,
                            std::vector<wchar_t>& environment,
                            HANDLE logHandle) {
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.valid()) {
        Fail(WithLastError(L"Could not create the backend lifecycle Job Object."));
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
    jobLimits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(),
                                 JobObjectExtendedLimitInformation,
                                 &jobLimits,
                                 sizeof(jobLimits))) {
        Fail(WithLastError(
            L"Could not configure safe backend lifecycle management."));
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    UniqueHandle nullInput(CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!nullInput.valid()) {
        Fail(WithLastError(L"Could not open the backend's null input stream."));
    }

    std::vector<std::wstring> arguments{
        L"-jar",
        paths.backendJar.wstring(),
        L"--spring.profiles.active=local-windows",
        L"--server.address=127.0.0.1",
        L"--server.port=6006",
        L"--management.endpoint.shutdown.enabled=true",
        L"--management.endpoints.web.exposure.include=health,shutdown",
        L"--spring.lifecycle.timeout-per-shutdown-phase=20s",
    };
    std::vector<wchar_t> command =
        BuildCommandLine(paths.javaExecutable, arguments);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nullInput.get();
    startup.hStdOutput = logHandle;
    startup.hStdError = logHandle;

    PROCESS_INFORMATION processInfo{};
    const DWORD creationFlags =
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(paths.javaExecutable.c_str(),
                        command.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        creationFlags,
                        environment.data(),
                        paths.root.c_str(),
                        &startup,
                        &processInfo)) {
        Fail(WithLastError(
            L"Could not start the private Java backend:\n\n" +
            paths.javaExecutable.wstring()));
    }

    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);

    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const DWORD error = GetLastError();
        TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process.get(), 5000);
        Fail(WithLastError(
            L"Could not attach the backend to its private lifecycle Job Object.",
            error));
    }

    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process.get(), 5000);
        Fail(WithLastError(L"Could not resume the private Java backend.", error));
    }

    BackendProcess backend{};
    backend.process = std::move(process);
    backend.job = std::move(job);
    backend.pid = processInfo.dwProcessId;
    return backend;
}

UniqueHandle StartQt(const PackagePaths& paths,
                     std::vector<wchar_t>& environment) {
    std::vector<wchar_t> command =
        BuildCommandLine(paths.qtExecutable, {});

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(paths.qtExecutable.c_str(),
                        command.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_UNICODE_ENVIRONMENT,
                        environment.data(),
                        paths.root.c_str(),
                        &startup,
                        &processInfo)) {
        Fail(WithLastError(
            L"The CAC user interface could not start:\n\n" +
            paths.qtExecutable.wstring() +
            L"\n\nThe package may be missing a Qt, VTK, graphics, or VC runtime "
            L"dependency."));
    }

    CloseHandle(processInfo.hThread);
    return UniqueHandle(processInfo.hProcess);
}

struct HttpResult {
    bool transportSucceeded = false;
    DWORD statusCode = 0;
    std::string body;
};

HttpResult LocalHttpRequest(const wchar_t* method, const wchar_t* objectName) {
    HttpResult result{};
    UniqueInternetHandle session(WinHttpOpen(
        L"CACLauncher/0.6.10",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.valid()) {
        return result;
    }
    WinHttpSetTimeouts(session.get(), 1000, 1000, 1500, 1500);

    UniqueInternetHandle connection(
        WinHttpConnect(session.get(), L"127.0.0.1", kBackendPort, 0));
    if (!connection.valid()) {
        return result;
    }

    UniqueInternetHandle request(WinHttpOpenRequest(
        connection.get(),
        method,
        objectName,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0));
    if (!request.valid()) {
        return result;
    }

    if (!WinHttpSendRequest(request.get(),
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return result;
    }

    DWORD statusSize = sizeof(result.statusCode);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &result.statusCode,
                             &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        return result;
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            return result;
        }
        if (available == 0) {
            break;
        }
        if (result.body.size() + available > 1024 * 1024) {
            return result;
        }

        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(
                request.get(), buffer.data(), available, &read)) {
            return result;
        }
        result.body.append(buffer.data(), read);
    }

    result.transportSucceeded = true;
    return result;
}

bool HealthIsUp() {
    const HttpResult result =
        LocalHttpRequest(L"GET", L"/actuator/health");
    if (!result.transportSucceeded || result.statusCode != 200) {
        return false;
    }

    std::string compact;
    compact.reserve(result.body.size());
    std::copy_if(result.body.begin(),
                 result.body.end(),
                 std::back_inserter(compact),
                 [](unsigned char character) {
                     return std::isspace(character) == 0;
                 });
    return compact.find("\"status\":\"UP\"") != std::string::npos;
}

DWORD ExitCode(HANDLE process) {
    DWORD code = STILL_ACTIVE;
    if (!GetExitCodeProcess(process, &code)) {
        return static_cast<DWORD>(-1);
    }
    return code;
}

void WaitForBackendHealth(const BackendProcess& backend,
                          const fs::path& logPath) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(kBackendStartupTimeoutSeconds);

    while (std::chrono::steady_clock::now() < deadline) {
        if (WaitForSingleObject(backend.process.get(), 0) == WAIT_OBJECT_0) {
            Fail(L"The local backend exited before it became ready.\n\nPID: " +
                 std::to_wstring(backend.pid) +
                 L"\nExit code: " +
                 std::to_wstring(ExitCode(backend.process.get())) +
                 L"\nLog: " + logPath.wstring());
        }
        if (HealthIsUp()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    Fail(L"The local backend did not become healthy within " +
         std::to_wstring(kBackendStartupTimeoutSeconds) +
         L" seconds.\n\nPID: " + std::to_wstring(backend.pid) +
         L"\nHealth URL: http://127.0.0.1:6006/actuator/health"
         L"\nLog: " +
         logPath.wstring());
}

bool IsProcessRunning(HANDLE process) {
    return process != nullptr &&
           WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

// Returns true only when last-resort termination was required.
bool StopBackend(BackendProcess& backend) {
    if (!backend.process.valid() ||
        !IsProcessRunning(backend.process.get())) {
        return false;
    }

    // The shutdown endpoint is enabled only on this loopback-bound child through
    // its command line. A transport failure can be the expected result of the
    // server closing the connection while shutting down.
    LocalHttpRequest(L"POST", L"/actuator/shutdown");

    if (WaitForSingleObject(backend.process.get(),
                            kBackendShutdownTimeoutMilliseconds) ==
        WAIT_OBJECT_0) {
        return false;
    }

    if (!TerminateProcess(backend.process.get(), ERROR_PROCESS_ABORTED)) {
        // Closing backend.job still applies JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
        return true;
    }
    WaitForSingleObject(backend.process.get(), 5000);
    return true;
}

void StopBackendAfterFailure(BackendProcess& backend) noexcept {
    if (!backend.process.valid() ||
        !IsProcessRunning(backend.process.get())) {
        return;
    }
    try {
        LocalHttpRequest(L"POST", L"/actuator/shutdown");
    } catch (...) {
        // Failure cleanup must continue to the exact child handle.
    }
    if (WaitForSingleObject(backend.process.get(), 5000) == WAIT_TIMEOUT) {
        TerminateProcess(backend.process.get(), ERROR_PROCESS_ABORTED);
        WaitForSingleObject(backend.process.get(), 5000);
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
    BackendProcess backend{};
    fs::path backendLog;
    fs::path launcherLog;

    try {
        SetLastError(ERROR_SUCCESS);
        UniqueHandle mutex(CreateMutexW(nullptr, FALSE, kMutexName));
        const DWORD mutexResult = GetLastError();
        if (!mutex.valid()) {
            Fail(WithLastError(
                L"Could not create the single-instance launcher mutex."));
        }
        if (mutexResult == ERROR_ALREADY_EXISTS) {
            ShowError(L"CAC Platform is already being launched.\n\nClose the "
                      L"existing application before starting another copy.");
            return 2;
        }

        const fs::path packageRoot = ExecutableDirectory();
        const PackagePaths paths = BuildPackagePaths(packageRoot);
        ValidatePackage(paths);
        CreateWritableDataDirectories(paths);
        launcherLog = paths.logs / L"launcher.log";
        AppendLauncherLog(launcherLog,
                          L"Launcher started from package root: " +
                              paths.root.wstring());
        EnsureBackendPortIsAvailable();

        std::vector<wchar_t> environment = BuildChildEnvironment(paths);
        backendLog = paths.logs / L"backend.log";
        UniqueHandle log = OpenBackendLog(backendLog);

        backend = StartBackend(paths, environment, log.get());
        AppendLauncherLog(launcherLog,
                          L"Private backend started with PID " +
                              std::to_wstring(backend.pid) + L".");
        SetHandleInformation(log.get(), HANDLE_FLAG_INHERIT, 0);
        WaitForBackendHealth(backend, backendLog);
        AppendLauncherLog(launcherLog, L"Backend health is UP.");

        UniqueHandle qtProcess = StartQt(paths, environment);
        AppendLauncherLog(
            launcherLog,
            L"Qt application started with PID " +
                std::to_wstring(GetProcessId(qtProcess.get())) + L".");
        HANDLE waits[]{qtProcess.get(), backend.process.get()};
        const DWORD waitResult =
            WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        AppendLauncherLog(launcherLog,
                          L"Child wait completed with result " +
                              std::to_wstring(waitResult) + L".");

        bool backendExitedUnexpectedly = false;
        if (waitResult == WAIT_OBJECT_0 + 1) {
            backendExitedUnexpectedly = true;
            AppendLauncherLog(
                launcherLog,
                L"Backend exited unexpectedly with code " +
                    std::to_wstring(ExitCode(backend.process.get())) + L".");
            ShowError(
                L"The local backend stopped while the CAC window was open.\n\n"
                L"Backend PID: " +
                std::to_wstring(backend.pid) +
                L"\nExit code: " +
                std::to_wstring(ExitCode(backend.process.get())) +
                L"\nLog: " + backendLog.wstring() +
                L"\n\nClose the CAC window after reviewing this message.");
            WaitForSingleObject(qtProcess.get(), INFINITE);
        } else if (waitResult == WAIT_FAILED) {
            Fail(WithLastError(
                L"Could not wait for the CAC application to close."));
        }

        const DWORD qtExitCode = ExitCode(qtProcess.get());
        AppendLauncherLog(launcherLog,
                          L"Qt exit code is " +
                              std::to_wstring(qtExitCode) + L".");
        const bool forcedBackendStop =
            backendExitedUnexpectedly ? false : StopBackend(backend);
        AppendLauncherLog(
            launcherLog,
            forcedBackendStop
                ? L"Backend required exact-child forced termination."
                : L"Backend stopped without exact-child forced termination.");

        if (forcedBackendStop) {
            AppendLauncherLog(launcherLog,
                              L"Displaying forced-shutdown warning.");
            ShowWarning(
                L"The backend did not finish graceful shutdown in time, so "
                L"the launcher terminated only its own backend child process.\n\n"
                L"Log: " +
                backendLog.wstring());
        }
        if (qtExitCode != 0) {
            AppendLauncherLog(launcherLog,
                              L"Displaying nonzero Qt exit warning.");
            ShowWarning(L"The CAC user interface exited with code " +
                        std::to_wstring(qtExitCode) +
                        L".\n\nLog: " + backendLog.wstring());
        }
        AppendLauncherLog(launcherLog, L"Launcher exiting.");
        return backendExitedUnexpectedly || qtExitCode != 0 ? 1 : 0;
    } catch (const LauncherError& error) {
        StopBackendAfterFailure(backend);
        if (!launcherLog.empty()) {
            AppendLauncherLog(launcherLog,
                              L"Launcher error: " + error.message);
        }
        std::wstring message = error.message;
        if (!backendLog.empty()) {
            message += L"\n\nBackend log: " + backendLog.wstring();
        }
        ShowError(message);
        return 1;
    } catch (...) {
        StopBackendAfterFailure(backend);
        if (!launcherLog.empty()) {
            AppendLauncherLog(launcherLog,
                              L"Unexpected launcher exception.");
        }
        ShowError(L"An unexpected launcher error occurred. No unrelated "
                  L"Java or Python process was stopped.");
        return 1;
    }
}
