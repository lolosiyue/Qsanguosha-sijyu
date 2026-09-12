// QSanguosha portable Web bundle launcher.
// Build as a Windows GUI subsystem C++17 executable and place beside index.html.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;
namespace {

constexpr unsigned short kPort = 9529;
constexpr size_t kMaxRequest = 16 * 1024;
constexpr size_t kIoBuffer = 64 * 1024;
constexpr int kMaxClients = 16;
constexpr DWORD kSocketTimeoutMs = 5000;
constexpr UINT kStatusMessage = WM_APP + 1;

std::atomic_bool g_stop{false};
std::atomic<SOCKET> g_listener{INVALID_SOCKET};
std::atomic<HWND> g_window{nullptr};
fs::path g_root;
std::mutex g_clientsMutex;
std::vector<SOCKET> g_clients;

void trackClient(SOCKET socket) {
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    g_clients.push_back(socket);
}

void finishClient(SOCKET socket) {
    // Hold the registry lock through close so shutdown cannot miss a reused descriptor.
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), socket), g_clients.end());
    shutdown(socket, SD_BOTH);
    closesocket(socket);
}

void wakeClients() {
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    for (SOCKET socket : g_clients) shutdown(socket, SD_BOTH);
}

std::wstring winError(DWORD error) {
    wchar_t *buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        error, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::wstring result = count ? std::wstring(buffer, count) : L"unknown error";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

void setStatus(const std::wstring &message) {
    HWND window = g_window.load();
    if (window) {
        auto *copy = new std::wstring(message);
        if (!PostMessageW(window, kStatusMessage, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
    }
}

bool sendAll(SOCKET socket, const char *data, size_t size) {
    while (size) {
        const int sent = send(socket, data, static_cast<int>(std::min(size, size_t(INT_MAX))), 0);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

std::wstring urlDecodePath(std::string_view input, bool &ok) {
    std::string bytes;
    bytes.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char value = static_cast<unsigned char>(input[i]);
        if (value == '%') {
            if (i + 2 >= input.size()) { ok = false; return {}; }
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int high = hex(input[i + 1]), low = hex(input[i + 2]);
            if (high < 0 || low < 0) { ok = false; return {}; }
            value = static_cast<unsigned char>((high << 4) | low);
            i += 2;
        }
        if (value == 0 || value < 0x20 || value == '\\' || value == ':') { ok = false; return {}; }
        bytes.push_back(static_cast<char>(value));
    }
    if (bytes.empty() || bytes.front() != '/') { ok = false; return {}; }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) { ok = false; return {}; }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), result.data(), length);
    ok = true;
    return result;
}

bool hasReparsePoint(const fs::path &path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool safePath(const std::wstring &urlPath, fs::path &file) {
    if (urlPath == L"/") {
        file = g_root / L"index.html";
        return !hasReparsePoint(file);
    }
    std::wstring relative = urlPath.substr(1);
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= relative.size()) {
        size_t end = relative.find(L'/', start);
        std::wstring part = relative.substr(start, end == std::wstring::npos ? end : end - start);
        if (part == L".." || part == L"." || part.empty()) {
            if (part == L"..") return false;
        } else {
            parts.push_back(std::move(part));
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    file = g_root;
    for (const auto &part : parts) file /= part;
    // Reject reparse points at every component, then compare canonical paths.
    fs::path current = g_root;
    for (const auto &part : parts) {
        current /= part;
        if (hasReparsePoint(current)) return false;
    }
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(file, error);
    const fs::path rootCanonical = fs::weakly_canonical(g_root, error);
    if (error || !fs::is_regular_file(canonical, error)) return false;
    auto rootText = rootCanonical.native();
    auto fileText = canonical.native();
    if (fileText.size() <= rootText.size() ||
        fileText.compare(0, rootText.size(), rootText) != 0 ||
        fileText[rootText.size()] != L'\\') return false;
    file = canonical;
    return true;
}

const char *mimeType(const fs::path &path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
    if (ext == L".html" || ext == L".htm") return "text/html; charset=utf-8";
    if (ext == L".js" || ext == L".mjs") return "text/javascript; charset=utf-8";
    if (ext == L".json" || ext == L".map") return "application/json; charset=utf-8";
    if (ext == L".wasm") return "application/wasm";
    if (ext == L".lua") return "text/plain; charset=utf-8";
    if (ext == L".css") return "text/css; charset=utf-8";
    if (ext == L".svg") return "image/svg+xml";
    if (ext == L".png") return "image/png";
    if (ext == L".jpg" || ext == L".jpeg") return "image/jpeg";
    if (ext == L".webp") return "image/webp";
    if (ext == L".gif") return "image/gif";
    if (ext == L".mp3") return "audio/mpeg";
    if (ext == L".ogg") return "audio/ogg";
    if (ext == L".wav") return "audio/wav";
    return "application/octet-stream";
}

void response(SOCKET socket, int code, const char *reason, const char *type, uint64_t length) {
    std::string header = "HTTP/1.1 " + std::to_string(code) + " " + reason +
        "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(length) +
        "\r\nCache-Control: no-cache\r\nCross-Origin-Opener-Policy: same-origin\r\n"
        "Cross-Origin-Embedder-Policy: require-corp\r\nCross-Origin-Resource-Policy: same-origin\r\n"
        "Connection: close\r\n\r\n";
    sendAll(socket, header.data(), header.size());
}

bool validHostHeader(const std::string &request, size_t firstEnd) {
    bool found = false;
    size_t lineStart = firstEnd + 2;
    while (lineStart < request.size()) {
        const size_t lineEnd = request.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd == lineStart) break;
        const size_t colon = request.find(':', lineStart);
        if (colon == std::string::npos || colon > lineEnd) return false;
        std::string name = request.substr(lineStart, colon - lineStart);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "host") {
            if (found) return false;
            found = true;
            size_t valueStart = colon + 1;
            while (valueStart < lineEnd && (request[valueStart] == ' ' || request[valueStart] == '\t')) ++valueStart;
            size_t valueEnd = lineEnd;
            while (valueEnd > valueStart && (request[valueEnd - 1] == ' ' || request[valueEnd - 1] == '\t')) --valueEnd;
            if (request.substr(valueStart, valueEnd - valueStart) != "127.0.0.1:9529") return false;
        }
        lineStart = lineEnd + 2;
    }
    return found;
}

void handleClient(SOCKET socket) {
    std::string request;
    char buffer[2048];
    while (request.size() < kMaxRequest && request.find("\r\n\r\n") == std::string::npos) {
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) return;
        request.append(buffer, static_cast<size_t>(received));
    }
    if (request.size() >= kMaxRequest) { response(socket, 413, "Payload Too Large", "text/plain", 0); return; }
    const size_t firstEnd = request.find("\r\n");
    if (firstEnd == std::string::npos) { response(socket, 400, "Bad Request", "text/plain", 0); return; }
    if (!validHostHeader(request, firstEnd)) { response(socket, 400, "Bad Request", "text/plain", 0); return; }
    const std::string line = request.substr(0, firstEnd);
    const bool head = line.rfind("HEAD ", 0) == 0;
    const bool get = line.rfind("GET ", 0) == 0;
    if (!head && !get) { response(socket, 405, "Method Not Allowed", "text/plain", 0); return; }
    const size_t pathStart = head ? 5 : 4, pathEnd = line.find(' ', pathStart);
    if (pathEnd == std::string::npos || pathEnd == pathStart || pathEnd - pathStart > 8192) {
        response(socket, 400, "Bad Request", "text/plain", 0); return;
    }
    std::string_view raw(line.data() + pathStart, pathEnd - pathStart);
    const size_t query = raw.find_first_of("?#");
    if (query != std::string_view::npos) raw = raw.substr(0, query);
    bool valid = false;
    const std::wstring urlPath = urlDecodePath(raw, valid);
    fs::path file;
    if (!valid || !safePath(urlPath, file)) { response(socket, 403, "Forbidden", "text/plain", 0); return; }
    std::error_code error;
    const uintmax_t size = fs::file_size(file, error);
    if (error) { response(socket, 404, "Not Found", "text/plain", 0); return; }
    response(socket, 200, "OK", mimeType(file), static_cast<uint64_t>(size));
    if (head) return;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return;
    std::vector<char> chunk(kIoBuffer);
    while (stream && !g_stop.load()) {
        stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize count = stream.gcount();
        if (count <= 0 || !sendAll(socket, chunk.data(), static_cast<size_t>(count))) break;
    }
}

void serverThread() {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    g_listener.store(listener);
    if (listener == INVALID_SOCKET) { setStatus(L"Socket failed: " + winError(WSAGetLastError())); return; }
    if (g_stop.load()) { closesocket(listener); g_listener.store(INVALID_SOCKET); return; }
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        const int error = WSAGetLastError(); closesocket(listener); g_listener.store(INVALID_SOCKET);
        setStatus(L"Cannot bind 127.0.0.1:" + std::to_wstring(kPort) + L": " + winError(error)); return;
    }
    setStatus(L"遊戲執行中：http://127.0.0.1:" + std::to_wstring(kPort) + L"/（按「關閉」停止）");
    const std::wstring browserUrl = L"http://127.0.0.1:" + std::to_wstring(kPort) + L"/";
    // This worker has no Windows message loop. Initialize its COM apartment and
    // finish the shell handoff before entering the socket loop.
    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(comStatus)) {
        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        launch.lpVerb = L"open";
        launch.lpFile = browserUrl.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch)) {
            const DWORD error = GetLastError();
            setStatus(L"無法開啟預設瀏覽器：" + winError(error) + L"。請開啟 " + browserUrl);
        }
        CoUninitialize();
    } else {
        setStatus(L"無法初始化瀏覽器啟動元件。請開啟 " + browserUrl);
    }
    std::vector<std::thread> workers;
    std::vector<std::shared_ptr<std::atomic_bool>> completed;
    auto reapWorkers = [&] {
        for (size_t i = 0; i < workers.size();) {
            if (!completed[i]->load()) { ++i; continue; }
            workers[i].join();
            workers.erase(workers.begin() + static_cast<std::ptrdiff_t>(i));
            completed.erase(completed.begin() + static_cast<std::ptrdiff_t>(i));
        }
    };
    while (!g_stop.load()) {
        reapWorkers();
        listener = g_listener.load();
        if (listener == INVALID_SOCKET) break;
        if (workers.size() >= kMaxClients) {
            Sleep(25);
            continue;
        }
        fd_set set; FD_ZERO(&set); FD_SET(listener, &set);
        timeval timeout{0, 250000};
        if (select(0, &set, nullptr, nullptr, &timeout) <= 0) continue;
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        DWORD socketTimeout = kSocketTimeoutMs;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&socketTimeout), sizeof(socketTimeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&socketTimeout), sizeof(socketTimeout));
        trackClient(client);
        auto done = std::make_shared<std::atomic_bool>(false);
        completed.push_back(done);
        workers.emplace_back([client, done] {
            handleClient(client);
            finishClient(client);
            done->store(true);
        });
    }
    wakeClients();
    for (auto &worker : workers) if (worker.joinable()) worker.join();
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kStatusMessage) {
        auto *text = reinterpret_cast<std::wstring *>(lParam);
        SetWindowTextW(GetDlgItem(window, 1), text->c_str()); delete text; return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == 2) { DestroyWindow(window); return 0; }
    if (message == WM_CLOSE) { g_stop = true; g_window.store(nullptr); SOCKET listener = g_listener.exchange(INVALID_SOCKET); if (listener != INVALID_SOCKET) closesocket(listener); wakeClients(); DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    wchar_t module[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!length || length >= MAX_PATH) { MessageBoxW(nullptr, L"Cannot locate launcher executable.", L"QSanguosha Web", MB_ICONERROR); return 1; }
    g_root = fs::path(module).parent_path();
    std::error_code error;
    if (!fs::is_regular_file(g_root / L"index.html", error)) { MessageBoxW(nullptr, L"index.html is missing beside this launcher.", L"QSanguosha Web", MB_ICONERROR); return 2; }
    WSAData wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { MessageBoxW(nullptr, L"Winsock initialization failed.", L"QSanguosha Web", MB_ICONERROR); return 3; }
    const wchar_t className[] = L"QSanguoshaPortableWebLauncher";
    WNDCLASSW klass{}; klass.lpfnWndProc = windowProc; klass.hInstance = instance; klass.hCursor = LoadCursor(nullptr, IDC_ARROW); klass.lpszClassName = className;
    RegisterClassW(&klass);
    g_window.store(CreateWindowW(className, L"三國殺 Web 遊戲", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 150, nullptr, nullptr, instance, nullptr));
    HWND window = g_window.load();
    if (!window) { WSACleanup(); MessageBoxW(nullptr, L"無法建立啟動器視窗。", L"三國殺 Web 遊戲", MB_ICONERROR); return 4; }
    HWND status = CreateWindowW(L"STATIC", L"啟動遊戲中……", WS_CHILD | WS_VISIBLE, 16, 18, 520, 42, window, reinterpret_cast<HMENU>(1), instance, nullptr);
    HWND close = CreateWindowW(L"BUTTON", L"關閉", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 230, 75, 90, 28, window, reinterpret_cast<HMENU>(2), instance, nullptr);
    if (!status || !close) { DestroyWindow(window); WSACleanup(); MessageBoxW(nullptr, L"無法建立啟動器控制項。", L"三國殺 Web 遊戲", MB_ICONERROR); return 5; }
    ShowWindow(window, show ? show : SW_SHOWNORMAL); UpdateWindow(window);
    std::thread server(serverThread);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    g_stop = true;
    SOCKET listener = g_listener.exchange(INVALID_SOCKET);
    if (listener != INVALID_SOCKET) closesocket(listener);
    wakeClients();
    if (server.joinable()) server.join();
    WSACleanup();
    return 0;
}
