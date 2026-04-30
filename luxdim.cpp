/*
 * LuxDim - DDC/CI Brightness Tray Controller
 * Minimal Win32 + DDC/CI, no external dependencies
 *
 * Build (portable, static):
 *   g++ -O2 -mwindows -static -static-libgcc -static-libstdc++ \
 *       luxdim.cpp -o luxdim.exe \
 *       -ldxva2 -luser32 -lgdi32 -lshell32 -lcomctl32
 *
 * Requires: MinGW-w64 with dxva2 support
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <algorithm>
using std::min;
using std::max;
#include <shellapi.h>
#include <commctrl.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define WM_TRAY        (WM_USER + 1)
#define IDI_TRAY       1
#define ID_EXIT        1001
#define ID_SLIDER      1002
#define TIMER_REFRESH  1003

#define POPUP_W        90
#define POPUP_H        220

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HWND          g_hwndMain   = nullptr;
static HWND          g_hwndPopup  = nullptr;
static HWND          g_hwndSlider = nullptr;
static HWND          g_hwndLabel  = nullptr;
static HINSTANCE     g_hInst      = nullptr;
static NOTIFYICONDATA g_nid       = {};
static bool          g_popupVisible = false;

// DDC monitor handles
static PHYSICAL_MONITOR* g_monitors  = nullptr;
static DWORD             g_monCount  = 0;

// Current brightness (0-100)
static int g_brightness = 50;

// ---------------------------------------------------------------------------
// DDC/CI helpers
// ---------------------------------------------------------------------------
void DDC_EnumerateMonitors()
{
    // Free previous
    if (g_monitors && g_monCount)
        DestroyPhysicalMonitors(g_monCount, g_monitors);
    delete[] g_monitors;
    g_monitors = nullptr;
    g_monCount = 0;

    // Iterate all monitors
    DWORD total = 0;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        DWORD cnt = 0;
        if (GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &cnt))
            *reinterpret_cast<DWORD*>(lp) += cnt;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&total));

    if (total == 0) return;

    g_monitors = new PHYSICAL_MONITOR[total];
    g_monCount = 0;

    struct Ctx { PHYSICAL_MONITOR* arr; DWORD* cnt; };
    Ctx ctx{ g_monitors, &g_monCount };

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD cnt = 0;
        GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &cnt);
        GetPhysicalMonitorsFromHMONITOR(hm, cnt, c->arr + *c->cnt);
        *c->cnt += cnt;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
}

// Read brightness from first capable monitor
int DDC_GetBrightness()
{
    for (DWORD i = 0; i < g_monCount; i++) {
        DWORD minB = 0, curB = 0, maxB = 0;
        if (GetMonitorBrightness(g_monitors[i].hPhysicalMonitor, &minB, &curB, &maxB) && maxB > 0) {
            return (int)(100.0 * (curB - minB) / (maxB - minB) + 0.5);
        }
    }
    return g_brightness; // fallback: keep current
}

// Set brightness (0-100) on all capable monitors
void DDC_SetBrightness(int pct)
{
    pct = max(0, min(100, pct));
    for (DWORD i = 0; i < g_monCount; i++) {
        DWORD minB = 0, curB = 0, maxB = 0;
        if (GetMonitorBrightness(g_monitors[i].hPhysicalMonitor, &minB, &curB, &maxB) && maxB > 0) {
            DWORD val = minB + (DWORD)((maxB - minB) * pct / 100.0 + 0.5);
            SetMonitorBrightness(g_monitors[i].hPhysicalMonitor, val);
        }
    }
}

// ---------------------------------------------------------------------------
// Icon drawing (16x16 sun, GDI)
// ---------------------------------------------------------------------------
HICON CreateSunIcon(int brightness)
{
    // Simple bitmap: filled circle, brightness affects color
    int sz = 16;
    HDC hdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, sz, sz);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    SelectObject(mdc, bmp);

    // Background transparent
    HBRUSH bgBrush = CreateSolidBrush(RGB(0,0,0));
    RECT rc = {0,0,sz,sz};
    FillRect(mdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Sun color: dim orange → bright yellow
    int r = 180 + brightness * 75 / 100;
    int g2 = 120 + brightness * 100 / 100;
    int b  = 0;
    HBRUSH sunBrush = CreateSolidBrush(RGB(min(r,255), min(g2,255), b));
    SelectObject(mdc, sunBrush);
    SelectObject(mdc, GetStockObject(NULL_PEN));

    // Circle
    Ellipse(mdc, 4, 4, 12, 12);
    DeleteObject(sunBrush);

    // Rays
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(min(r,255), min(g2,220), b));
    SelectObject(mdc, pen);
    // 8 rays
    int cx=8,cy=8;
    int rays[8][4] = {
        {cx,cy-5, cx,cy-7}, {cx,cy+5, cx,cy+7},
        {cx-5,cy, cx-7,cy}, {cx+5,cy, cx+7,cy},
        {cx-3,cy-3, cx-5,cy-5}, {cx+3,cy-3, cx+5,cy-5},
        {cx-3,cy+3, cx-5,cy+5}, {cx+3,cy+3, cx+5,cy+5},
    };
    for (auto& ray : rays) {
        MoveToEx(mdc, ray[0], ray[1], nullptr);
        LineTo(mdc, ray[2], ray[3]);
    }
    DeleteObject(pen);

    // Mask: black=opaque, white=transparent
    HDC mskDC = CreateCompatibleDC(hdc);
    SelectObject(mskDC, mask);
    HBRUSH wb = (HBRUSH)GetStockObject(WHITE_BRUSH);
    HBRUSH bb = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(mskDC, &rc, wb);
    HBRUSH mkBrush = CreateSolidBrush(RGB(0,0,0));
    SelectObject(mskDC, mkBrush);
    SelectObject(mskDC, GetStockObject(NULL_PEN));
    Ellipse(mskDC, 4, 4, 12, 12);
    // fill rays in mask too
    HPEN mp = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    SelectObject(mskDC, mp);
    for (auto& ray : rays) {
        MoveToEx(mskDC, ray[0], ray[1], nullptr);
        LineTo(mskDC, ray[2], ray[3]);
    }
    DeleteObject(mp);
    DeleteObject(mkBrush);

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmColor = bmp;
    ii.hbmMask  = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteDC(mskDC);
    DeleteDC(mdc);
    DeleteObject(bmp);
    DeleteObject(mask);
    ReleaseDC(nullptr, hdc);
    return icon;
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------
void Tray_Update()
{
    wchar_t tip[64];
    wsprintfW(tip, L"LuxDim  %d%%", g_brightness);
    lstrcpynW(g_nid.szTip, tip, 64);

    HICON old = g_nid.hIcon;
    g_nid.hIcon = CreateSunIcon(g_brightness);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

void Tray_Add(HWND hwnd)
{
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = IDI_TRAY;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = CreateSunIcon(g_brightness);
    lstrcpyW(g_nid.szTip, L"LuxDim");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void Tray_Remove()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
}

// ---------------------------------------------------------------------------
// Popup window
// ---------------------------------------------------------------------------
void UpdateLabel()
{
    wchar_t buf[8];
    wsprintfW(buf, L"%d", g_brightness);
    SetWindowTextW(g_hwndLabel, buf);
}

void ShowPopup()
{
    // Position near tray (bottom-right)
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right  - POPUP_W - 4;
    int y = wa.bottom - POPUP_H - 4;
    SetWindowPos(g_hwndPopup, HWND_TOPMOST, x, y, POPUP_W, POPUP_H, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwndPopup);
    g_popupVisible = true;

    // Sync slider to current brightness
    SendMessage(g_hwndSlider, TBM_SETPOS, TRUE, 100 - g_brightness);
    UpdateLabel();
}

void HidePopup()
{
    ShowWindow(g_hwndPopup, SW_HIDE);
    g_popupVisible = false;
}

// Custom draw: dark popup
LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE)
            HidePopup();
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(255, 220, 80));
        SetBkColor(hdc, RGB(24, 24, 28));
        return (LRESULT)CreateSolidBrush(RGB(24, 24, 28));
    }

    case WM_HSCROLL:
    case WM_VSCROLL:
    {
        int pos = (int)SendMessage(g_hwndSlider, TBM_GETPOS, 0, 0);
        g_brightness = 100 - pos;
        DDC_SetBrightness(g_brightness);
        UpdateLabel();
        Tray_Update();
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(24, 24, 28));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Border
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 70));
        SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom);
        DeleteObject(pen);

        // "LUX" label at top
        SetTextColor(hdc, RGB(160, 160, 180));
        SetBkMode(hdc, TRANSPARENT);
        HFONT hf = CreateFontW(11, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SelectObject(hdc, hf);
        RECT tr = {0, 6, rc.right, 22};
        DrawTextW(hdc, L"LUX", -1, &tr, DT_CENTER | DT_SINGLELINE);
        DeleteObject(hf);

        EndPaint(hwnd, &ps);
        break;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Main invisible window
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        Tray_Add(hwnd);
        SetTimer(hwnd, TIMER_REFRESH, 5000, nullptr); // re-read DDC every 5s
        break;

    case WM_TIMER:
        if (wp == TIMER_REFRESH) {
            // Non-blocking re-read (DDC can be slow, skip if popup open to avoid lag)
            if (!g_popupVisible) {
                int read = DDC_GetBrightness();
                if (read != g_brightness) {
                    g_brightness = read;
                    Tray_Update();
                }
            }
        }
        break;

    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
            if (g_popupVisible) HidePopup();
            else                ShowPopup();
            break;
        case WM_RBUTTONUP:
        {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            break;
        }
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_EXIT)
            DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH);
        Tray_Remove();
        if (g_monitors && g_monCount)
            DestroyPhysicalMonitors(g_monCount, g_monitors);
        delete[] g_monitors;
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Init common controls (trackbar)
    INITCOMMONCONTROLSEX icx = {sizeof(icx), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icx);

    // Enumerate DDC monitors
    DDC_EnumerateMonitors();
    g_brightness = DDC_GetBrightness();

    // Register main (hidden) window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = MainProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"LuxDimMain";
    RegisterClassW(&wc);

    g_hwndMain = CreateWindowW(L"LuxDimMain", L"LuxDim",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    // Register popup class
    WNDCLASSW wcp = {};
    wcp.lpfnWndProc   = PopupProc;
    wcp.hInstance     = hInst;
    wcp.hbrBackground = CreateSolidBrush(RGB(24, 24, 28));
    wcp.lpszClassName = L"LuxDimPopup";
    RegisterClassW(&wcp);

    g_hwndPopup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"LuxDimPopup", nullptr,
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, POPUP_W, POPUP_H,
        g_hwndMain, nullptr, hInst, nullptr);

    // Vertical slider (trackbar): 0=top(bright) .. 100=bottom(dim)
    g_hwndSlider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_NOTICKS | TBS_BOTH,
        18, 28, 34, 150,
        g_hwndPopup, (HMENU)ID_SLIDER, hInst, nullptr);
    SendMessage(g_hwndSlider, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessage(g_hwndSlider, TBM_SETPOS,   TRUE,  100 - g_brightness);

    // Brightness number label
    g_hwndLabel = CreateWindowExW(0, L"STATIC", L"50",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        8, 184, POPUP_W - 16, 22,
        g_hwndPopup, nullptr, hInst, nullptr);

    // Set label font
    HFONT hf = CreateFontW(18, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessage(g_hwndLabel, WM_SETFONT, (WPARAM)hf, TRUE);
    UpdateLabel();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
