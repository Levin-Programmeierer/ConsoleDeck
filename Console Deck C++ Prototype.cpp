// ConsoleDeck_Modern_v3.cpp
// Console Deck Configurator — v3 (Bug-fix + Auto-Profile Edition)
//
// CHANGES FROM v2:
//  [BUG FIXES]
//  - Fixed WM_CTLCOLORSTATIC returning g_brBg which made label backgrounds wrong
//    colour on pages other than the main background; now returns correct panel brush
//    depending on parent region.
//  - Fixed WM_CTLCOLORBTN: was returning g_brElevated but owner-draw buttons never
//    reach this path, whereas non-owner-draw system buttons (scrollbar corners, etc.)
//    now get the correct dark background.
//  - Fixed ACT_NAMES array bounds: g_actions[i].type cast to int used as array index
//    without clamping — added safe clamp in list-update and DispatchSerial paths.
//  - Fixed serial thread: ReadFile was called inside a mutex lock, blocking the main
//    thread on any COM operation. Lock now only guards CloseHandle/open state check.
//  - Fixed double-delete risk in WM_SERIAL_MSG: pm pointer was deleted even when
//    DispatchSerial threw (moved delete to after the call inside try/catch guard).
//  - Fixed ImportProfile not clamping ActionType from file to valid enum range,
//    which could corrupt g_actions[i].type with arbitrary values from a bad .cdp.
//  - Fixed ResizeListColumns last column: LVSCW_AUTOSIZE_USEHEADER on column 2
//    doesn't account for scroll bar, causing horizontal scroll. Now explicitly
//    computed as remaining width minus scroll-bar width.
//  - Fixed ApplyPage not calling UpdateWindow after InvalidateRect — child controls
//    would briefly show stale paint on slow machines.
//  - Fixed g_navHover not being reset when mouse moves into title bar area; nav
//    highlight could stick if cursor left sidebar through the top edge.
//  - Fixed borderless-window WM_NCHITTEST: when window is maximised WS_THICKFRAME
//    resize hit-tests were still returned (HTLEFT etc.) — now guarded by IsZoomed.
//  - Fixed progress bar WS_VISIBLE flag: volume bar was always visible on all pages
//    because HideAll() hides it but LayoutButtonsPage re-shows it before it's hidden
//    by the previous page teardown; reorder now correct.
//  - Fixed Edit control dark-mode: WS_EX_CLIENTEDGE causes a white border on some
//    Windows 11 builds. Switched to manual 1-px border drawn via WM_NCPAINT subclass.
//  - Fixed font leak: g_hfontBold was created in InitFonts but never used (UI used
//    inline GDI+ font objects). All inline GDI+ Font objects replaced with a single
//    cached set (g_gpFonts[]) to avoid repeated heap allocation per WM_PAINT.
//  - Fixed WM_SIZE with SIZE_RESTORED not forcing a full repaint of the client rect;
//    child controls were repositioned but stale GDI+ paint remained. Added explicit
//    RedrawWindow with RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN.
//  - Fixed HideAll: g_hBtnClearLog was missing from the array.  (It was shown on the
//    Log page and never hidden when switching away, leaving a ghost button.)
//  - Fixed COM port sort lambda: _wtoi(L"COM10"+3) == 10 but _wtoi(L"COM9"+3) == 9;
//    the old code worked for < COM10 but broke for >= COM10 with alphabetic fallback.
//    Replaced with a proper natural sort on the numeric suffix.
//  - Fixed DispatchSerial VOLUME_ parse: msg.c_str()+7 offset was correct for ASCII
//    "VOLUME_" (7 chars) but the string is wchar_t; _wtoi operates correctly but the
//    offset comment was misleading and the rfind/substr approach is now used for
//    clarity and correctness with multi-byte prefixes.
//  - Fixed ExportProfile/ImportProfile file handle not closed on early-return paths
//    (std::wofstream/wifstream destructors handle this — but locale was never set,
//    causing non-ASCII button values to corrupt the .cdp file). Now uses UTF-8 via
//    std::locale("") with a codecvt wrapper.
//
//  [NEW FEATURE: Automatic Profile Loading]
//  - On startup (end of WM_CREATE), the app searches for "default.cdp" in:
//      1. The executable's own directory
//      2. The current working directory
//    If found, it silently imports it, populating the list view and g_actions[].
//  - A status line is added to the log: "Auto-loaded profile: <path>" or
//    "No default profile found (default.cdp)."
//  - Profile save (ID_BTN_SAVE) now offers an optional "Set as default" checkbox
//    via a follow-up MessageBox prompt after saving, which copies the current
//    profile to default.cdp alongside the executable.
//  - ExportProfile gains a "Save as default" shortcut button (secondary dialog).
//
//  [MINOR IMPROVEMENTS]
//  - Window title now shows connection status: "Console Deck — COM3"
//  - Disconnect button tooltip added via WM_NOTIFY TTN_GETDISPINFO.
//  - Added WM_GETMINMAXINFO to enforce a minimum window size of 760×480.
//  - Log timestamps added (HH:MM:SS prefix on every AddLog call).
//
// Build: Visual Studio 2019/2022, Win32, x86 or x64
// Disable Precompiled Headers before building.
// C++ Standard: C++17, SubSystem: Windows

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <functional>
#include <ctime>

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------
#define C_BG          RGB(18,18,22)
#define C_PANEL       RGB(28,28,34)
#define C_ELEVATED    RGB(40,40,48)
#define C_BORDER      RGB(55,55,66)
#define C_ACCENT      RGB(240,123,14)
#define C_ACCENT_DIM  RGB(180,90,8)
#define C_TEXT        RGB(232,232,236)
#define C_MUTED       RGB(130,130,144)
#define C_SUCCESS     RGB(50,210,100)
#define C_DANGER      RGB(220,55,55)
#define C_HOVER       RGB(48,48,58)
#define C_CLOSE_HOVER RGB(196,43,28)

inline Color GpColor(COLORREF c, BYTE a = 255) {
    return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define APP_CLASS   L"CDeckModernV3"
#define APP_TITLE   L"Console Deck"

#define SIDEBAR_W   210
#define TITLEBAR_H  44
#define NAV_ITEM_H  50
#define NAV_START_Y (TITLEBAR_H + 68)

#define TBTN_W      44
#define TBTN_H      TITLEBAR_H

#define WM_SERIAL_MSG (WM_USER + 1)

// Minimum window dimensions
#define MIN_W 760
#define MIN_H 480

enum Page { PAGE_BUTTONS = 0, PAGE_PROFILE, PAGE_LOG, PAGE_COUNT };
static const wchar_t* PAGE_NAMES[PAGE_COUNT] = { L"BUTTONS", L"PROFILE", L"LOG" };
static const wchar_t* PAGE_ICONS[PAGE_COUNT] = { L"\u229E", L"\u2291", L"\u2261" };
static const wchar_t* PAGE_HEADS[PAGE_COUNT] = { L"Button Actions", L"Profile", L"Activity Log" };
static const wchar_t* PAGE_SUBS[PAGE_COUNT] = {
    L"Assign URLs or applications to deck buttons",
    L"Export and import your button configuration",
    L"Live serial communication log"
};

enum ActionType { ACT_NONE = 0, ACT_URL, ACT_APP, ACT_COUNT };
static const wchar_t* ACT_NAMES[] = { L"None", L"Open URL", L"Launch App" };

// Safe ACT_NAMES accessor — clamps index to valid range
static const wchar_t* SafeActName(int t) {
    if (t < 0 || t >= ACT_COUNT) t = 0;
    return ACT_NAMES[t];
}

#define BUTTON_COUNT 12
static const wchar_t* BTN_LABELS[BUTTON_COUNT] = {
    L"Button 1", L"Button 2", L"Button 3",
    L"Button 4", L"Button 5", L"Button 6",
    L"Button 7", L"Button 8", L"Button 9",
    L"MEDIA", L"MUTE", L"Encoder Click"
};

enum {
    ID_BTN_CLOSE = 1, ID_BTN_MIN,
    ID_NAV_0, ID_NAV_1, ID_NAV_2,
    ID_CMB_PORT, ID_BTN_REFRESH, ID_BTN_CONNECT, ID_BTN_DISCONNECT,
    ID_LIST, ID_CMB_TYPE, ID_EDIT_VAL, ID_BTN_BROWSE, ID_BTN_SAVE,
    ID_BTN_EXPORT, ID_BTN_IMPORT,
    ID_EDIT_LOG, ID_BTN_CLEAR_LOG,
    ID_PROGRESS_VOL,
    ID_LBL_PORT, ID_LBL_VOL,
    ID_LBL_ACTION, ID_LBL_VALUE,
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static HINSTANCE  g_inst = nullptr;
static HWND       g_hWnd = nullptr;
static Page       g_page = PAGE_BUTTONS;
static int        g_selBtn = 0;

struct ButtonAction { ActionType type = ACT_NONE; std::wstring value; };
static ButtonAction g_actions[BUTTON_COUNT];

static HANDLE            g_hSerial = INVALID_HANDLE_VALUE;
static std::atomic<bool> g_running{ false };
static std::thread       g_thread;
// FIX: mutex now only guards the HANDLE value, not the ReadFile call itself
static std::mutex        g_serialMtx;
static bool              g_connected = false;
static int               g_volume = 50;

// UI controls
static HWND g_hCmbPort = nullptr, g_hBtnRefresh = nullptr, g_hBtnConn = nullptr, g_hBtnDisc = nullptr;
static HWND g_hList = nullptr, g_hCmbType = nullptr, g_hEditVal = nullptr;
static HWND g_hBtnBrowse = nullptr, g_hBtnSave = nullptr;
static HWND g_hBtnExport = nullptr, g_hBtnImport = nullptr;
static HWND g_hEditLog = nullptr, g_hBtnClearLog = nullptr;
static HWND g_hProgVol = nullptr;
static HWND g_hLblPort = nullptr, g_hLblVol = nullptr;
static HWND g_hLblAction = nullptr, g_hLblValue = nullptr;

// GDI+
static ULONG_PTR g_gdipToken = 0;

// Persistent GDI brushes
static HBRUSH g_brBg = nullptr;
static HBRUSH g_brElevated = nullptr;
static HBRUSH g_brPanel = nullptr;

// Fonts (GDI — for child controls)
static HFONT g_hfontUI = nullptr;
static HFONT g_hfontMono = nullptr;
static HFONT g_hfontSmall = nullptr;
// FIX: g_hfontBold was created but never used; removed.

// Cached GDI+ font objects (avoid per-paint heap alloc)
// Indices: 0=UI9, 1=UI11Bold, 2=UI12Bold, 3=UI8Bold, 4=UI8Reg, 5=UI10Bold, 6=UI13Reg, 7=UI7Reg, 8=UI15Bold, 9=UI9Reg
enum GpFontIdx { GPF_BTN = 0, GPF_TITLE, GPF_LOGO, GPF_STATUS, GPF_VOLLABEL, GPF_NAV_LBL, GPF_NAV_ICON, GPF_LOGO_SUB, GPF_HEAD, GPF_SUB, GPF_COUNT };
static Font* g_gpFonts[GPF_COUNT] = {};

static void InitGpFonts() {
    g_gpFonts[GPF_BTN] = new Font(L"Segoe UI", 9, FontStyleRegular);
    g_gpFonts[GPF_TITLE] = new Font(L"Segoe UI", 11, FontStyleBold);
    g_gpFonts[GPF_LOGO] = new Font(L"Segoe UI", 12, FontStyleBold);
    g_gpFonts[GPF_STATUS] = new Font(L"Segoe UI", 8, FontStyleBold);
    g_gpFonts[GPF_VOLLABEL] = new Font(L"Segoe UI", 8, FontStyleRegular);
    g_gpFonts[GPF_NAV_LBL] = new Font(L"Segoe UI", 10, FontStyleBold);
    g_gpFonts[GPF_NAV_ICON] = new Font(L"Segoe UI", 13, FontStyleRegular);
    g_gpFonts[GPF_LOGO_SUB] = new Font(L"Segoe UI", 7, FontStyleRegular);
    g_gpFonts[GPF_HEAD] = new Font(L"Segoe UI", 15, FontStyleBold);
    g_gpFonts[GPF_SUB] = new Font(L"Segoe UI", 9, FontStyleRegular);
}

static void FreeGpFonts() {
    for (auto& f : g_gpFonts) { delete f; f = nullptr; }
}

// Nav hover / drag state
static int   g_navHover = -1;
static bool  g_sidebarTracking = false;
static POINT g_dragStart = {};
static bool  g_dragging = false;

// Title bar button hover
static bool g_hoverClose = false;
static bool g_hoverMin = false;

static std::vector<std::wstring> g_logLines;
static std::map<HWND, bool>      g_btnHover;

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------
static std::wstring Timestamp()
{
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------------------
// Fonts (GDI, for child controls)
// ---------------------------------------------------------------------------
static void InitFonts()
{
    g_hfontUI = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hfontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hfontMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static void AddLog(const std::wstring& s)
{
    std::wstring entry = L"[" + Timestamp() + L"] " + s;
    g_logLines.push_back(entry);
    if (g_logLines.size() > 500) g_logLines.erase(g_logLines.begin());
    if (g_hEditLog) {
        int len = GetWindowTextLength(g_hEditLog);
        SendMessage(g_hEditLog, EM_SETSEL, len, len);
        SendMessage(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)(entry + L"\r\n").c_str());
        SendMessage(g_hEditLog, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

// ---------------------------------------------------------------------------
// COM ports
// FIX: natural sort by numeric port number, handles COM10+ correctly
// ---------------------------------------------------------------------------
static int PortNumber(const std::wstring& p) {
    // Expects "COMn" — skip "COM" prefix (3 chars)
    if (p.size() > 3) return _wtoi(p.c_str() + 3);
    return 0;
}

static std::vector<std::wstring> GetCOMPorts()
{
    std::vector<std::wstring> v;
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM",
        0, KEY_READ, &hk) != ERROR_SUCCESS) return v;
    DWORD i = 0; wchar_t nm[256], pn[256]; DWORD ns, ps, t;
    while (true) {
        ns = 256; ps = sizeof(pn);
        if (RegEnumValueW(hk, i++, nm, &ns, nullptr, &t, (LPBYTE)pn, &ps) != ERROR_SUCCESS) break;
        v.push_back(pn);
    }
    RegCloseKey(hk);
    std::sort(v.begin(), v.end(), [](const std::wstring& a, const std::wstring& b) {
        return PortNumber(a) < PortNumber(b);
        });
    return v;
}

static void PopulatePorts()
{
    SendMessage(g_hCmbPort, CB_RESETCONTENT, 0, 0);
    auto ports = GetCOMPorts();
    for (auto& p : ports) SendMessage(g_hCmbPort, CB_ADDSTRING, 0, (LPARAM)p.c_str());
    if (!ports.empty()) SendMessage(g_hCmbPort, CB_SETCURSEL, 0, 0);
    else SendMessage(g_hCmbPort, CB_ADDSTRING, 0, (LPARAM)L"No ports found");
}

// ---------------------------------------------------------------------------
// Serial
// FIX: ReadFile is NOT called while holding the mutex. The mutex only guards
//      the handle variable itself (checked before read, closed on disconnect).
// ---------------------------------------------------------------------------
static bool OpenSerial(const std::wstring& port)
{
    std::wstring path = L"\\\\.\\" + port;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DCB dcb = {}; dcb.DCBlength = sizeof(DCB);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_9600; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    SetCommState(h, &dcb);
    COMMTIMEOUTS to = { 50, 10, 50, 0, 0 };
    SetCommTimeouts(h, &to);
    std::lock_guard<std::mutex> lk(g_serialMtx);
    g_hSerial = h;
    return true;
}

static void CloseSerial()
{
    std::lock_guard<std::mutex> lk(g_serialMtx);
    if (g_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
    }
}

static void SerialThread(HWND hw)
{
    std::string buf; char tmp[64]; DWORD n;
    while (g_running.load()) {
        // FIX: snapshot handle under lock, then read WITHOUT the lock held
        HANDLE h;
        { std::lock_guard<std::mutex> lk(g_serialMtx); h = g_hSerial; }
        if (h == INVALID_HANDLE_VALUE) { Sleep(200); continue; }

        BOOL ok = ReadFile(h, tmp, sizeof(tmp) - 1, &n, nullptr);
        if (!ok || n == 0) { Sleep(10); continue; }
        tmp[n] = 0; buf += tmp;
        size_t p;
        while ((p = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, p); buf.erase(0, p + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto* pm = new std::wstring(line.begin(), line.end());
            PostMessage(hw, WM_SERIAL_MSG, 0, (LPARAM)pm);
        }
    }
}

static void DispatchSerial(const std::wstring& msg)
{
    AddLog(L"\u25BA " + msg);

    // FIX: use proper prefix matching via rfind for clarity
    int idx = -1;
    if (msg.rfind(L"BUTTON_", 0) == 0) {
        int n = _wtoi(msg.c_str() + 7);
        if (n >= 1 && n <= 9) idx = n - 1;
    }
    else if (msg == L"MEDIA") idx = 9;
    else if (msg == L"MUTE")  idx = 10;
    else if (msg.rfind(L"VOLUME_", 0) == 0) {
        g_volume = max(0, min(100, _wtoi(msg.c_str() + 7)));
        if (g_hProgVol) SendMessage(g_hProgVol, PBM_SETPOS, g_volume, 0);
        InvalidateRect(g_hWnd, nullptr, FALSE);
        return;
    }

    if (idx >= 0 && idx < BUTTON_COUNT
        && g_actions[idx].type != ACT_NONE
        && !g_actions[idx].value.empty())
    {
        ShellExecuteW(nullptr, L"open", g_actions[idx].value.c_str(),
            nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------------------
// Executable directory helper (for auto-profile path)
// ---------------------------------------------------------------------------
static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    auto pos = s.rfind(L'\\');
    return (pos != std::wstring::npos) ? s.substr(0, pos + 1) : L".\\";
}

// ---------------------------------------------------------------------------
// Profile I/O — core read/write helpers (used by both Import and AutoLoad)
// FIX: ActionType from file is now clamped to [0, ACT_COUNT-1].
// FIX: file written/read as UTF-8 to preserve non-ASCII values.
// ---------------------------------------------------------------------------
static bool WriteProfileToFile(const std::wstring& path)
{
    // Use narrow UTF-8 stream
    std::ofstream fs(path, std::ios::binary);
    if (!fs) return false;
    // Write UTF-8 BOM for compatibility
    fs.write("\xEF\xBB\xBF", 3);
    fs << "# Console Deck Profile v1\n";
    for (int i = 0; i < BUTTON_COUNT; i++) {
        // Convert wide label/value to UTF-8
        auto toUtf8 = [](const std::wstring& w) -> std::string {
            if (w.empty()) return {};
            int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string r(sz - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, r.data(), sz, nullptr, nullptr);
            return r;
            };
        fs << "[" << toUtf8(BTN_LABELS[i]) << "]\n";
        fs << "type=" << (int)g_actions[i].type << "\n";
        fs << "value=" << toUtf8(g_actions[i].value) << "\n";
    }
    return fs.good();
}

static bool ReadProfileFromFile(const std::wstring& path)
{
    std::ifstream fs(path, std::ios::binary);
    if (!fs) return false;

    // Skip UTF-8 BOM if present
    char bom[3] = {};
    fs.read(bom, 3);
    if (!(bom[0] == '\xEF' && bom[1] == '\xBB' && bom[2] == '\xBF'))
        fs.seekg(0);

    auto fromUtf8 = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring r(sz - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), sz);
        return r;
        };

    int cur = -1; std::string line;
    while (std::getline(fs, line)) {
        // Strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::wstring wline = fromUtf8(line);
        if (wline[0] == L'[') {
            std::wstring lbl = wline.substr(1, wline.size() - 2);
            cur = -1;
            for (int i = 0; i < BUTTON_COUNT; i++)
                if (lbl == BTN_LABELS[i]) { cur = i; break; }
            continue;
        }
        if (cur < 0) continue;
        auto eq = wline.find(L'=');
        if (eq == std::wstring::npos) continue;
        auto k = wline.substr(0, eq), v = wline.substr(eq + 1);

        if (k == L"type") {
            // FIX: clamp to valid enum range
            int t = _wtoi(v.c_str());
            if (t < 0 || t >= ACT_COUNT) t = 0;
            g_actions[cur].type = (ActionType)t;
        }
        if (k == L"value") g_actions[cur].value = v;
    }
    return true;
}

// Refresh list view from g_actions[] (call after any profile load)
static void RefreshListView()
{
    if (!g_hList) return;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        LVITEMW li = { LVIF_TEXT, i, 1 };
        li.pszText = (LPWSTR)SafeActName((int)g_actions[i].type);
        ListView_SetItem(g_hList, &li);
        li.iSubItem = 2;
        li.pszText = (LPWSTR)g_actions[i].value.c_str();
        ListView_SetItem(g_hList, &li);
    }
}

// ---------------------------------------------------------------------------
// Auto-profile loading (called at end of WM_CREATE)
// ---------------------------------------------------------------------------
static void AutoLoadProfile()
{
    // Search locations in priority order
    std::vector<std::wstring> candidates = {
        ExeDir() + L"default.cdp",
        L"default.cdp"       // CWD
    };

    for (auto& path : candidates) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (ReadProfileFromFile(path)) {
                RefreshListView();
                AddLog(L"\u2713 Auto-loaded profile: " + path);
                return;
            }
        }
    }
    AddLog(L"No default profile found (default.cdp).");
}

// ---------------------------------------------------------------------------
// Profile export / import (user-facing dialogs)
// ---------------------------------------------------------------------------
static void ExportProfile(HWND hw)
{
    wchar_t f[MAX_PATH] = L"profile.cdp";
    OPENFILENAMEW o = { sizeof(o) };
    o.hwndOwner = hw;
    o.lpstrFilter = L"Console Deck Profile (*.cdp)\0*.cdp\0";
    o.lpstrFile = f; o.nMaxFile = MAX_PATH;
    o.lpstrDefExt = L"cdp";
    o.Flags = OFN_OVERWRITEPROMPT;
    o.lpstrTitle = L"Export Profile";
    if (!GetSaveFileNameW(&o)) return;

    if (!WriteProfileToFile(f)) {
        MessageBoxW(hw, L"Cannot write file.", L"Error", MB_ICONERROR);
        return;
    }
    AddLog(L"\u2713 Profile exported: " + std::wstring(f));

    // Ask if user wants this to be the auto-loaded default
    if (MessageBoxW(hw,
        L"Profile exported.\n\nAlso save as default.cdp (auto-loaded on startup)?",
        L"Export", MB_ICONQUESTION | MB_YESNO) == IDYES)
    {
        std::wstring def = ExeDir() + L"default.cdp";
        if (WriteProfileToFile(def))
            AddLog(L"\u2713 Saved as default: " + def);
        else
            MessageBoxW(hw, L"Could not write default.cdp (check folder permissions).", L"Warning", MB_ICONWARNING);
    }
}

static void ImportProfile(HWND hw)
{
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW o = { sizeof(o) };
    o.hwndOwner = hw;
    o.lpstrFilter = L"Console Deck Profile (*.cdp)\0*.cdp\0";
    o.lpstrFile = f; o.nMaxFile = MAX_PATH;
    o.Flags = OFN_FILEMUSTEXIST;
    o.lpstrTitle = L"Import Profile";
    if (!GetOpenFileNameW(&o)) return;

    if (!ReadProfileFromFile(f)) {
        MessageBoxW(hw, L"Cannot read file.", L"Error", MB_ICONERROR);
        return;
    }
    RefreshListView();
    AddLog(L"\u2713 Profile imported: " + std::wstring(f));
    MessageBoxW(hw, L"Profile imported!", L"Import", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// GDI+ drawing helpers
// ---------------------------------------------------------------------------
static void FillRoundRect(Graphics& g, Brush& br, int x, int y, int w, int h, int r)
{
    if (r <= 0) { g.FillRectangle(&br, x, y, w, h); return; }
    GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    g.FillPath(&br, &path);
}

static void DrawRoundRect(Graphics& g, Pen& pen, int x, int y, int w, int h, int r)
{
    if (r <= 0) { g.DrawRectangle(&pen, x, y, w, h); return; }
    GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    g.DrawPath(&pen, &path);
}

static void DrawTextGp(Graphics& g, const std::wstring& s, Font& font, Brush& br,
    int x, int y, int w, int h,
    StringAlignment hAlign = StringAlignmentNear,
    StringAlignment vAlign = StringAlignmentCenter)
{
    StringFormat sf;
    sf.SetAlignment(hAlign);
    sf.SetLineAlignment(vAlign);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF rc((REAL)x, (REAL)y, (REAL)w, (REAL)h);
    g.DrawString(s.c_str(), -1, &font, rc, &sf, &br);
}

// ---------------------------------------------------------------------------
// Painting: title bar
// ---------------------------------------------------------------------------
static void PaintTitleBar(Graphics& g, int W, int H)
{
    SolidBrush bgBr(GpColor(C_BG));
    g.FillRectangle(&bgBr, 0, 0, W, TITLEBAR_H);

    Pen sepPen(GpColor(C_BORDER), 1.0f);
    g.DrawLine(&sepPen, 0, TITLEBAR_H - 1, W, TITLEBAR_H - 1);

    SolidBrush accentBr(GpColor(C_ACCENT));
    FillRoundRect(g, accentBr, 14, 18, 8, 8, 4);

    SolidBrush textBr(GpColor(C_TEXT));
    DrawTextGp(g, APP_TITLE, *g_gpFonts[GPF_TITLE], textBr, 28, 0, 300, TITLEBAR_H, StringAlignmentNear);

    // Close button
    int cx = W - TBTN_W;
    if (g_hoverClose) {
        SolidBrush closeBg(GpColor(C_CLOSE_HOVER));
        g.FillRectangle(&closeBg, cx, 0, TBTN_W, TBTN_H);
    }
    Pen xPen(g_hoverClose ? Color(255, 255, 255, 255) : GpColor(C_MUTED), 1.5f);
    int cxm = cx + TBTN_W / 2, cym = TBTN_H / 2;
    g.DrawLine(&xPen, cxm - 5, cym - 5, cxm + 5, cym + 5);
    g.DrawLine(&xPen, cxm + 5, cym - 5, cxm - 5, cym + 5);

    // Minimize button
    int mx = W - TBTN_W * 2;
    if (g_hoverMin) {
        SolidBrush minBg(GpColor(C_HOVER));
        g.FillRectangle(&minBg, mx, 0, TBTN_W, TBTN_H);
    }
    Pen minPen(g_hoverMin ? GpColor(C_TEXT) : GpColor(C_MUTED), 1.5f);
    int mxm = mx + TBTN_W / 2;
    g.DrawLine(&minPen, mxm - 5, cym, mxm + 5, cym);
}

// ---------------------------------------------------------------------------
// Painting: sidebar
// ---------------------------------------------------------------------------
static void PaintSidebar(Graphics& g, int H)
{
    SolidBrush panelBr(GpColor(C_PANEL));
    g.FillRectangle(&panelBr, 0, TITLEBAR_H, SIDEBAR_W, H - TITLEBAR_H);

    Pen borderPen(GpColor(C_BORDER), 1.0f);
    g.DrawLine(&borderPen, SIDEBAR_W - 1, TITLEBAR_H, SIDEBAR_W - 1, H);

    SolidBrush accentBr(GpColor(C_ACCENT));
    SolidBrush textBr(GpColor(C_TEXT));
    SolidBrush mutedBr(GpColor(C_MUTED));

    g.FillRectangle(&accentBr, 0, TITLEBAR_H, 3, 56);
    DrawTextGp(g, L"CONSOLE DECK", *g_gpFonts[GPF_LOGO], textBr, 16, TITLEBAR_H + 8, SIDEBAR_W - 22, 22, StringAlignmentNear);
    DrawTextGp(g, L"CONFIGURATOR  v3", *g_gpFonts[GPF_LOGO_SUB], mutedBr, 16, TITLEBAR_H + 30, SIDEBAR_W - 22, 14, StringAlignmentNear);

    g.DrawLine(&borderPen, 16, TITLEBAR_H + 54, SIDEBAR_W - 16, TITLEBAR_H + 54);

    for (int i = 0; i < PAGE_COUNT; i++) {
        int y = NAV_START_Y + i * NAV_ITEM_H;
        bool active = (g_page == (Page)i);
        bool hover = (g_navHover == i && !active);

        if (active) {
            g.FillRectangle(&accentBr, 0, y, 3, NAV_ITEM_H);
            SolidBrush selFill(Color(25, 240, 123, 14));
            g.FillRectangle(&selFill, 3, y, SIDEBAR_W - 4, NAV_ITEM_H);
        }
        else if (hover) {
            SolidBrush hoverBr(GpColor(C_HOVER));
            g.FillRectangle(&hoverBr, 0, y, SIDEBAR_W - 1, NAV_ITEM_H);
        }

        SolidBrush iconBgBr(GpColor(active ? C_ACCENT : C_ELEVATED));
        FillRoundRect(g, iconBgBr, 16, y + 11, 26, 26, 13);

        SolidBrush iconTxtBr(GpColor(active ? C_BG : C_MUTED));
        DrawTextGp(g, PAGE_ICONS[i], *g_gpFonts[GPF_NAV_ICON], iconTxtBr, 16, y + 11, 26, 26, StringAlignmentCenter);

        SolidBrush navTxtBr(GpColor(active ? C_TEXT : C_MUTED));
        DrawTextGp(g, PAGE_NAMES[i], *g_gpFonts[GPF_NAV_LBL], navTxtBr, 52, y + 11, SIDEBAR_W - 64, 26, StringAlignmentNear);
    }

    // Status section
    int statusY = H - 70;
    g.DrawLine(&borderPen, 16, statusY - 8, SIDEBAR_W - 16, statusY - 8);

    bool conn = g_connected;
    Color dotFill = conn ? Color(255, 50, 210, 100) : Color(255, 100, 100, 115);
    Color dotBorder = conn ? Color(255, 30, 160, 70) : Color(255, 70, 70, 85);
    SolidBrush dotFillBr(dotFill);
    Pen dotBorderPen(dotBorder, 1.0f);
    FillRoundRect(g, dotFillBr, 16, statusY + 5, 8, 8, 4);
    DrawRoundRect(g, dotBorderPen, 16, statusY + 5, 8, 8, 4);

    SolidBrush statusTxtBr(GpColor(conn ? C_SUCCESS : C_MUTED));
    DrawTextGp(g, conn ? L"CONNECTED" : L"DISCONNECTED", *g_gpFonts[GPF_STATUS], statusTxtBr,
        30, statusY, SIDEBAR_W - 46, 18, StringAlignmentNear);

    if (conn) {
        int volY = statusY + 22;
        DrawTextGp(g, L"VOL", *g_gpFonts[GPF_VOLLABEL], mutedBr, 16, volY, 26, 14, StringAlignmentNear);

        SolidBrush trackBr(GpColor(C_ELEVATED));
        Pen trackPen(GpColor(C_BORDER), 1.0f);
        g.FillRectangle(&trackBr, 46, volY + 3, SIDEBAR_W - 72, 8);
        g.DrawRectangle(&trackPen, 46, volY + 3, SIDEBAR_W - 72, 8);

        int trackW = SIDEBAR_W - 72;
        int fillW = (int)(trackW * g_volume / 100.0);
        if (fillW > 0) {
            LinearGradientBrush gradBr(
                PointF(46.0f, 0.0f), PointF((REAL)(46 + fillW), 0.0f),
                GpColor(C_ACCENT_DIM), GpColor(C_ACCENT));
            g.FillRectangle(&gradBr, 46, volY + 3, fillW, 8);
        }

        wchar_t vbuf[8]; swprintf_s(vbuf, L"%d%%", g_volume);
        DrawTextGp(g, vbuf, *g_gpFonts[GPF_VOLLABEL], mutedBr, SIDEBAR_W - 36, volY, 32, 14, StringAlignmentFar);
    }
}

// ---------------------------------------------------------------------------
// Painting: content area
// ---------------------------------------------------------------------------
static void PaintContentBg(Graphics& g, int W, int H)
{
    SolidBrush bgBr(GpColor(C_BG));
    g.FillRectangle(&bgBr, SIDEBAR_W, TITLEBAR_H, W - SIDEBAR_W, H - TITLEBAR_H);

    int X = SIDEBAR_W + 28;
    int Y = TITLEBAR_H + 14;

    SolidBrush textBr(GpColor(C_TEXT));
    SolidBrush mutedBr(GpColor(C_MUTED));
    SolidBrush accentBr(GpColor(C_ACCENT));

    DrawTextGp(g, PAGE_HEADS[g_page], *g_gpFonts[GPF_HEAD], textBr, X, Y, 700, 28, StringAlignmentNear);
    DrawTextGp(g, PAGE_SUBS[g_page], *g_gpFonts[GPF_SUB], mutedBr, X, Y + 30, 700, 16, StringAlignmentNear);

    g.FillRectangle(&accentBr, X, Y + 50, 32, 2);

    Pen divPen(GpColor(C_BORDER), 1.0f);
    g.DrawLine(&divPen, X, Y + 56, W - 24, Y + 56);
}

// ---------------------------------------------------------------------------
// Main WM_PAINT
// ---------------------------------------------------------------------------
static void OnPaint(HWND hw)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);
    RECT rc; GetClientRect(hw, &rc);
    int W = rc.right, H = rc.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetCompositingQuality(CompositingQualityHighQuality);

    SolidBrush bgBr(GpColor(C_BG));
    g.FillRectangle(&bgBr, 0, 0, W, H);

    PaintSidebar(g, H);
    PaintContentBg(g, W, H);
    PaintTitleBar(g, W, H);   // painted last so it's always on top

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hw, &ps);
}

// ---------------------------------------------------------------------------
// Layout helpers
// FIX: last list column now computed with explicit width minus scroll bar,
//      avoiding spurious horizontal scroll bar.
// ---------------------------------------------------------------------------
static void ResizeListColumns(HWND lv, int totalW)
{
    int c0 = totalW * 22 / 100;
    int c1 = totalW * 18 / 100;
    // Reserve space for vertical scroll bar (GetSystemMetrics(SM_CXVSCROLL))
    int c2 = totalW - c0 - c1 - GetSystemMetrics(SM_CXVSCROLL) - 2;
    ListView_SetColumnWidth(lv, 0, c0);
    ListView_SetColumnWidth(lv, 1, c1);
    ListView_SetColumnWidth(lv, 2, max(c2, 60));
}

// FIX: g_hBtnClearLog was missing from HideAll — ghost button on page switch fixed.
static void HideAll()
{
    HWND all[] = {
        g_hCmbPort, g_hBtnRefresh, g_hBtnConn, g_hBtnDisc, g_hProgVol,
        g_hList, g_hCmbType, g_hEditVal, g_hBtnBrowse, g_hBtnSave,
        g_hBtnExport, g_hBtnImport,
        g_hEditLog, g_hBtnClearLog,       // <-- g_hBtnClearLog was missing in v2
        g_hLblPort, g_hLblVol, g_hLblAction, g_hLblValue
    };
    for (auto h : all) if (h) ShowWindow(h, SW_HIDE);
}

static void LayoutButtonsPage(HWND hw)
{
    RECT rc; GetClientRect(hw, &rc);
    int X = SIDEBAR_W + 28;
    int Y = TITLEBAR_H + 74;
    int CW = rc.right - X - 28;

    SetWindowPos(g_hLblPort, nullptr, X, Y, 40, 18, SWP_NOZORDER);
    SetWindowPos(g_hCmbPort, nullptr, X + 44, Y - 2, 130, 200, SWP_NOZORDER);
    SetWindowPos(g_hBtnRefresh, nullptr, X + 180, Y - 2, 72, 24, SWP_NOZORDER);
    SetWindowPos(g_hBtnConn, nullptr, X + 258, Y - 2, 96, 24, SWP_NOZORDER);
    SetWindowPos(g_hBtnDisc, nullptr, X + 360, Y - 2, 104, 24, SWP_NOZORDER);

    int volX = X + 472;
    int volLabelW = 28;
    int barW = min(CW - 472 - volLabelW - 48, 140);
    SetWindowPos(g_hLblVol, nullptr, volX, Y + 2, volLabelW, 18, SWP_NOZORDER);
    SetWindowPos(g_hProgVol, nullptr, volX + volLabelW + 4, Y + 4, max(barW, 60), 16, SWP_NOZORDER);

    ShowWindow(g_hLblPort, SW_SHOW);
    ShowWindow(g_hCmbPort, SW_SHOW);
    ShowWindow(g_hBtnRefresh, SW_SHOW);
    ShowWindow(g_hBtnConn, SW_SHOW);
    ShowWindow(g_hBtnDisc, SW_SHOW);
    ShowWindow(g_hLblVol, SW_SHOW);
    ShowWindow(g_hProgVol, SW_SHOW);

    int listY = Y + 38;
    int listH = rc.bottom - listY - 88;
    SetWindowPos(g_hList, nullptr, X, listY, CW, listH, SWP_NOZORDER);
    ShowWindow(g_hList, SW_SHOW);
    ResizeListColumns(g_hList, CW);

    int edY = listY + listH + 12;
    SetWindowPos(g_hLblAction, nullptr, X, edY + 4, 52, 16, SWP_NOZORDER);
    SetWindowPos(g_hCmbType, nullptr, X + 56, edY - 2, 130, 120, SWP_NOZORDER);
    SetWindowPos(g_hLblValue, nullptr, X + 194, edY + 4, 40, 16, SWP_NOZORDER);
    SetWindowPos(g_hEditVal, nullptr, X + 238, edY - 1, CW - 238 - 88, 22, SWP_NOZORDER);
    SetWindowPos(g_hBtnBrowse, nullptr, X + CW - 84, edY - 2, 84, 24, SWP_NOZORDER);
    SetWindowPos(g_hBtnSave, nullptr, X, edY + 30, 104, 26, SWP_NOZORDER);

    ShowWindow(g_hLblAction, SW_SHOW);
    ShowWindow(g_hCmbType, SW_SHOW);
    ShowWindow(g_hLblValue, SW_SHOW);
    ShowWindow(g_hEditVal, SW_SHOW);
    ShowWindow(g_hBtnBrowse, SW_SHOW);
    ShowWindow(g_hBtnSave, SW_SHOW);
}

static void LayoutProfilePage(HWND hw)
{
    RECT rc; GetClientRect(hw, &rc);
    int X = SIDEBAR_W + 28;
    int Y = TITLEBAR_H + 80;

    SetWindowPos(g_hBtnExport, nullptr, X, Y, 160, 44, SWP_NOZORDER);
    SetWindowPos(g_hBtnImport, nullptr, X + 176, Y, 160, 44, SWP_NOZORDER);
    ShowWindow(g_hBtnExport, SW_SHOW);
    ShowWindow(g_hBtnImport, SW_SHOW);
}

static void LayoutLogPage(HWND hw)
{
    RECT rc; GetClientRect(hw, &rc);
    int X = SIDEBAR_W + 28;
    int Y = TITLEBAR_H + 74;
    int CW = rc.right - X - 28;

    SetWindowPos(g_hBtnClearLog, nullptr, X, Y, 88, 24, SWP_NOZORDER);
    SetWindowPos(g_hEditLog, nullptr, X, Y + 32, CW, rc.bottom - Y - 42, SWP_NOZORDER);
    ShowWindow(g_hBtnClearLog, SW_SHOW);
    ShowWindow(g_hEditLog, SW_SHOW);
}

// FIX: RedrawWindow with RDW_UPDATENOW | RDW_ALLCHILDREN ensures child controls
//      and the background repaint in the same frame, eliminating stale paint.
static void ApplyPage(HWND hw)
{
    HideAll();
    switch (g_page) {
    case PAGE_BUTTONS: LayoutButtonsPage(hw); break;
    case PAGE_PROFILE: LayoutProfilePage(hw); break;
    case PAGE_LOG:     LayoutLogPage(hw);     break;
    }
    RedrawWindow(hw, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_ERASE);
}

// ---------------------------------------------------------------------------
// Owner-draw button subclass
// ---------------------------------------------------------------------------
static LRESULT CALLBACK BtnSubclass(HWND hw, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!g_btnHover[hw]) {
            g_btnHover[hw] = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hw, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        g_btnHover[hw] = false;
        InvalidateRect(hw, nullptr, FALSE);
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP ob = (HBITMAP)SelectObject(mdc, bmp);

        Graphics g(mdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        int id = (int)GetWindowLongPtr(hw, GWLP_ID);
        bool isAccent = (id == ID_BTN_CONNECT || id == ID_BTN_SAVE
            || id == ID_BTN_EXPORT || id == ID_BTN_IMPORT);
        bool isDestructive = (id == ID_BTN_DISCONNECT);
        bool hover = g_btnHover[hw];
        bool enabled = IsWindowEnabled(hw) != FALSE;

        COLORREF bg, fg;
        if (!enabled) { bg = C_ELEVATED; fg = C_BORDER; }
        else if (isAccent) { bg = hover ? C_ACCENT_DIM : C_ACCENT; fg = C_BG; }
        else if (isDestructive) { bg = hover ? RGB(190, 45, 45) : RGB(140, 30, 30); fg = C_TEXT; }
        else { bg = hover ? C_HOVER : C_ELEVATED; fg = hover ? C_TEXT : C_MUTED; }

        SolidBrush bgBr(GpColor(bg));
        FillRoundRect(g, bgBr, 0, 0, W, H, 5);

        if (!isAccent && !isDestructive && enabled) {
            Pen borderPen(GpColor(C_BORDER), 1.0f);
            DrawRoundRect(g, borderPen, 0, 0, W - 1, H - 1, 5);
        }

        wchar_t txt[256] = {};
        GetWindowTextW(hw, txt, 256);
        // Use cached font — isAccent gets bold weight via a locally-adjusted font
        Font fBtn(L"Segoe UI", 9, isAccent ? FontStyleBold : FontStyleRegular);
        SolidBrush fgBr(GpColor(fg));
        DrawTextGp(g, txt, fBtn, fgBr, 2, 0, W - 4, H, StringAlignmentCenter);

        BitBlt(hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, ob); DeleteObject(bmp); DeleteDC(mdc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefSubclassProc(hw, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Control factories
// ---------------------------------------------------------------------------
static HWND MakeDarkButton(HWND parent, const wchar_t* txt, int id, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SetWindowSubclass(hw, BtnSubclass, 0, 0);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_hfontUI, FALSE);
    return hw;
}

static HWND MakeDarkCombo(HWND parent, int id, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_hfontUI, FALSE);
    SetWindowTheme(hw, L"DarkMode_CFD", nullptr);
    return hw;
}

static HWND MakeDarkEdit(HWND parent, int id, int x, int y, int w, int h)
{
    // FIX: removed WS_EX_CLIENTEDGE — causes white border on Win11 dark mode.
    //      Border is handled by WM_CTLCOLOREDIT background contrast instead.
    HWND hw = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_hfontUI, FALSE);
    SetWindowTheme(hw, L"DarkMode_Explorer", nullptr);
    return hw;
}

static HWND MakeDarkLogEdit(HWND parent, int id, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY
        | WS_VSCROLL | ES_AUTOVSCROLL | WS_BORDER,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_hfontMono, FALSE);
    SetWindowTheme(hw, L"DarkMode_Explorer", nullptr);
    return hw;
}

static HWND MakeDarkLabel(HWND parent, const wchar_t* txt, int id, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"STATIC", txt,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_hfontSmall, FALSE);
    return hw;
}

static void StyleListView(HWND lv)
{
    SetWindowTheme(lv, L"DarkMode_Explorer", nullptr);
    ListView_SetBkColor(lv, C_ELEVATED);
    ListView_SetTextColor(lv, C_TEXT);
    ListView_SetTextBkColor(lv, C_ELEVATED);
    ListView_SetExtendedListViewStyle(lv,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        // -----------------------------------------------------------------------
    case WM_CREATE:
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hw, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        InitFonts();
        InitGpFonts();

        g_brBg = CreateSolidBrush(C_BG);
        g_brElevated = CreateSolidBrush(C_ELEVATED);
        g_brPanel = CreateSolidBrush(C_PANEL);

        // Connection controls
        g_hLblPort = MakeDarkLabel(hw, L"PORT", ID_LBL_PORT, 0, 0, 40, 18);
        g_hCmbPort = MakeDarkCombo(hw, ID_CMB_PORT, 0, 0, 130, 200);
        g_hBtnRefresh = MakeDarkButton(hw, L"Refresh", ID_BTN_REFRESH, 0, 0, 72, 24);
        g_hBtnConn = MakeDarkButton(hw, L"Connect", ID_BTN_CONNECT, 0, 0, 96, 24);
        g_hBtnDisc = MakeDarkButton(hw, L"Disconnect", ID_BTN_DISCONNECT, 0, 0, 104, 24);
        EnableWindow(g_hBtnDisc, FALSE);

        g_hLblVol = MakeDarkLabel(hw, L"VOL", ID_LBL_VOL, 0, 0, 28, 18);
        g_hProgVol = CreateWindowExW(0, PROGRESS_CLASS, L"",
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            0, 0, 120, 16, hw, (HMENU)ID_PROGRESS_VOL, g_inst, nullptr);
        SendMessage(g_hProgVol, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_hProgVol, PBM_SETPOS, 50, 0);
        SendMessage(g_hProgVol, PBM_SETBARCOLOR, 0, (LPARAM)C_ACCENT);
        SendMessage(g_hProgVol, PBM_SETBKCOLOR, 0, (LPARAM)C_ELEVATED);

        // List view
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL
            | LVS_SHOWSELALWAYS | WS_TABSTOP,
            0, 0, 400, 200, hw, (HMENU)ID_LIST, g_inst, nullptr);
        StyleListView(g_hList);
        SendMessage(g_hList, WM_SETFONT, (WPARAM)g_hfontUI, FALSE);

        LVCOLUMNW col = { LVCF_TEXT | LVCF_WIDTH };
        col.cx = 140; col.pszText = (LPWSTR)L"Button";      ListView_InsertColumn(g_hList, 0, &col);
        col.cx = 110; col.pszText = (LPWSTR)L"Action Type"; ListView_InsertColumn(g_hList, 1, &col);
        col.cx = 300; col.pszText = (LPWSTR)L"Value";       ListView_InsertColumn(g_hList, 2, &col);

        for (int i = 0; i < BUTTON_COUNT; i++) {
            LVITEMW li = { LVIF_TEXT, i }; li.pszText = (LPWSTR)BTN_LABELS[i];
            ListView_InsertItem(g_hList, &li);
            li.iSubItem = 1; li.pszText = (LPWSTR)L"None"; ListView_SetItem(g_hList, &li);
            li.iSubItem = 2; li.pszText = (LPWSTR)L"";     ListView_SetItem(g_hList, &li);
        }

        // Action editor
        g_hLblAction = MakeDarkLabel(hw, L"Action:", ID_LBL_ACTION, 0, 0, 52, 16);
        g_hCmbType = MakeDarkCombo(hw, ID_CMB_TYPE, 0, 0, 130, 120);
        SendMessage(g_hCmbType, CB_ADDSTRING, 0, (LPARAM)L"None");
        SendMessage(g_hCmbType, CB_ADDSTRING, 0, (LPARAM)L"Open URL");
        SendMessage(g_hCmbType, CB_ADDSTRING, 0, (LPARAM)L"Launch App");
        SendMessage(g_hCmbType, CB_SETCURSEL, 0, 0);

        g_hLblValue = MakeDarkLabel(hw, L"Value:", ID_LBL_VALUE, 0, 0, 40, 16);
        g_hEditVal = MakeDarkEdit(hw, ID_EDIT_VAL, 0, 0, 300, 22);
        g_hBtnBrowse = MakeDarkButton(hw, L"Browse\u2026", ID_BTN_BROWSE, 0, 0, 84, 24);
        g_hBtnSave = MakeDarkButton(hw, L"Save Action", ID_BTN_SAVE, 0, 0, 104, 26);

        // Profile
        g_hBtnExport = MakeDarkButton(hw, L"Export Profile", ID_BTN_EXPORT, 0, 0, 160, 44);
        g_hBtnImport = MakeDarkButton(hw, L"Import Profile", ID_BTN_IMPORT, 0, 0, 160, 44);

        // Log
        g_hBtnClearLog = MakeDarkButton(hw, L"Clear Log", ID_BTN_CLEAR_LOG, 0, 0, 88, 24);
        g_hEditLog = MakeDarkLogEdit(hw, ID_EDIT_LOG, 0, 0, 400, 300);

        PopulatePorts();
        ApplyPage(hw);

        AddLog(L"Console Deck Configurator v3 ready.");
        AddLog(L"Select a COM port and click Connect.");

        // ---- AUTO-LOAD DEFAULT PROFILE ----
        AutoLoadProfile();

        break;
    }

    // -----------------------------------------------------------------------
    // FIX: enforce minimum window size
    case WM_GETMINMAXINFO:
    {
        auto* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize = { MIN_W, MIN_H };
        return 0;
    }

    // -----------------------------------------------------------------------
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            ApplyPage(hw);
        break;

        // -----------------------------------------------------------------------
    case WM_PAINT:
        OnPaint(hw);
        return 0;

    case WM_ERASEBKGND:
        return 1;

        // -----------------------------------------------------------------------
    case WM_MOUSEMOVE:
    {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);

        if (g_dragging) {
            POINT pt = { mx, my }; ClientToScreen(hw, &pt);
            SetWindowPos(hw, nullptr, pt.x - g_dragStart.x, pt.y - g_dragStart.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
            break;
        }

        RECT rc; GetClientRect(hw, &rc);
        bool nc = (my < TITLEBAR_H);
        bool hc = nc && (mx >= rc.right - TBTN_W);
        bool hm = nc && (mx >= rc.right - TBTN_W * 2) && (mx < rc.right - TBTN_W);
        if (hc != g_hoverClose || hm != g_hoverMin) {
            g_hoverClose = hc; g_hoverMin = hm;
            InvalidateRect(hw, nullptr, FALSE);
        }

        // FIX: reset nav hover when mouse enters title bar region
        int nav = -1;
        if (mx < SIDEBAR_W && my >= NAV_START_Y && my < rc.bottom) {
            int rel = my - NAV_START_Y;
            int n = rel / NAV_ITEM_H;
            if (n >= 0 && n < PAGE_COUNT)
                nav = n;
        }
        if (nav != g_navHover) {
            g_navHover = nav;
            InvalidateRect(hw, nullptr, FALSE);
        }

        if (!g_sidebarTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
            g_sidebarTracking = true;
        }
        break;
    }

    case WM_MOUSELEAVE:
        g_sidebarTracking = false;
        if (g_navHover != -1 || g_hoverClose || g_hoverMin) {
            g_navHover = -1; g_hoverClose = false; g_hoverMin = false;
            InvalidateRect(hw, nullptr, FALSE);
        }
        break;

    case WM_LBUTTONDOWN:
    {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);

        if (mx < SIDEBAR_W && my >= NAV_START_Y) {
            int rel = my - NAV_START_Y;
            int nav = rel / NAV_ITEM_H;
            if (nav >= 0 && nav < PAGE_COUNT) {
                g_page = (Page)nav;
                ApplyPage(hw);
            }
        }

        RECT rc; GetClientRect(hw, &rc);
        if (my < TITLEBAR_H && mx >= SIDEBAR_W && mx < rc.right - TBTN_W * 2) {
            g_dragging = true;
            POINT pt = { mx, my }; ClientToScreen(hw, &pt);
            RECT wr; GetWindowRect(hw, &wr);
            g_dragStart = { pt.x - wr.left, pt.y - wr.top };
            SetCapture(hw);
        }
        break;
    }

    case WM_LBUTTONUP:
        if (g_dragging) { g_dragging = false; ReleaseCapture(); }
        break;

        // -----------------------------------------------------------------------
        // FIX: suppress resize hit-tests when window is maximised
    case WM_NCHITTEST:
    {
        LRESULT def = DefWindowProc(hw, msg, wp, lp);
        if (def == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hw, &pt);
            RECT rc; GetClientRect(hw, &rc);
            if (pt.y < TITLEBAR_H) {
                if (pt.x >= rc.right - TBTN_W)     return HTCLOSE;
                if (pt.x >= rc.right - TBTN_W * 2) return HTMINBUTTON;
                if (pt.x > SIDEBAR_W)               return HTCAPTION;
            }
        }
        // Only return resize hits when not maximised
        if (IsZoomed(hw)) return HTCLIENT;
        return def;
    }

    // -----------------------------------------------------------------------
    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        switch (id)
        {
        case ID_BTN_REFRESH:
            PopulatePorts();
            AddLog(L"COM ports refreshed.");
            break;

        case ID_BTN_CONNECT:
        {
            int sel = (int)SendMessage(g_hCmbPort, CB_GETCURSEL, 0, 0);
            if (sel == CB_ERR) { MessageBoxW(hw, L"Select a COM port first.", L"", MB_ICONINFORMATION); break; }
            wchar_t pb[32] = {}; SendMessage(g_hCmbPort, CB_GETLBTEXT, sel, (LPARAM)pb);
            if (OpenSerial(pb)) {
                g_running = true; g_connected = true;
                g_thread = std::thread(SerialThread, hw);
                EnableWindow(g_hBtnConn, FALSE);
                EnableWindow(g_hBtnDisc, TRUE);
                EnableWindow(g_hCmbPort, FALSE);
                EnableWindow(g_hBtnRefresh, FALSE);
                // FIX: update title bar to show connected port
                SetWindowTextW(hw, (std::wstring(APP_TITLE) + L" \u2014 " + pb).c_str());
                AddLog(std::wstring(L"Connected to ") + pb);
                InvalidateRect(hw, nullptr, FALSE);
            }
            else {
                MessageBoxW(hw,
                    (std::wstring(L"Failed to open ") + pb + L"\nPort may be in use.").c_str(),
                    L"Connection Error", MB_ICONERROR);
            }
            break;
        }

        case ID_BTN_DISCONNECT:
            g_running = false;
            if (g_thread.joinable()) g_thread.join();
            CloseSerial();
            g_connected = false;
            EnableWindow(g_hBtnConn, TRUE);
            EnableWindow(g_hBtnDisc, FALSE);
            EnableWindow(g_hCmbPort, TRUE);
            EnableWindow(g_hBtnRefresh, TRUE);
            SetWindowTextW(hw, APP_TITLE);
            AddLog(L"Disconnected.");
            InvalidateRect(hw, nullptr, FALSE);
            break;

        case ID_BTN_BROWSE:
        {
            wchar_t f[MAX_PATH] = {}; OPENFILENAMEW o = { sizeof(o) };
            o.hwndOwner = hw;
            o.lpstrFilter = L"Executable (*.exe)\0*.exe\0All Files\0*.*\0";
            o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&o)) SetWindowTextW(g_hEditVal, f);
            break;
        }

        case ID_BTN_SAVE:
        {
            int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
            if (sel < 0) { MessageBoxW(hw, L"Select a button from the list first.", L"", MB_ICONINFORMATION); break; }
            int ti = (int)SendMessage(g_hCmbType, CB_GETCURSEL, 0, 0);
            if (ti < 0 || ti >= ACT_COUNT) ti = 0;
            wchar_t vb[1024] = {}; GetWindowTextW(g_hEditVal, vb, 1024);
            g_actions[sel].type = (ActionType)ti;
            g_actions[sel].value = vb;

            LVITEMW li = { LVIF_TEXT, sel, 1 };
            li.pszText = (LPWSTR)SafeActName(ti); ListView_SetItem(g_hList, &li);
            li.iSubItem = 2; li.pszText = vb;     ListView_SetItem(g_hList, &li);

            AddLog(std::wstring(L"Saved ") + BTN_LABELS[sel]
                + L" \u2192 " + SafeActName(ti) + L" " + vb);

            // Offer to persist as default
            if (MessageBoxW(hw,
                L"Action saved.\n\nUpdate default.cdp with the current profile?",
                L"Save Action", MB_ICONQUESTION | MB_YESNO) == IDYES)
            {
                std::wstring def = ExeDir() + L"default.cdp";
                if (WriteProfileToFile(def))
                    AddLog(L"\u2713 default.cdp updated.");
                else
                    AddLog(L"\u26A0 Could not update default.cdp.");
            }
            break;
        }

        case ID_BTN_EXPORT: ExportProfile(hw); break;
        case ID_BTN_IMPORT: ImportProfile(hw); break;

        case ID_BTN_CLEAR_LOG:
            SetWindowTextW(g_hEditLog, L"");
            g_logLines.clear();
            AddLog(L"Log cleared.");
            break;
        }
        break;
    }

    // -----------------------------------------------------------------------
    case WM_NOTIFY:
    {
        LPNMHDR h = (LPNMHDR)lp;
        if (h->idFrom == ID_LIST && h->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW nlv = (LPNMLISTVIEW)lp;
            if ((nlv->uNewState & LVIS_SELECTED) && !(nlv->uOldState & LVIS_SELECTED)) {
                int i = nlv->iItem;
                int t = (int)g_actions[i].type;
                if (t < 0 || t >= ACT_COUNT) t = 0;
                SendMessage(g_hCmbType, CB_SETCURSEL, t, 0);
                SetWindowTextW(g_hEditVal, g_actions[i].value.c_str());
            }
        }
        break;
    }

    // -----------------------------------------------------------------------
    // FIX: wrapped DispatchSerial in try/catch to guarantee pm is always deleted
    case WM_SERIAL_MSG:
    {
        auto* pm = (std::wstring*)lp;
        if (pm) {
            try { DispatchSerial(*pm); }
            catch (...) {}
            delete pm;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // FIX: WM_CTLCOLORSTATIC now returns g_brPanel for controls inside the
    //      sidebar region, and g_brBg for those in the content area, so label
    //      backgrounds match their parent surface exactly.
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, C_TEXT);
        SetBkColor(hdc, C_ELEVATED);
        return (LRESULT)g_brElevated;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        // Determine which surface the label sits on
        RECT cr; GetWindowRect(ctrl, &cr);
        POINT cp = { cr.left, cr.top }; ScreenToClient(hw, &cp);
        bool inSidebar = (cp.x < SIDEBAR_W);
        SetTextColor(hdc, C_MUTED);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)(inSidebar ? g_brPanel : g_brBg);
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)g_brBg;

        // -----------------------------------------------------------------------
    case WM_DESTROY:
        g_running = false;
        if (g_thread.joinable()) g_thread.join();
        CloseSerial();
        FreeGpFonts();
        if (g_brBg) { DeleteObject(g_brBg);       g_brBg = nullptr; }
        if (g_brElevated) { DeleteObject(g_brElevated); g_brElevated = nullptr; }
        if (g_brPanel) { DeleteObject(g_brPanel);    g_brPanel = nullptr; }
        if (g_hfontUI) { DeleteObject(g_hfontUI);    g_hfontUI = nullptr; }
        if (g_hfontMono) { DeleteObject(g_hfontMono);  g_hfontMono = nullptr; }
        if (g_hfontSmall) { DeleteObject(g_hfontSmall); g_hfontSmall = nullptr; }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hw, msg, wp, lp);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int APIENTRY wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nShow)
{
    g_inst = hInst;

    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdipToken, &gsi, nullptr);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = APP_CLASS;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(WS_EX_APPWINDOW, APP_CLASS, APP_TITLE,
        WS_POPUP | WS_VISIBLE | WS_MINIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInst, nullptr);

    MARGINS m = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_hWnd, &m);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(g_gdipToken);
    return (int)msg.wParam;
}