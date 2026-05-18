/*
 * LuxDim 1.1-beta - DDC/CI Brightness Tray Controller
 * Changelog 1.1: hotkey settings window (right-click -> Settings)
 *
 * Build (portable, static):
 *   g++ -O2 -mwindows -static -static-libgcc -static-libstdc++ \
 *       luxdim.cpp -o luxdim.exe \
 *       -ldxva2 -luser32 -lgdi32 -lshell32 -lcomctl32
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

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define WM_TRAY         (WM_USER + 1)
#define IDI_TRAY        1
#define ID_EXIT         1001
#define ID_SLIDER       1002
#define TIMER_REFRESH   1003
#define ID_SETTINGS     1004

#define ID_HK_UP_BOX    2001
#define ID_HK_DN_BOX    2002
#define ID_BTN_SAVE     2003
#define ID_BTN_CANCEL   2004
#define ID_BTN_CLEAR_UP 2005
#define ID_BTN_CLEAR_DN 2006

#define HOTKEY_UP       1
#define HOTKEY_DOWN     2

#define POPUP_W         90
#define POPUP_H         220
#define STEP            5

// ---------------------------------------------------------------------------
// Hotkey state
// ---------------------------------------------------------------------------
struct HotkeyDef { UINT mod; UINT vk; };

static HotkeyDef g_hkUp = {MOD_CONTROL, VK_UP};
static HotkeyDef g_hkDn = {MOD_CONTROL, VK_DOWN};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HWND           g_hwndMain     = nullptr;
static HWND           g_hwndPopup    = nullptr;
static HWND           g_hwndSlider   = nullptr;
static HWND           g_hwndLabel    = nullptr;
static HWND           g_hwndSettings = nullptr;
static HINSTANCE      g_hInst        = nullptr;
static NOTIFYICONDATA g_nid          = {};
static bool           g_popupVisible = false;

static PHYSICAL_MONITOR* g_monitors  = nullptr;
static DWORD             g_monCount  = 0;
static int               g_brightness= 50;

// ---------------------------------------------------------------------------
// DDC/CI
// ---------------------------------------------------------------------------
void DDC_EnumerateMonitors()
{
    if (g_monitors && g_monCount) DestroyPhysicalMonitors(g_monCount, g_monitors);
    delete[] g_monitors; g_monitors = nullptr; g_monCount = 0;

    DWORD total = 0;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        DWORD cnt = 0;
        if (GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &cnt)) *reinterpret_cast<DWORD*>(lp) += cnt;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&total));
    if (total == 0) return;

    g_monitors = new PHYSICAL_MONITOR[total];
    struct Ctx { PHYSICAL_MONITOR* arr; DWORD* cnt; };
    Ctx ctx{ g_monitors, &g_monCount };
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD cnt = 0;
        GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &cnt);
        GetPhysicalMonitorsFromHMONITOR(hm, cnt, c->arr + *c->cnt);
        *c->cnt += cnt; return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
}

int DDC_GetBrightness()
{
    for (DWORD i = 0; i < g_monCount; i++) {
        DWORD minB=0, curB=0, maxB=0;
        if (GetMonitorBrightness(g_monitors[i].hPhysicalMonitor, &minB, &curB, &maxB) && maxB>0)
            return (int)(100.0*(curB-minB)/(maxB-minB)+0.5);
    }
    return g_brightness;
}

void DDC_SetBrightness(int pct)
{
    pct = max(0, min(100, pct));
    for (DWORD i = 0; i < g_monCount; i++) {
        DWORD minB=0, curB=0, maxB=0;
        if (GetMonitorBrightness(g_monitors[i].hPhysicalMonitor, &minB, &curB, &maxB) && maxB>0) {
            DWORD val = minB + (DWORD)((maxB-minB)*pct/100.0+0.5);
            SetMonitorBrightness(g_monitors[i].hPhysicalMonitor, val);
        }
    }
}

// ---------------------------------------------------------------------------
// Hotkey registration
// ---------------------------------------------------------------------------
void Hotkeys_Unregister()
{
    UnregisterHotKey(g_hwndMain, HOTKEY_UP);
    UnregisterHotKey(g_hwndMain, HOTKEY_DOWN);
}

void Hotkeys_Register()
{
    Hotkeys_Unregister();
    if (g_hkUp.vk) RegisterHotKey(g_hwndMain, HOTKEY_UP,   g_hkUp.mod, g_hkUp.vk);
    if (g_hkDn.vk) RegisterHotKey(g_hwndMain, HOTKEY_DOWN, g_hkDn.mod, g_hkDn.vk);
}

// ---------------------------------------------------------------------------
// Icon (GDI, 16x16)
// ---------------------------------------------------------------------------
HICON CreateSunIcon(int brightness)
{
    int sz = 16;
    HDC hdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP bmp  = CreateCompatibleBitmap(hdc, sz, sz);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    SelectObject(mdc, bmp);

    HBRUSH bgBrush = CreateSolidBrush(RGB(0,0,0));
    RECT rc = {0,0,sz,sz};
    FillRect(mdc, &rc, bgBrush); DeleteObject(bgBrush);

    int r  = min(180 + brightness*75/100, 255);
    int g2 = min(120 + brightness*100/100, 255);

    HBRUSH sunBrush = CreateSolidBrush(RGB(r,g2,0));
    SelectObject(mdc, sunBrush);
    SelectObject(mdc, GetStockObject(NULL_PEN));
    Ellipse(mdc, 4, 4, 12, 12);
    DeleteObject(sunBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, min(g2,220), 0));
    SelectObject(mdc, pen);
    int cx=8, cy=8;
    int rays[8][4] = {
        {cx,cy-5,cx,cy-7},{cx,cy+5,cx,cy+7},{cx-5,cy,cx-7,cy},{cx+5,cy,cx+7,cy},
        {cx-3,cy-3,cx-5,cy-5},{cx+3,cy-3,cx+5,cy-5},{cx-3,cy+3,cx-5,cy+5},{cx+3,cy+3,cx+5,cy+5}
    };
    for (auto& ray : rays) { MoveToEx(mdc,ray[0],ray[1],nullptr); LineTo(mdc,ray[2],ray[3]); }
    DeleteObject(pen);

    HDC mskDC = CreateCompatibleDC(hdc);
    SelectObject(mskDC, mask);
    FillRect(mskDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HBRUSH mkBrush = CreateSolidBrush(RGB(0,0,0));
    SelectObject(mskDC, mkBrush);
    SelectObject(mskDC, GetStockObject(NULL_PEN));
    Ellipse(mskDC, 4, 4, 12, 12);
    HPEN mp = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    SelectObject(mskDC, mp);
    for (auto& ray : rays) { MoveToEx(mskDC,ray[0],ray[1],nullptr); LineTo(mskDC,ray[2],ray[3]); }
    DeleteObject(mp); DeleteObject(mkBrush);

    ICONINFO ii = {}; ii.fIcon=TRUE; ii.hbmColor=bmp; ii.hbmMask=mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteDC(mskDC); DeleteDC(mdc);
    DeleteObject(bmp); DeleteObject(mask);
    ReleaseDC(nullptr, hdc);
    return icon;
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------
void Tray_Update()
{
    wchar_t tip[64]; wsprintfW(tip, L"LuxDim  %d%%", g_brightness);
    lstrcpynW(g_nid.szTip, tip, 64);
    HICON old = g_nid.hIcon;
    g_nid.hIcon = CreateSunIcon(g_brightness);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

void Tray_Add(HWND hwnd)
{
    g_nid.cbSize=sizeof(g_nid); g_nid.hWnd=hwnd; g_nid.uID=IDI_TRAY;
    g_nid.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE; g_nid.uCallbackMessage=WM_TRAY;
    g_nid.hIcon=CreateSunIcon(g_brightness);
    lstrcpyW(g_nid.szTip, L"LuxDim");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void Tray_Remove()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
}

// ---------------------------------------------------------------------------
// Brightness popup
// ---------------------------------------------------------------------------
void UpdateLabel()
{
    wchar_t buf[8]; wsprintfW(buf, L"%d", g_brightness);
    SetWindowTextW(g_hwndLabel, buf);
}

void ShowPopup()
{
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_hwndPopup, HWND_TOPMOST,
        wa.right-POPUP_W-4, wa.bottom-POPUP_H-4, POPUP_W, POPUP_H, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwndPopup);
    g_popupVisible = true;
    SendMessage(g_hwndSlider, TBM_SETPOS, TRUE, 100-g_brightness);
    UpdateLabel();
}

void HidePopup() { ShowWindow(g_hwndPopup, SW_HIDE); g_popupVisible = false; }

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ACTIVATE:      if (LOWORD(wp)==WA_INACTIVE) HidePopup(); break;
    case WM_CTLCOLORSTATIC: {
        HDC hdc=(HDC)wp; SetTextColor(hdc,RGB(255,220,80)); SetBkColor(hdc,RGB(24,24,28));
        return (LRESULT)CreateSolidBrush(RGB(24,24,28));
    }
    case WM_HSCROLL: case WM_VSCROLL: {
        int pos=(int)SendMessage(g_hwndSlider,TBM_GETPOS,0,0);
        g_brightness=100-pos; DDC_SetBrightness(g_brightness); UpdateLabel(); Tray_Update();
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(24,24,28)); FillRect(hdc,&rc,bg); DeleteObject(bg);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(60,60,70));
        SelectObject(hdc,pen); SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,0,0,rc.right,rc.bottom); DeleteObject(pen);
        SetTextColor(hdc,RGB(160,160,180)); SetBkMode(hdc,TRANSPARENT);
        HFONT hf=CreateFontW(11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        SelectObject(hdc,hf);
        RECT tr={0,6,rc.right,22}; DrawTextW(hdc,L"LUX",-1,&tr,DT_CENTER|DT_SINGLELINE);
        DeleteObject(hf); EndPaint(hwnd,&ps); break;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

// ---------------------------------------------------------------------------
// Settings window
// ---------------------------------------------------------------------------
struct HkCapture { WNDPROC origProc; HotkeyDef current; };
static HkCapture g_capUp = {}, g_capDn = {};

void FormatHotkey(const HotkeyDef& hk, wchar_t* buf, int bufLen)
{
    if (!hk.vk) { lstrcpynW(buf, L"(not set)", bufLen); return; }
    buf[0] = 0;
    if (hk.mod & MOD_CONTROL) lstrcatW(buf, L"Ctrl+");
    if (hk.mod & MOD_ALT)     lstrcatW(buf, L"Alt+");
    if (hk.mod & MOD_SHIFT)   lstrcatW(buf, L"Shift+");
    if (hk.mod & MOD_WIN)     lstrcatW(buf, L"Win+");
    LONG sc = (MapVirtualKeyW(hk.vk, MAPVK_VK_TO_VSC) << 16);
    if (hk.vk==VK_UP||hk.vk==VK_DOWN||hk.vk==VK_LEFT||hk.vk==VK_RIGHT||
        hk.vk==VK_INSERT||hk.vk==VK_DELETE||hk.vk==VK_HOME||hk.vk==VK_END||
        hk.vk==VK_PRIOR||hk.vk==VK_NEXT) sc |= (1<<24);
    int len = lstrlenW(buf);
    if (!GetKeyNameTextW(sc, buf+len, bufLen-len))
        wsprintfW(buf+len, L"0x%02X", hk.vk);
}

LRESULT CALLBACK HkEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND parent = GetParent(hwnd);
    HkCapture* cap = (hwnd==GetDlgItem(parent,ID_HK_UP_BOX)) ? &g_capUp : &g_capDn;

    if (msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN) {
        UINT vk=(UINT)wp;
        if (vk==VK_CONTROL||vk==VK_SHIFT||vk==VK_MENU||vk==VK_LWIN||vk==VK_RWIN) return 0;
        UINT mod=0;
        if (GetKeyState(VK_CONTROL)&0x8000) mod|=MOD_CONTROL;
        if (GetKeyState(VK_MENU)   &0x8000) mod|=MOD_ALT;
        if (GetKeyState(VK_SHIFT)  &0x8000) mod|=MOD_SHIFT;
        if ((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000) mod|=MOD_WIN;
        cap->current={mod,vk};
        wchar_t buf[128]; FormatHotkey(cap->current,buf,128);
        SetWindowTextW(hwnd,buf); return 0;
    }
    if (msg==WM_KEYUP||msg==WM_SYSKEYUP||msg==WM_CHAR) return 0;
    if (msg==WM_SETFOCUS)  SetWindowTextW(hwnd, L"Press keys...");
    if (msg==WM_KILLFOCUS) {
        wchar_t buf[128]; FormatHotkey(cap->current,buf,128); SetWindowTextW(hwnd,buf);
    }
    return CallWindowProcW(cap->origProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        // Up row
        CreateWindowW(L"STATIC",L"Brightness Up:",WS_CHILD|WS_VISIBLE|SS_LEFT,
            16,18,130,16,hwnd,nullptr,g_hInst,nullptr);
        HWND eUp=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_READONLY,
            16,36,182,22,hwnd,(HMENU)ID_HK_UP_BOX,g_hInst,nullptr);
        CreateWindowW(L"BUTTON",L"x",WS_CHILD|WS_VISIBLE,
            202,36,26,22,hwnd,(HMENU)ID_BTN_CLEAR_UP,g_hInst,nullptr);
        // Down row
        CreateWindowW(L"STATIC",L"Brightness Down:",WS_CHILD|WS_VISIBLE|SS_LEFT,
            16,70,130,16,hwnd,nullptr,g_hInst,nullptr);
        HWND eDn=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_READONLY,
            16,88,182,22,hwnd,(HMENU)ID_HK_DN_BOX,g_hInst,nullptr);
        CreateWindowW(L"BUTTON",L"x",WS_CHILD|WS_VISIBLE,
            202,88,26,22,hwnd,(HMENU)ID_BTN_CLEAR_DN,g_hInst,nullptr);
        // Save / Cancel
        CreateWindowW(L"BUTTON",L"Save",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            36,124,82,26,hwnd,(HMENU)ID_BTN_SAVE,g_hInst,nullptr);
        CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE,
            126,124,82,26,hwnd,(HMENU)ID_BTN_CANCEL,g_hInst,nullptr);

        // Init from current
        g_capUp.current=g_hkUp; g_capDn.current=g_hkDn;
        wchar_t buf[128];
        FormatHotkey(g_hkUp,buf,128); SetWindowTextW(eUp,buf);
        FormatHotkey(g_hkDn,buf,128); SetWindowTextW(eDn,buf);

        g_capUp.origProc=(WNDPROC)SetWindowLongPtrW(eUp,GWLP_WNDPROC,(LONG_PTR)HkEditProc);
        g_capDn.origProc=(WNDPROC)SetWindowLongPtrW(eDn,GWLP_WNDPROC,(LONG_PTR)HkEditProc);

        // Apply font to all children
        HFONT hf=CreateFontW(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        EnumChildWindows(hwnd,[](HWND child,LPARAM lp)->BOOL{
            SendMessage(child,WM_SETFONT,lp,TRUE); return TRUE;
        },(LPARAM)hf);
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,RGB(220,220,220)); SetBkColor(hdc,RGB(30,30,36));
        return (LRESULT)CreateSolidBrush(RGB(30,30,36));
    }
    case WM_CTLCOLORBTN: {
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,RGB(220,220,220)); SetBkColor(hdc,RGB(44,44,52));
        return (LRESULT)CreateSolidBrush(RGB(44,44,52));
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_BTN_CLEAR_UP:
            g_capUp.current={0,0};
            SetWindowTextW(GetDlgItem(hwnd,ID_HK_UP_BOX),L"(not set)"); break;
        case ID_BTN_CLEAR_DN:
            g_capDn.current={0,0};
            SetWindowTextW(GetDlgItem(hwnd,ID_HK_DN_BOX),L"(not set)"); break;
        case ID_BTN_SAVE:
            g_hkUp=g_capUp.current; g_hkDn=g_capDn.current;
            Hotkeys_Register(); DestroyWindow(hwnd); break;
        case ID_BTN_CANCEL:
            DestroyWindow(hwnd); break;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(24,24,30));
        FillRect(hdc,&rc,bg); DeleteObject(bg);
        EndPaint(hwnd,&ps); break;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: g_hwndSettings=nullptr; break;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

void ShowSettings()
{
    if (g_hwndSettings) { SetForegroundWindow(g_hwndSettings); return; }

    static bool reg=false;
    if (!reg) {
        WNDCLASSW wcs={};
        wcs.lpfnWndProc=SettingsProc; wcs.hInstance=g_hInst;
        wcs.hbrBackground=CreateSolidBrush(RGB(24,24,30));
        wcs.lpszClassName=L"LuxDimSettings";
        wcs.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassW(&wcs); reg=true;
    }

    int W=246, H=166;
    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);
    g_hwndSettings=CreateWindowExW(WS_EX_TOOLWINDOW,
        L"LuxDimSettings", L"LuxDim - Hotkey Settings",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
        (sx-W)/2,(sy-H)/2,W,H,
        g_hwndMain,nullptr,g_hInst,nullptr);
    ShowWindow(g_hwndSettings,SW_SHOW);
    SetForegroundWindow(g_hwndSettings);
}

// ---------------------------------------------------------------------------
// Main invisible window
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        Tray_Add(hwnd);
        Hotkeys_Register();
        SetTimer(hwnd, TIMER_REFRESH, 5000, nullptr);
        break;

    case WM_HOTKEY:
        if (wp==HOTKEY_UP) {
            g_brightness=min(100,g_brightness+STEP);
        } else if (wp==HOTKEY_DOWN) {
            g_brightness=max(0,g_brightness-STEP);
        } else break;
        DDC_SetBrightness(g_brightness);
        if (g_popupVisible) { SendMessage(g_hwndSlider,TBM_SETPOS,TRUE,100-g_brightness); UpdateLabel(); }
        Tray_Update();
        break;

    case WM_TIMER:
        if (wp==TIMER_REFRESH&&!g_popupVisible) {
            int read=DDC_GetBrightness();
            if (read!=g_brightness) { g_brightness=read; Tray_Update(); }
        }
        break;

    case WM_TRAY:
        switch(LOWORD(lp)) {
        case WM_LBUTTONUP:
            if (g_popupVisible) HidePopup(); else ShowPopup(); break;
        case WM_RBUTTONUP: {
            POINT pt; GetCursorPos(&pt);
            HMENU menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,ID_SETTINGS,L"Settings");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,ID_EXIT,L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu,TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,nullptr);
            DestroyMenu(menu); break;
        }
        }
        break;

    case WM_COMMAND:
        if      (LOWORD(wp)==ID_SETTINGS) ShowSettings();
        else if (LOWORD(wp)==ID_EXIT)     DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        Hotkeys_Unregister();
        KillTimer(hwnd,TIMER_REFRESH);
        Tray_Remove();
        if (g_monitors&&g_monCount) DestroyPhysicalMonitors(g_monCount,g_monitors);
        delete[] g_monitors;
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst=hInst;
    INITCOMMONCONTROLSEX icx={sizeof(icx),ICC_BAR_CLASSES};
    InitCommonControlsEx(&icx);

    DDC_EnumerateMonitors();
    g_brightness=DDC_GetBrightness();

    WNDCLASSW wc={};
    wc.lpfnWndProc=MainProc; wc.hInstance=hInst; wc.lpszClassName=L"LuxDimMain";
    RegisterClassW(&wc);
    g_hwndMain=CreateWindowW(L"LuxDimMain",L"LuxDim",WS_POPUP,0,0,0,0,nullptr,nullptr,hInst,nullptr);

    WNDCLASSW wcp={};
    wcp.lpfnWndProc=PopupProc; wcp.hInstance=hInst;
    wcp.hbrBackground=CreateSolidBrush(RGB(24,24,28));
    wcp.lpszClassName=L"LuxDimPopup";
    RegisterClassW(&wcp);
    g_hwndPopup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"LuxDimPopup",nullptr,
        WS_POPUP|WS_CLIPCHILDREN,0,0,POPUP_W,POPUP_H,g_hwndMain,nullptr,hInst,nullptr);

    g_hwndSlider=CreateWindowExW(0,TRACKBAR_CLASSW,nullptr,
        WS_CHILD|WS_VISIBLE|TBS_VERT|TBS_NOTICKS|TBS_BOTH,
        18,28,34,150,g_hwndPopup,(HMENU)ID_SLIDER,hInst,nullptr);
    SendMessage(g_hwndSlider,TBM_SETRANGE,FALSE,MAKELPARAM(0,100));
    SendMessage(g_hwndSlider,TBM_SETPOS,TRUE,100-g_brightness);

    g_hwndLabel=CreateWindowExW(0,L"STATIC",L"50",WS_CHILD|WS_VISIBLE|SS_CENTER,
        8,184,POPUP_W-16,22,g_hwndPopup,nullptr,hInst,nullptr);
    HFONT hf=CreateFontW(18,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    SendMessage(g_hwndLabel,WM_SETFONT,(WPARAM)hf,TRUE);
    UpdateLabel();

    MSG msg;
    while (GetMessage(&msg,nullptr,0,0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
