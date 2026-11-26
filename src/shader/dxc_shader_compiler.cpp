#include "shader/dxc_shader_compiler.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace raytime::shader {
namespace {

constexpr std::uint64_t maxSourceSize = 16U * 1024U * 1024U;
constexpr std::uint64_t maxDxilSize = 256U * 1024U * 1024U;
constexpr std::size_t maxDiagnosticBytes = 1024U * 1024U;
std::atomic_uint64_t tempFileCounter = 0;

bool setWindowsError(core::Error& error, const char* operation, DWORD code) {
    error.message = std::string(operation) + " failed with Windows error " +
                    std::to_string(code) + ".";
    error.resultCode = static_cast<long>(HRESULT_FROM_WIN32(code));
    return false;
}

bool setNtError(core::Error& error, const char* operation, NTSTATUS status) {
    std::ostringstream stream;
    stream << operation << " failed (NTSTATUS 0x" << std::hex
           << static_cast<unsigned long>(status) << ").";
    error.message = stream.str();
    error.resultCode = static_cast<long>(status);
    return false;
}

class ScopedHandle final {
public:
    explicit ScopedHandle(HANDLE handle = nullptr) noexcept : m_handle(handle) {}
    ~ScopedHandle() {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return m_handle;
    }

    void reset() noexcept {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
        m_handle = nullptr;
    }

private:
    HANDLE m_handle;
};

class ScopedPath final {
public:
    explicit ScopedPath(std::filesystem::path path) : m_path(std::move(path)) {}
    ~ScopedPath() {
        if (m_remove) {
            std::error_code ignored;
            std::filesystem::remove(m_path, ignored);
        }
    }

    ScopedPath(const ScopedPath&) = delete;
    ScopedPath& operator=(const ScopedPath&) = delete;

    void release() noexcept {
        m_remove = false;
    }

private:
    std::filesystem::path m_path;
    bool m_remove = true;
};

class ScopedProcessAttributes final {
public:
    ScopedProcessAttributes() = default;

    ~ScopedProcessAttributes() {
        if (m_initialized) {
            DeleteProcThreadAttributeList(m_attributes);
        }
    }

    ScopedProcessAttributes(const ScopedProcessAttributes&) = delete;
    ScopedProcessAttributes& operator=(const ScopedProcessAttributes&) = delete;

    [[nodiscard]] bool initialize(
        std::span<const HANDLE> inheritedHandles,
        core::Error& error) {
        SIZE_T storageSize = 0;
        const BOOL queryResult = InitializeProcThreadAttributeList(
            nullptr, 1, 0, &storageSize);
        const DWORD queryError = queryResult ? ERROR_SUCCESS : GetLastError();
        if (queryResult || queryError != ERROR_INSUFFICIENT_BUFFER) {
            return setWindowsError(
                error,
                "InitializeProcThreadAttributeList(size)",
                queryResult ? ERROR_INVALID_DATA : queryError);
        }

        m_storage.resize(
            (storageSize + sizeof(std::max_align_t) - 1) /
            sizeof(std::max_align_t));
        m_attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            m_storage.data());
        if (!InitializeProcThreadAttributeList(
                m_attributes, 1, 0, &storageSize)) {
            return setWindowsError(
                error, "InitializeProcThreadAttributeList", GetLastError());
        }
        m_initialized = true;
        if (!UpdateProcThreadAttribute(
                m_attributes,
                0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE*>(inheritedHandles.data()),
                inheritedHandles.size_bytes(),
                nullptr,
                nullptr)) {
            return setWindowsError(
                error, "UpdateProcThreadAttribute(HANDLE_LIST)", GetLastError());
        }
        return true;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return m_attributes;
    }

private:
    std::vector<std::max_align_t> m_storage;
    LPPROC_THREAD_ATTRIBUTE_LIST m_attributes = nullptr;
    bool m_initialized = false;
};

class Sha256 final {
public:
    ~Sha256() {
        if (m_hash != nullptr) {
            BCryptDestroyHash(m_hash);
        }
        if (m_algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(m_algorithm, 0);
        }
    }

    [[nodiscard]] bool initialize(core::Error& error) {
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &m_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0) {
            return setNtError(error, "BCryptOpenAlgorithmProvider(SHA-256)", status);
        }

        ULONG objectLength = 0;
        ULONG resultLength = 0;
        status = BCryptGetProperty(
            m_algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength),
            &resultLength,
            0);
        if (status < 0) {
            return setNtError(error, "BCryptGetProperty", status);
        }
        m_hashObject.resize(objectLength);
        status = BCryptCreateHash(
            m_algorithm,
            &m_hash,
            m_hashObject.data(),
            objectLength,
            nullptr,
            0,
            0);
        if (status < 0) {
            return setNtError(error, "BCryptCreateHash", status);
        }
        return true;
    }

    [[nodiscard]] bool append(
        const void* data,
        std::size_t size,
        core::Error& error) {
        if (size > std::numeric_limits<ULONG>::max()) {
            error.message = "Shader cache key component exceeds the SHA-256 input limit.";
            return false;
        }
        const NTSTATUS status = BCryptHashData(
            m_hash,
            static_cast<PUCHAR>(const_cast<void*>(data)),
            static_cast<ULONG>(size),
            0);
        if (status < 0) {
            return setNtError(error, "BCryptHashData", status);
        }
        return true;
    }

    [[nodiscard]] bool appendField(
        const void* data,
        std::size_t size,
        core::Error& error) {
        const std::uint64_t fieldSize = size;
        return append(&fieldSize, sizeof(fieldSize), error) &&
               append(data, size, error);
    }

    [[nodiscard]] bool finish(std::array<UCHAR, 32>& digest, core::Error& error) {
        const NTSTATUS status = BCryptFinishHash(
            m_hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        if (status < 0) {
            return setNtError(error, "BCryptFinishHash", status);
        }
        return true;
    }

private:
    BCRYPT_ALG_HANDLE m_algorithm = nullptr;
    BCRYPT_HASH_HANDLE m_hash = nullptr;
    std::vector<UCHAR> m_hashObject;
};

std::wstring quoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

bool runProcess(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    DWORD& exitCode,
    std::string& output,
    core::Error& error) {
    std::wstring commandLine = quoteArgument(executable.wstring());
    for (const auto& argument : arguments) {
        commandLine.push_back(L' ');
        commandLine += quoteArgument(argument);
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE rawReadPipe = nullptr;
    HANDLE rawWritePipe = nullptr;
    if (!CreatePipe(&rawReadPipe, &rawWritePipe, &security, 0)) {
        return setWindowsError(error, "CreatePipe", GetLastError());
    }
    ScopedHandle readPipe(rawReadPipe);
    ScopedHandle writePipe(rawWritePipe);
    if (!SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0)) {
        return setWindowsError(error, "SetHandleInformation", GetLastError());
    }

    ScopedHandle nullInput(CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (nullInput.get() == INVALID_HANDLE_VALUE) {
        return setWindowsError(error, "CreateFileW(NUL)", GetLastError());
    }

    const std::array inheritedHandles{nullInput.get(), writePipe.get()};
    ScopedProcessAttributes processAttributes;
    if (!processAttributes.initialize(inheritedHandles, error)) {
        return false;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullInput.get();
    startup.StartupInfo.hStdOutput = writePipe.get();
    startup.StartupInfo.hStdError = writePipe.get();
    startup.lpAttributeList = processAttributes.get();

    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(
            executable.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            nullptr,
            &startup.StartupInfo,
            &processInfo)) {
        return setWindowsError(error, "CreateProcessW(DXC)", GetLastError());
    }
    ScopedHandle process(processInfo.hProcess);
    ScopedHandle thread(processInfo.hThread);
    writePipe.reset();
    nullInput.reset();

    std::array<char, 4096> buffer{};
    bool truncated = false;
    DWORD readFailure = ERROR_SUCCESS;
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(
                readPipe.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                nullptr)) {
            const DWORD readError = GetLastError();
            if (readError == ERROR_BROKEN_PIPE) {
                break;
            }
            readFailure = readError;
            readPipe.reset();
            break;
        }
        if (bytesRead == 0) {
            break;
        }

        const std::size_t remaining =
            output.size() < maxDiagnosticBytes
                ? maxDiagnosticBytes - output.size()
                : 0;
        const std::size_t stored = std::min<std::size_t>(remaining, bytesRead);
        output.append(buffer.data(), stored);
        truncated = truncated || stored < bytesRead;
    }
    if (truncated) {
        output += "\n[RayTime] DXC diagnostic output truncated.\n";
    }

    if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
        return setWindowsError(error, "WaitForSingleObject(DXC)", GetLastError());
    }
    if (!GetExitCodeProcess(process.get(), &exitCode)) {
        return setWindowsError(error, "GetExitCodeProcess(DXC)", GetLastError());
    }
    if (readFailure != ERROR_SUCCESS) {
        return setWindowsError(error, "ReadFile(DXC output)", readFailure);
    }
    return true;
}

bool isValidEntryPoint(const std::string& entryPoint) {
    if (entryPoint.empty()) {
        return false;
    }
    const auto isStart = [](char character) {
        return (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') ||
               character == '_';
    };
    const auto isPart = [&](char character) {
        return isStart(character) || (character >= '0' && character <= '9');
    };
    if (!isStart(entryPoint.front())) {
        return false;
    }
    return std::all_of(entryPoint.begin() + 1, entryPoint.end(), isPart);
}

const wchar_t* targetProfile(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return L"vs_6_0";
    case ShaderStage::Pixel:
        return L"ps_6_0";
    case ShaderStage::Compute:
        return L"cs_6_0";
    }
    return nullptr;
}

bool hasIncludeDirective(std::string_view source) {
    std::size_t lineStart = 0;
    while (lineStart < source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::size_t end =
            lineEnd == std::string_view::npos ? source.size() : lineEnd;
        std::size_t cursor = lineStart;
        while (cursor < end &&
               std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }
        if (cursor < end && source[cursor] == '#') {
            ++cursor;
            while (cursor < end &&
                   std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
                ++cursor;
            }
            constexpr std::string_view include = "include";
            if (source.substr(cursor, include.size()) == include) {
                const std::size_t afterKeyword = cursor + include.size();
                if (afterKeyword == end ||
                    (std::isalnum(static_cast<unsigned char>(
                         source[afterKeyword])) == 0 &&
                     source[afterKeyword] != '_')) {
                    return true;
                }
            }
        }
        lineStart = lineEnd == std::string_view::npos ? source.size() : lineEnd + 1;
    }
    return false;
}

bool readFile(
    const std::filesystem::path& path,
    std::uint64_t maxSize,
    std::vector<std::byte>& data,
    core::Error& error) {
    std::error_code fileError;
    const std::uint64_t size = std::filesystem::file_size(path, fileError);
    if (fileError) {
        error.message = "Could not read file size for '" + path.string() +
                        "': " + fileError.message();
        return false;
    }
    if (size == 0 || size > maxSize ||
        size > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        error.message = "File is empty or exceeds the supported size limit: " +
                        path.string();
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error.message = "Could not open file: " + path.string();
        return false;
    }
    data.resize(static_cast<std::size_t>(size));
    file.read(
        reinterpret_cast<char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    if (!file || static_cast<std::uint64_t>(file.gcount()) != size) {
        error.message = "Could not read the complete file: " + path.string();
        return false;
    }
    return true;
}

bool hasDxilContainerSignature(const std::vector<std::byte>& data) {
    return data.size() >= 4 &&
           std::memcmp(data.data(), "DXBC", 4) == 0;
}

std::wstring defaultCacheDirectory() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring localAppData(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(), required);
    if (copied == 0 || copied >= required) {
        return {};
    }
    localAppData.resize(copied);
    return (std::filesystem::path(localAppData) / L"RayTime" / L"ShaderCache")
        .wstring();
}

bool makeCacheKey(
    const std::filesystem::path& sourcePath,
    const std::vector<std::byte>& source,
    const ShaderCompileDescription& description,
    const std::wstring& compilerFingerprint,
    std::string& key,
    core::Error& error) {
    const wchar_t* profile = targetProfile(description.stage);
    if (profile == nullptr) {
        error.message = "Unsupported shader stage.";
        return false;
    }

    Sha256 hasher;
    if (!hasher.initialize(error)) {
        return false;
    }
    constexpr char cacheFormat[] = "RayTime-DXIL-cache-v1";
    const std::wstring sourcePathString = sourcePath.native();
    std::string profileString;
    for (const wchar_t character : std::wstring_view(profile)) {
        profileString.push_back(static_cast<char>(character));
    }

    if (!hasher.appendField(cacheFormat, sizeof(cacheFormat), error) ||
        !hasher.appendField(
            sourcePathString.data(),
            sourcePathString.size() * sizeof(wchar_t),
            error) ||
        !hasher.appendField(source.data(), source.size(), error) ||
        !hasher.appendField(
            description.entryPoint.data(), description.entryPoint.size(), error) ||
        !hasher.appendField(
            profileString.data(), profileString.size(), error) ||
        !hasher.appendField(
            compilerFingerprint.data(),
            compilerFingerprint.size() * sizeof(wchar_t),
            error)) {
        return false;
    }

    const std::uint8_t debugFlag = description.debugInformation ? 1 : 0;
    if (!hasher.appendField(&debugFlag, sizeof(debugFlag), error)) {
        return false;
    }
    for (const auto& directory : description.includeDirectories) {
        const std::wstring includePath = directory.native();
        if (!hasher.appendField(
                includePath.data(), includePath.size() * sizeof(wchar_t), error)) {
            return false;
        }
    }

    std::array<UCHAR, 32> digest{};
    if (!hasher.finish(digest, error)) {
        return false;
    }
    constexpr char hexDigits[] = "0123456789abcdef";
    key.resize(digest.size() * 2);
    for (std::size_t index = 0; index < digest.size(); ++index) {
        key[index * 2] = hexDigits[digest[index] >> 4];
        key[index * 2 + 1] = hexDigits[digest[index] & 0x0f];
    }
    return true;
}

} // namespace

bool DxcShaderCompiler::initialize(
    const std::filesystem::path& dxcExecutable,
    const std::filesystem::path& cacheDirectory,
    core::Error& error) {
    error = {};
    if (dxcExecutable.empty()) {
        std::array<wchar_t, 32768> pathBuffer{};
        const DWORD length = SearchPathW(
            nullptr, L"dxc.exe", nullptr,
            static_cast<DWORD>(pathBuffer.size()), pathBuffer.data(), nullptr);
        if (length == 0 || length >= pathBuffer.size()) {
            error.message =
                "DXC_NOT_FOUND: dxc.exe was not found on PATH; pass --dxc <path>.";
            error.resultCode = dxcUnavailableResultCode;
            return false;
        }
        m_dxcExecutable = pathBuffer.data();
    } else {
        m_dxcExecutable = dxcExecutable;
    }

    const DWORD attributes = GetFileAttributesW(m_dxcExecutable.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD fileError = GetLastError();
        error.message = "DXC executable does not exist or is not a file: " +
                        m_dxcExecutable.string();
        error.resultCode = static_cast<long>(HRESULT_FROM_WIN32(fileError));
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        error.message = "DXC executable path points to a directory: " +
                        m_dxcExecutable.string();
        error.resultCode =
            static_cast<long>(HRESULT_FROM_WIN32(ERROR_INVALID_NAME));
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA compilerFile{};
    if (!GetFileAttributesExW(
            m_dxcExecutable.c_str(), GetFileExInfoStandard, &compilerFile)) {
        return setWindowsError(
            error, "GetFileAttributesExW(DXC)", GetLastError());
    }

    std::error_code pathError;
    m_dxcExecutable = std::filesystem::absolute(m_dxcExecutable, pathError);
    if (pathError) {
        error.message = "Could not resolve the DXC executable path: " +
                        pathError.message();
        return false;
    }

    m_compilerFingerprint =
        m_dxcExecutable.native() + L"|" +
        std::to_wstring(compilerFile.nFileSizeHigh) + L"|" +
        std::to_wstring(compilerFile.nFileSizeLow) + L"|" +
        std::to_wstring(compilerFile.ftLastWriteTime.dwHighDateTime) + L"|" +
        std::to_wstring(compilerFile.ftLastWriteTime.dwLowDateTime);

    if (cacheDirectory.empty()) {
        m_cacheDirectory = defaultCacheDirectory();
        if (m_cacheDirectory.empty()) {
            error.message =
                "LOCALAPPDATA is unavailable; pass --cache-dir <path>.";
            return false;
        }
    } else {
        m_cacheDirectory = cacheDirectory;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(m_cacheDirectory, directoryError);
    if (directoryError) {
        error.message = "Could not create shader cache directory '" +
                        m_cacheDirectory.string() + "': " +
                        directoryError.message();
        return false;
    }
    return true;
}

bool DxcShaderCompiler::compile(
    const ShaderCompileDescription& description,
    CompiledShader& output,
    core::Error& error) const {
    error = {};
    output = {};
    if (m_dxcExecutable.empty() || m_cacheDirectory.empty()) {
        error.message = "Initialize the DXC shader compiler before compiling.";
        return false;
    }
    if (!isValidEntryPoint(description.entryPoint)) {
        error.message = "Shader entry point must be a non-empty ASCII identifier.";
        return false;
    }
    const wchar_t* profile = targetProfile(description.stage);
    if (profile == nullptr) {
        error.message = "Unsupported shader stage.";
        return false;
    }

    std::error_code sourcePathError;
    const auto sourcePath =
        std::filesystem::absolute(description.sourcePath, sourcePathError);
    if (sourcePathError) {
        error.message = "Could not resolve shader source path: " +
                        sourcePathError.message();
        return false;
    }

    std::vector<std::byte> source;
    if (!readFile(sourcePath, maxSourceSize, source, error)) {
        return false;
    }

    bool cacheable = true;
    const std::string_view sourceText(
        reinterpret_cast<const char*>(source.data()), source.size());
    if (hasIncludeDirective(sourceText)) {
        cacheable = false;
    }

    std::string key;
    if (!makeCacheKey(
            sourcePath, source, description, m_compilerFingerprint, key, error)) {
        return false;
    }
    const auto cachePath = m_cacheDirectory / (key + ".dxil");
    if (cacheable) {
        std::error_code cacheError;
        if (std::filesystem::exists(cachePath, cacheError)) {
            if (cacheError) {
                error.message = "Could not inspect shader cache: " +
                                cacheError.message();
                return false;
            }
            if (!readFile(cachePath, maxDxilSize, output.dxil, error)) {
                error.message = "Invalid shader cache entry: " + error.message;
                return false;
            }
            if (!hasDxilContainerSignature(output.dxil)) {
                output = {};
                error.message = "Shader cache entry is not a DXIL container: " +
                                cachePath.string();
                return false;
            }
            output.cacheHit = true;
            return true;
        }
        if (cacheError) {
            error.message = "Could not inspect shader cache: " +
                            cacheError.message();
            return false;
        }
    }

    const auto counter = tempFileCounter.fetch_add(1, std::memory_order_relaxed);
    const auto temporaryPath = m_cacheDirectory /
        (key + "." + std::to_string(GetCurrentProcessId()) + "." +
         std::to_string(counter) + ".tmp.dxil");
    ScopedPath temporaryFile(temporaryPath);

    std::vector<std::wstring> arguments{
        L"-nologo",
        L"-Ges",
        L"-E",
        std::wstring(description.entryPoint.begin(), description.entryPoint.end()),
        L"-T",
        profile,
        L"-Fo",
        temporaryPath.wstring(),
    };
    if (description.debugInformation) {
        arguments.emplace_back(L"-Zi");
        arguments.emplace_back(L"-Qembed_debug");
        arguments.emplace_back(L"-Od");
    }
    for (const auto& includeDirectory : description.includeDirectories) {
        arguments.emplace_back(L"-I");
        arguments.push_back(includeDirectory.wstring());
    }
    arguments.push_back(sourcePath.wstring());

    DWORD exitCode = 0;
    if (!runProcess(
            m_dxcExecutable, arguments, exitCode, output.diagnostics, error)) {
        return false;
    }
    if (exitCode != 0) {
        error.message = "DXC compilation failed with exit code " +
                        std::to_string(exitCode) + ".";
        if (!output.diagnostics.empty()) {
            error.message += "\n" + output.diagnostics;
        }
        return false;
    }

    if (!readFile(temporaryPath, maxDxilSize, output.dxil, error)) {
        error.message = "DXC reported success but produced no valid output: " +
                        error.message;
        return false;
    }
    if (!hasDxilContainerSignature(output.dxil)) {
        output = {};
        error.message = "DXC output does not have a DXIL container signature.";
        return false;
    }

    if (cacheable) {
        if (MoveFileExW(
                temporaryPath.c_str(),
                cachePath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            temporaryFile.release();
        } else {
            output.diagnostics +=
                "\n[RayTime] Shader compiled, but the cache entry could not be "
                "written (Windows error " +
                std::to_string(GetLastError()) + ").\n";
        }
    }
    return true;
}

bool validateShaderCache(
    const DxcShaderCompiler& compiler,
    const ShaderCompileDescription& description,
    core::Error& error) {
    CompiledShader first;
    if (!compiler.compile(description, first, error)) {
        return false;
    }
    if (!first.diagnostics.empty()) {
        std::clog << first.diagnostics;
    }

    CompiledShader second;
    if (!compiler.compile(description, second, error)) {
        return false;
    }
    if (!second.cacheHit) {
        error.message =
            "The second compile did not use the expected shader cache entry.";
        return false;
    }
    if (first.dxil != second.dxil) {
        error.message =
            "Cached DXIL bytes differ from the original shader compilation.";
        return false;
    }
    return true;
}

} // namespace raytime::shader
