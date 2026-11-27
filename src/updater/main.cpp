// Zero-Qt, zero-network Win32 updater helper for AnimeHub Desktop.
// Applies a verified staging plan, monitors post-update startup health via a Win32 Named Event,
// and rolls back both binaries and SQLite library backup automatically on crash or timeout.

#include <windows.h>

#include <bcrypt.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr DWORD kDefaultParentWaitMs = 15'000;
constexpr DWORD kDefaultHealthWaitMs = 15'000;
constexpr int kFileOpRetries = 30;
constexpr DWORD kFileOpRetryDelayMs = 100;
constexpr wchar_t kInnoUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                                        L"{D47C98AF-073F-4E55-A850-C8AACADF48F6}_is1";

// --- UTF-8 <-> UTF-16 helpers ---

std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                        needed);
    return wide;
}

std::string wideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), needed,
                        nullptr, nullptr);
    return utf8;
}

std::string toLowerAscii(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

bool startsWithCaseInsensitive(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto a = static_cast<unsigned char>(text[i]);
        const auto b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

bool equalsCaseInsensitive(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// --- Strict path validation (defense in depth) ---

bool isSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 512 || path.front() == '/' || path.back() == '/') {
        return false;
    }
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    std::size_t start = 0;
    bool first = true;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view seg = slash == std::string_view::npos
                                         ? path.substr(start)
                                         : path.substr(start, slash - start);
        if (seg.empty() || seg == "." || seg == ".." || seg.back() == ' ' || seg.back() == '.') {
            return false;
        }
        if (first && startsWithCaseInsensitive(seg, ".update") && seg.size() == 7) {
            return false;
        }
        first = false;
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

// --- Minimal strict JSON parser for plan.json ---

struct JsonValue {
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object> data;

    [[nodiscard]] const Object* asObject() const {
        return std::get_if<Object>(&data);
    }
    [[nodiscard]] const Array* asArray() const {
        return std::get_if<Array>(&data);
    }
    [[nodiscard]] std::string asString(const std::string& fallback = {}) const {
        if (const auto* s = std::get_if<std::string>(&data)) {
            return *s;
        }
        return fallback;
    }
    [[nodiscard]] bool asBool(bool fallback = false) const {
        if (const auto* b = std::get_if<bool>(&data)) {
            return *b;
        }
        return fallback;
    }
    [[nodiscard]] std::int64_t asInt(std::int64_t fallback = 0) const {
        if (const auto* n = std::get_if<std::int64_t>(&data)) {
            return *n;
        }
        return fallback;
    }
    [[nodiscard]] const JsonValue* field(const std::string& key) const {
        const auto* obj = asObject();
        if (obj == nullptr) {
            return nullptr;
        }
        const auto it = obj->find(key);
        return it == obj->end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : src_(src) {}

    std::optional<JsonValue> parse() {
        skipWs();
        auto v = parseValue();
        if (!v) {
            return std::nullopt;
        }
        skipWs();
        return pos_ == src_.size() ? v : std::nullopt;
    }

private:
    void skipWs() {
        while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t' ||
                                      src_[pos_] == '\r' || src_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool consume(char ch) {
        skipWs();
        if (pos_ < src_.size() && src_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<JsonValue> parseValue() {
        skipWs();
        if (pos_ >= src_.size()) {
            return std::nullopt;
        }
        const char c = src_[pos_];
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            auto s = parseString();
            if (!s) {
                return std::nullopt;
            }
            return JsonValue{.data = std::move(*s)};
        }
        if (c == 't' && src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue{.data = true};
        }
        if (c == 'f' && src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue{.data = false};
        }
        if (c == 'n' && src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue{.data = nullptr};
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber();
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseObject() {
        ++pos_; // '{'
        JsonValue::Object obj;
        if (consume('}')) {
            return JsonValue{.data = std::move(obj)};
        }
        while (true) {
            skipWs();
            if (pos_ >= src_.size() || src_[pos_] != '"') {
                return std::nullopt;
            }
            auto key = parseString();
            if (!key || !consume(':')) {
                return std::nullopt;
            }
            auto val = parseValue();
            if (!val) {
                return std::nullopt;
            }
            obj.emplace(std::move(*key), std::move(*val));
            if (consume('}')) {
                return JsonValue{.data = std::move(obj)};
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    std::optional<JsonValue> parseArray() {
        ++pos_; // '['
        JsonValue::Array arr;
        if (consume(']')) {
            return JsonValue{.data = std::move(arr)};
        }
        while (true) {
            auto val = parseValue();
            if (!val) {
                return std::nullopt;
            }
            arr.push_back(std::move(*val));
            if (consume(']')) {
                return JsonValue{.data = std::move(arr)};
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    static void appendUtf8Codepoint(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7FU) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFU) {
            out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else if (cp <= 0xFFFFU) {
            out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        }
    }

    std::optional<std::uint16_t> parseHex4() {
        if (pos_ + 4 > src_.size()) {
            return std::nullopt;
        }
        std::uint16_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = src_[pos_++];
            v <<= 4U;
            if (c >= '0' && c <= '9') {
                v |= static_cast<std::uint16_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<std::uint16_t>(10 + c - 'a');
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<std::uint16_t>(10 + c - 'A');
            } else {
                return std::nullopt;
            }
        }
        return v;
    }

    std::optional<std::string> parseString() {
        ++pos_; // '"'
        std::string out;
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    return std::nullopt;
                }
                const char esc = src_[pos_++];
                switch (esc) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    auto u1 = parseHex4();
                    if (!u1) {
                        return std::nullopt;
                    }
                    std::uint32_t cp = *u1;
                    if (cp >= 0xD800U && cp <= 0xDBFFU && pos_ + 2 <= src_.size() &&
                        src_[pos_] == '\\' && src_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        auto u2 = parseHex4();
                        if (!u2 || *u2 < 0xDC00U || *u2 > 0xDFFFU) {
                            return std::nullopt;
                        }
                        cp = 0x10000U + (((cp - 0xD800U) << 10U) | (*u2 - 0xDC00U));
                    }
                    appendUtf8Codepoint(out, cp);
                    break;
                }
                default:
                    return std::nullopt;
                }
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        const std::size_t start = pos_;
        if (src_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E')) {
            while (pos_ < src_.size() &&
                   (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.' ||
                    src_[pos_] == '+' || src_[pos_] == '-')) {
                ++pos_;
            }
        }
        try {
            const std::int64_t val = std::stoll(std::string(src_.substr(start, pos_ - start)));
            return JsonValue{.data = val};
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string_view src_;
    std::size_t pos_ = 0;
};

std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c >= 0x20) {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

// --- Windows CNG SHA-256 verification ---

std::optional<std::string> sha256FileHex(const fs::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    struct FileGuard {
        HANDLE h;
        ~FileGuard() {
            CloseHandle(h);
        }
    } fguard{file};

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return std::nullopt;
    }
    struct AlgGuard {
        BCRYPT_ALG_HANDLE h;
        ~AlgGuard() {
            BCryptCloseAlgorithmProvider(h, 0);
        }
    } aguard{alg};

    DWORD obj_len = 0;
    DWORD cb_data = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
                          sizeof(obj_len), &cb_data, 0) < 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> hash_obj(obj_len);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg, &hash, hash_obj.data(), obj_len, nullptr, 0, 0) < 0) {
        return std::nullopt;
    }
    struct HashGuard {
        BCRYPT_HASH_HANDLE h;
        ~HashGuard() {
            BCryptDestroyHash(h);
        }
    } hguard{hash};

    std::array<std::uint8_t, 64 * 1024> buf{};
    DWORD read = 0;
    while (ReadFile(file, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) != FALSE) {
        if (read == 0) {
            break;
        }
        if (BCryptHashData(hash, buf.data(), read, 0) < 0) {
            return std::nullopt;
        }
    }
    std::array<std::uint8_t, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        return std::nullopt;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (std::uint8_t b : digest) {
        hex.push_back(kHex[(b >> 4U) & 0xFU]);
        hex.push_back(kHex[b & 0xFU]);
    }
    return hex;
}

// --- Process & File helpers ---

bool isExecutableRunningAtPath(const fs::path& target_exe) {
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(target_exe, ec);
    const std::wstring target_w = ec ? target_exe.wstring() : canon.wstring();
    const std::wstring target_name = target_exe.filename().wstring();
    if (target_name.empty()) {
        return false;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    struct SnapGuard {
        HANDLE h;
        ~SnapGuard() {
            CloseHandle(h);
        }
    } guard{snap};

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        return false;
    }
    do {
        if (!equalsCaseInsensitive(pe.szExeFile, target_name)) {
            continue;
        }
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (proc == nullptr) {
            continue;
        }
        std::array<wchar_t, 32768> path_buf{};
        auto len = static_cast<DWORD>(path_buf.size());
        const bool ok = QueryFullProcessImageNameW(proc, 0, path_buf.data(), &len) != FALSE;
        CloseHandle(proc);
        if (!ok) {
            continue;
        }
        std::error_code ec2;
        const fs::path running_canon = fs::weakly_canonical(fs::path(path_buf.data()), ec2);
        const std::wstring running_w =
            ec2 ? std::wstring(path_buf.data()) : running_canon.wstring();
        if (equalsCaseInsensitive(running_w, target_w)) {
            return true;
        }
    } while (Process32NextW(snap, &pe));
    return false;
}

bool moveFileWithRetry(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    for (int attempt = 0; attempt < kFileOpRetries; ++attempt) {
        if (MoveFileExW(src.c_str(), dst.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE) {
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED &&
            err != ERROR_LOCK_VIOLATION) {
            break;
        }
        Sleep(kFileOpRetryDelayMs);
    }
    return false;
}

bool copyFileWithRetry(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    for (int attempt = 0; attempt < kFileOpRetries; ++attempt) {
        if (CopyFileW(src.c_str(), dst.c_str(), FALSE) != FALSE) {
            return true;
        }
        Sleep(kFileOpRetryDelayMs);
    }
    return false;
}

void deleteFileWithRetry(const fs::path& p) {
    for (int attempt = 0; attempt < kFileOpRetries; ++attempt) {
        if (DeleteFileW(p.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND) {
            return;
        }
        Sleep(kFileOpRetryDelayMs);
    }
}

void writeTextFile(const fs::path& p, std::string_view text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
}

void updateInnoDisplayVersion(const std::string& version) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInnoUninstallKey, 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        return;
    }
    const std::wstring ver_w = utf8ToWide(version);
    RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(ver_w.c_str()),
                   static_cast<DWORD>((ver_w.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::wstring quoteWinArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}

bool launchProcess(const fs::path& exe, const std::vector<std::wstring>& args,
                   PROCESS_INFORMATION& pi_out) {
    std::wstring cmdline = quoteWinArg(exe.wstring());
    for (const auto& arg : args) {
        cmdline.push_back(L' ');
        cmdline += quoteWinArg(arg);
    }
    std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    pi_out = {};
    return CreateProcessW(exe.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          exe.parent_path().c_str(), &si, &pi_out) != FALSE;
}

struct PlanFile {
    std::string relativePath;
    fs::path stagedPath;
    std::string sha256;
    std::int64_t size = 0;
};

struct Plan {
    fs::path installDir;
    fs::path dataDir;
    fs::path stagingDir;
    fs::path backupDir;
    fs::path pendingEnginesDir;
    std::string oldVersion;
    std::string newVersion;
    bool installedMode = false;
    DWORD parentPid = 0;
    std::string verifyToken;
    std::vector<std::string> relaunchArgs;
    fs::path dbPath;
    fs::path dbBackupPath;
    fs::path settingsPath;
    fs::path settingsBackupPath;
    fs::path newInstalledManifestPath;
    std::vector<PlanFile> filesToInstall;
    std::vector<std::string> filesToDelete;
};

std::optional<Plan> loadPlan(const fs::path& plan_path) {
    std::ifstream in(plan_path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    JsonParser parser(content);
    const auto parsed = parser.parse();
    if (!parsed || parsed->asObject() == nullptr) {
        return std::nullopt;
    }

    auto str_field = [&](const std::string& key) -> std::string {
        const auto* v = parsed->field(key);
        return v != nullptr ? v->asString() : std::string{};
    };

    Plan plan;
    plan.installDir = fs::path(utf8ToWide(str_field("installDir")));
    plan.dataDir = fs::path(utf8ToWide(str_field("dataDir")));
    plan.stagingDir = fs::path(utf8ToWide(str_field("stagingDir")));
    plan.backupDir = fs::path(utf8ToWide(str_field("backupDir")));
    plan.pendingEnginesDir = fs::path(utf8ToWide(str_field("pendingEnginesDir")));
    plan.oldVersion = str_field("oldVersion");
    plan.newVersion = str_field("newVersion");
    if (const auto* im = parsed->field("installedMode")) {
        plan.installedMode = im->asBool(false);
    }
    if (const auto* pid = parsed->field("parentPid")) {
        const auto raw = pid->asInt(0);
        if (raw > 0) {
            plan.parentPid = static_cast<DWORD>(raw);
        }
    }
    plan.verifyToken = str_field("verifyToken");
    plan.dbPath = fs::path(utf8ToWide(str_field("dbPath")));
    plan.dbBackupPath = fs::path(utf8ToWide(str_field("dbBackupPath")));
    plan.settingsPath = fs::path(utf8ToWide(str_field("settingsPath")));
    plan.settingsBackupPath = fs::path(utf8ToWide(str_field("settingsBackupPath")));
    plan.newInstalledManifestPath = fs::path(utf8ToWide(str_field("newInstalledManifestPath")));

    if (plan.installDir.empty() || plan.backupDir.empty() || plan.newVersion.empty()) {
        return std::nullopt;
    }

    if (const auto* rargs = parsed->field("relaunchArgs"); rargs && rargs->asArray()) {
        for (const auto& item : *rargs->asArray()) {
            plan.relaunchArgs.push_back(item.asString());
        }
    }
    if (const auto* files = parsed->field("filesToInstall"); files && files->asArray()) {
        for (const auto& item : *files->asArray()) {
            const auto* rel = item.field("relativePath");
            const auto* staged = item.field("stagedPath");
            const auto* sha = item.field("sha256");
            const auto* sz = item.field("size");
            if (rel == nullptr || staged == nullptr || sha == nullptr) {
                return std::nullopt;
            }
            const std::string rel_s = rel->asString();
            if (!isSafeRelativePath(rel_s)) {
                return std::nullopt;
            }
            plan.filesToInstall.push_back(PlanFile{
                .relativePath = rel_s,
                .stagedPath = fs::path(utf8ToWide(staged->asString())),
                .sha256 = toLowerAscii(sha->asString()),
                .size = sz != nullptr ? sz->asInt(0) : 0,
            });
        }
    }
    if (const auto* dels = parsed->field("filesToDelete"); dels && dels->asArray()) {
        for (const auto& item : *dels->asArray()) {
            const std::string rel_s = item.asString();
            if (!isSafeRelativePath(rel_s)) {
                return std::nullopt;
            }
            plan.filesToDelete.push_back(rel_s);
        }
    }
    return plan;
}

void pruneEmptyParents(fs::path dir, const fs::path& stop_root) {
    std::error_code ec;
    const fs::path root_canon = fs::weakly_canonical(stop_root, ec);
    while (!dir.empty()) {
        const fs::path dir_canon = fs::weakly_canonical(dir, ec);
        if (ec || dir_canon == root_canon) {
            break;
        }
        if (!fs::is_empty(dir, ec) || ec) {
            break;
        }
        fs::remove(dir, ec);
        dir = dir.parent_path();
    }
}

void restoreDatabaseAndSettings(const Plan& plan) {
    std::error_code ec;
    if (!plan.dbBackupPath.empty() && fs::exists(plan.dbBackupPath, ec) && !plan.dbPath.empty()) {
        // Remove stale WAL/SHM sidecars so SQLite never replays new-schema WAL onto old DB.
        deleteFileWithRetry(fs::path(plan.dbPath.wstring() + L"-wal"));
        deleteFileWithRetry(fs::path(plan.dbPath.wstring() + L"-shm"));
        if (fs::exists(plan.dbPath, ec)) {
            const fs::path failed_db =
                fs::path(plan.dbPath.wstring() + L".failed-" + utf8ToWide(plan.newVersion));
            moveFileWithRetry(plan.dbPath, failed_db);
        }
        copyFileWithRetry(plan.dbBackupPath, plan.dbPath);
    }
    if (!plan.settingsBackupPath.empty() && fs::exists(plan.settingsBackupPath, ec) &&
        !plan.settingsPath.empty()) {
        copyFileWithRetry(plan.settingsBackupPath, plan.settingsPath);
    }
}

int runApply(const fs::path& plan_path, bool no_relaunch, bool no_rollback_relaunch,
             DWORD health_timeout_ms) {
    const auto plan_opt = loadPlan(plan_path);
    if (!plan_opt) {
        return 2;
    }
    Plan plan = *plan_opt;
    const fs::path work_dir = plan.installDir / L".update";
    const fs::path journal_path = work_dir / L"journal.json";
    const fs::path rollback_path = work_dir / L"rollback.json";

    // 1. Wait for parent AnimeHub.exe to exit.
    if (plan.parentPid != 0 && plan.parentPid != GetCurrentProcessId()) {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, plan.parentPid);
        if (parent != nullptr) {
            WaitForSingleObject(parent, kDefaultParentWaitMs);
            CloseHandle(parent);
        }
    }

    // 2. Re-verify SHA-256 of every staged file before touching installDir.
    for (const auto& f : plan.filesToInstall) {
        std::error_code ec;
        if (!fs::exists(f.stagedPath, ec)) {
            return 3;
        }
        const auto actual = sha256FileHex(f.stagedPath);
        if (!actual || *actual != f.sha256) {
            return 4;
        }
    }

    // 3. Write journal (applying).
    writeTextFile(journal_path, "{\"state\":\"applying\",\"oldVersion\":\"" +
                                    jsonEscape(plan.oldVersion) + "\",\"newVersion\":\"" +
                                    jsonEscape(plan.newVersion) + "\"}\n");

    // 4. Detect running mpv.exe: NEVER kill it; defer only engines/mpv/* to pending-engines.
    const fs::path mpv_exe = plan.installDir / L"engines" / L"mpv" / L"mpv.exe";
    const bool mpv_running = isExecutableRunningAtPath(mpv_exe);

    std::vector<PlanFile> to_swap;
    to_swap.reserve(plan.filesToInstall.size());
    for (const auto& f : plan.filesToInstall) {
        if (mpv_running && startsWithCaseInsensitive(f.relativePath, "engines/mpv/")) {
            const fs::path deferred = plan.pendingEnginesDir / fs::path(utf8ToWide(f.relativePath));
            moveFileWithRetry(f.stagedPath, deferred);
            continue;
        }
        to_swap.push_back(f);
    }

    // Order swap so DLLs & resources are replaced first and AnimeHub.exe is replaced LAST.
    std::stable_partition(to_swap.begin(), to_swap.end(), [](const PlanFile& f) {
        return toLowerAscii(f.relativePath) != "animehub.exe";
    });

    std::error_code ec;
    fs::remove_all(plan.backupDir, ec);
    fs::create_directories(plan.backupDir, ec);

    struct BackupRecord {
        fs::path target;
        fs::path backup;
    };
    std::vector<BackupRecord> moved_to_backup;
    std::vector<fs::path> newly_created;

    auto perform_rollback = [&](const std::string& reason, bool relaunch_old) {
        for (const auto& p : newly_created) {
            deleteFileWithRetry(p);
            pruneEmptyParents(p.parent_path(), plan.installDir);
        }
        for (auto it = moved_to_backup.rbegin(); it != moved_to_backup.rend(); ++it) {
            moveFileWithRetry(it->backup, it->target);
        }
        if (plan.installedMode && !plan.oldVersion.empty()) {
            updateInnoDisplayVersion(plan.oldVersion);
        }
        restoreDatabaseAndSettings(plan);

        writeTextFile(rollback_path, "{\"failedVersion\":\"" + jsonEscape(plan.newVersion) +
                                         "\",\"restoredVersion\":\"" + jsonEscape(plan.oldVersion) +
                                         "\",\"reason\":\"" + jsonEscape(reason) + "\"}\n");

        std::error_code clean_ec;
        fs::remove_all(plan.backupDir, clean_ec);
        fs::remove_all(plan.stagingDir, clean_ec);
        fs::remove(journal_path, clean_ec);
        fs::remove(plan_path, clean_ec);

        if (relaunch_old && !no_rollback_relaunch) {
            const fs::path app_exe = plan.installDir / L"AnimeHub.exe";
            if (fs::exists(app_exe, clean_ec)) {
                std::vector<std::wstring> args;
                for (const auto& a : plan.relaunchArgs) {
                    args.push_back(utf8ToWide(a));
                }
                PROCESS_INFORMATION pi{};
                if (launchProcess(app_exe, args, pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
        }
    };

    // 5. Move replaced targets to backupDir and install staged files.
    for (const auto& f : to_swap) {
        const fs::path rel_w = fs::path(utf8ToWide(f.relativePath));
        const fs::path dst = plan.installDir / rel_w;
        const fs::path bak = plan.backupDir / rel_w;
        if (fs::exists(dst, ec)) {
            if (!moveFileWithRetry(dst, bak)) {
                perform_rollback("backup_move_failed:" + f.relativePath, !no_relaunch);
                return 5;
            }
            moved_to_backup.push_back(BackupRecord{.target = dst, .backup = bak});
        } else {
            newly_created.push_back(dst);
        }
        if (!moveFileWithRetry(f.stagedPath, dst)) {
            perform_rollback("stage_move_failed:" + f.relativePath, !no_relaunch);
            return 6;
        }
    }

    // 6. Delete obsolete files (backing them up first for rollback).
    for (const auto& rel : plan.filesToDelete) {
        if (mpv_running && startsWithCaseInsensitive(rel, "engines/mpv/")) {
            continue;
        }
        const fs::path rel_w = fs::path(utf8ToWide(rel));
        const fs::path dst = plan.installDir / rel_w;
        const fs::path bak = plan.backupDir / rel_w;
        if (fs::exists(dst, ec)) {
            if (!moveFileWithRetry(dst, bak)) {
                perform_rollback("delete_move_failed:" + rel, !no_relaunch);
                return 7;
            }
            moved_to_backup.push_back(BackupRecord{.target = dst, .backup = bak});
            pruneEmptyParents(dst.parent_path(), plan.installDir);
        }
    }

    // 7. Atomically swap installed-manifest.json.
    if (!plan.newInstalledManifestPath.empty() && fs::exists(plan.newInstalledManifestPath, ec)) {
        const fs::path manifest_dst = plan.installDir / L"installed-manifest.json";
        const fs::path manifest_bak = plan.backupDir / L"installed-manifest.json";
        if (fs::exists(manifest_dst, ec)) {
            if (moveFileWithRetry(manifest_dst, manifest_bak)) {
                moved_to_backup.push_back(
                    BackupRecord{.target = manifest_dst, .backup = manifest_bak});
            }
        } else {
            newly_created.push_back(manifest_dst);
        }
        moveFileWithRetry(plan.newInstalledManifestPath, manifest_dst);
    }

    // 8. Sync HKCU Uninstall DisplayVersion (ONLY when installed via Inno Setup).
    if (plan.installedMode) {
        updateInnoDisplayVersion(plan.newVersion);
    }

    writeTextFile(journal_path, "{\"state\":\"verifying\",\"oldVersion\":\"" +
                                    jsonEscape(plan.oldVersion) + "\",\"newVersion\":\"" +
                                    jsonEscape(plan.newVersion) + "\"}\n");

    if (no_relaunch) {
        fs::remove_all(plan.backupDir, ec);
        fs::remove_all(plan.stagingDir, ec);
        fs::remove(journal_path, ec);
        fs::remove(plan_path, ec);
        return 0;
    }

    // 9. Relaunch AnimeHub.exe with `--post-update-verify <token>` and wait for Named Event.
    const std::wstring event_name = L"Local\\AnimeHubUpdateHealthy_" + utf8ToWide(plan.verifyToken);
    HANDLE healthy_event = CreateEventW(nullptr, TRUE, FALSE, event_name.c_str());
    if (healthy_event == nullptr) {
        perform_rollback("create_event_failed", true);
        return 8;
    }
    struct EventGuard {
        HANDLE h;
        ~EventGuard() {
            CloseHandle(h);
        }
    } eguard{healthy_event};

    const fs::path app_exe = plan.installDir / L"AnimeHub.exe";
    std::vector<std::wstring> launch_args;
    launch_args.emplace_back(L"--post-update-verify");
    launch_args.push_back(utf8ToWide(plan.verifyToken));
    for (const auto& a : plan.relaunchArgs) {
        launch_args.push_back(utf8ToWide(a));
    }

    PROCESS_INFORMATION pi{};
    if (!launchProcess(app_exe, launch_args, pi)) {
        perform_rollback("relaunch_failed", true);
        return 9;
    }

    const std::array<HANDLE, 2> wait_handles = {healthy_event, pi.hProcess};
    const DWORD wait_res = WaitForMultipleObjects(static_cast<DWORD>(wait_handles.size()),
                                                  wait_handles.data(), FALSE, health_timeout_ms);
    if (wait_res == WAIT_OBJECT_0) {
        // New version signaled healthy! Clean up staging & backups.
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        fs::remove_all(plan.backupDir, ec);
        fs::remove_all(plan.stagingDir, ec);
        fs::remove(journal_path, ec);
        fs::remove(plan_path, ec);
        return 0;
    }

    std::string fail_reason = "startup_timeout";
    if (wait_res == WAIT_OBJECT_0 + 1) {
        DWORD exit_code = 1;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        fail_reason = "startup_crash_exit_" + std::to_string(exit_code);
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3'000);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    perform_rollback(fail_reason, true);
    return 10;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Prevent Windows error dialogs during headless/automated rollback tests.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 1;
    }

    fs::path plan_path;
    bool no_relaunch = false;
    bool no_rollback_relaunch = false;
    DWORD health_timeout_ms = kDefaultHealthWaitMs;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--apply" && i + 1 < argc) {
            plan_path = fs::path(argv[++i]);
        } else if (arg == L"--no-relaunch") {
            no_relaunch = true;
        } else if (arg == L"--no-rollback-relaunch") {
            no_rollback_relaunch = true;
        } else if (arg == L"--health-timeout-ms" && i + 1 < argc) {
            try {
                const long parsed = std::stol(std::wstring(argv[++i]));
                health_timeout_ms = static_cast<DWORD>(std::clamp<long>(parsed, 200L, 60'000L));
            } catch (...) {
                health_timeout_ms = kDefaultHealthWaitMs;
            }
        }
    }
    LocalFree(argv);

    if (plan_path.empty()) {
        return 1;
    }
    return runApply(plan_path, no_relaunch, no_rollback_relaunch, health_timeout_ms);
}
