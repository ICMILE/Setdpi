#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <ctime>
#include <string>

#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON (WM_USER + 100)
#define HOTKEY_ID_100  1
#define HOTKEY_ID_175  2
#define SETDPI_PATH L"E:\\miscellaneous\\BAT\\SetDpi.exe"

HWND g_hWnd;
NOTIFYICONDATAW g_nid = {};
HMENU g_hMenu;
std::ofstream g_log;

// 获取当前时间字符串
std::string GetTimeStr() {
    time_t now = time(nullptr);
    char buf[64];
    ctime_s(buf, sizeof(buf), &now);
    std::string s(buf);
    s.pop_back(); // 去掉换行
    return s;
}

// 写日志
void Log(const char* msg, DWORD err = 0) {
    if (g_log.is_open()) {
        g_log << "[" << GetTimeStr() << "] " << msg;
        if (err) g_log << " (err=" << err << ")";
        g_log << std::endl;
        g_log.flush();
    }
}

void ExecuteSetDPI(int scalePercent) {
    Log(("ExecuteSetDPI: " + std::to_string(scalePercent)).c_str());
    wchar_t commandLine[256];
    swprintf_s(commandLine, L"\"%s\" %d 1", SETDPI_PATH, scalePercent);
    
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL success = CreateProcessW(NULL, commandLine, NULL, NULL, FALSE,
                                  CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (success) {
        Log("CreateProcess succeeded, waiting...");
        WaitForSingleObject(pi.hProcess, 5000); // 最多等5秒
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        Log(("SetDpi.exe finished, exit code=" + std::to_string(exitCode)).c_str());
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        DWORD err = GetLastError();
        Log("CreateProcess FAILED", err);
    }
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey);
    if (res == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            res = RegSetValueExW(hKey, L"DPIShortcutCtrlZX", 0, REG_SZ,
                (BYTE*)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
            Log(enable ? "SetAutoStart: ON" : "SetAutoStart: OFF", res);
        } else {
            RegDeleteValueW(hKey, L"DPIShortcutCtrlZX");
        }
        RegCloseKey(hKey);
    } else {
        Log("RegOpenKeyEx for Run failed", res);
    }
}

void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 100;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"DPI 快捷键");
    BOOL ret = Shell_NotifyIconW(NIM_ADD, &g_nid);
    Log("AddTrayIcon", ret ? 0 : GetLastError());
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    Log("RemoveTrayIcon");
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    g_hMenu = CreatePopupMenu();
    AppendMenuW(g_hMenu, MF_STRING, HOTKEY_ID_100, L"缩放至 100% (Alt+Z)");
    AppendMenuW(g_hMenu, MF_STRING, HOTKEY_ID_175, L"缩放至 175% (Alt+X)");
    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_hMenu, MF_STRING, 3, L"退出");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(g_hMenu);
    Log("ContextMenu shown");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            Log("WM_CREATE: registering hotkeys");
            // 改为 Alt + Z / Alt + X
            if (!RegisterHotKey(hwnd, HOTKEY_ID_100, MOD_ALT, 'Z'))
                Log("RegisterHotKey Alt+Z failed", GetLastError());
            if (!RegisterHotKey(hwnd, HOTKEY_ID_175, MOD_ALT, 'X'))
                Log("RegisterHotKey Alt+X failed", GetLastError());
            AddTrayIcon(hwnd);
            SetAutoStart(true);
            break;
        case WM_HOTKEY:
            Log(("WM_HOTKEY: " + std::to_string(wParam)).c_str());
            if (wParam == HOTKEY_ID_100) ExecuteSetDPI(100);
            else if (wParam == HOTKEY_ID_175) ExecuteSetDPI(175);
            break;
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) ShowContextMenu(hwnd);
            else if (lParam == WM_LBUTTONDBLCLK) ExecuteSetDPI(175);
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == HOTKEY_ID_100) ExecuteSetDPI(100);
            else if (LOWORD(wParam) == HOTKEY_ID_175) ExecuteSetDPI(175);
            else if (LOWORD(wParam) == 3) {
                Log("Exit requested");
                PostQuitMessage(0);
            }
            break;
        case WM_DESTROY:
            UnregisterHotKey(hwnd, HOTKEY_ID_100);
            UnregisterHotKey(hwnd, HOTKEY_ID_175);
            RemoveTrayIcon();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 修复1：入口改为 wWinMain（Unicode 版本）
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    // 修复2：strcat_s 补充目标缓冲区大小
    char logPath[MAX_PATH];
    GetTempPathA(MAX_PATH, logPath);
    strcat_s(logPath, MAX_PATH, "DPIShortcut.log");
    g_log.open(logPath, std::ios::out | std::ios::app);
    Log("=== Program started ===");

    const wchar_t CLASS_NAME[] = L"DPIShortcutWindowClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassW(&wc)) {
        Log("RegisterClassW failed", GetLastError());
        return 1;
    }

    g_hWnd = CreateWindowExW(0, CLASS_NAME, L"DPI Shortcut", 0,
        0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) {
        Log("CreateWindowEx failed", GetLastError());
        return 1;
    }
    Log("Hidden window created");

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    g_log.close();
    return 0;
}