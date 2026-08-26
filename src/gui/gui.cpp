// STGR Microphone Equalizer - native configuration GUI (Win32, no framework).
//
// The GUI is a configuration frontend only. It reads/writes the JSON
// configuration for the selected microphone endpoint; the actual audio
// processing runs in the STGR APO inside the Windows audio engine.
#include <windows.h>
#include <winternl.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <cstdlib>
#include <cmath>

#include "../common/version.h"
#include "../common/paths.h"
#include "../common/util.h"
#include "../config/manager.h"
#include "../config/schema.h"
#include "../devices/device_manager.h"
#include "../plugins/plugin_loader.h"
#include "../bridge/shm.h"
#include "../dsp/biquad.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace stgr::gui {

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
constexpr COLORREF kBg        = RGB(18, 18, 20);
constexpr COLORREF kPanel     = RGB(26, 26, 30);
constexpr COLORREF kPanel2    = RGB(34, 34, 40);
constexpr COLORREF kText      = RGB(226, 226, 228);
constexpr COLORREF kTextDim   = RGB(140, 140, 148);
constexpr COLORREF kRed       = RGB(220, 40, 46);
constexpr COLORREF kRedDim    = RGB(140, 30, 34);
constexpr COLORREF kGreen     = RGB(70, 200, 110);
constexpr COLORREF kAmber     = RGB(230, 180, 60);

constexpr UINT kCtlFirst = 1000;
enum {
    CtlDeviceList = kCtlFirst,
    CtlBtnEnable, CtlBtnBypass, CtlBtnApply, CtlBtnReset,
    CtlBtnRefresh, CtlBtnAttach,
    CtlBtnEqAdd, CtlBtnEqDel, CtlBtnEqType, CtlBtnEqReset,
    CtlChainList, CtlBtnChainUp, CtlBtnChainDown, CtlBtnChainRemove,
    CtlBtnChainAdd, CtlBtnChainPlugin, CtlBtnChainEnable, CtlBtnChainParams,
    CtlPresetCombo, CtlBtnPresetLoad, CtlBtnPresetSave, CtlBtnPresetDelete,
    CtlBtnPresetDuplicate,
    CtlDiagText, CtlBtnDiagExport, CtlBtnDiagRestartAudio,
    CtlBtnAboutHome,
    CtlTabMic, CtlTabEq, CtlTabChain, CtlTabPresets, CtlTabDiag, CtlTabAbout,
    CtlStatusText, CtlMeterIn, CtlMeterOut, CtlLatencyText,
    CtlBtnChainAddBuiltin,
    CtlBtnHome,
    CtlEqFreq, CtlEqGain, CtlEqQ,
};

constexpr int kTabCount = 6;
const wchar_t* kTabNames[kTabCount] = {
    L"Microphone", L"Equalizer", L"Chain", L"Presets", L"Diagnostics", L"About",
};

// ---------------------------------------------------------------------------
// EQ response evaluation (cascade of biquads, exact magnitude)
// ---------------------------------------------------------------------------
struct Complex { double re, im; };
inline Complex cadd(Complex a, Complex b) { return {a.re + b.re, a.im + b.im}; }
inline Complex cmul(Complex a, Complex b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
inline double cmag(Complex c) { return std::sqrt(c.re * c.re + c.im * c.im); }

// |H(e^{-jw})| in dB for a TDF2 biquad.
inline double biquad_mag_db(const dsp::BiquadCoeffs& c, double w)
{
    const Complex z1{std::cos(-w), std::sin(-w)};
    const Complex z2 = cmul(z1, z1);
    const Complex num = cadd(cadd({c.b0, 0}, cmul({c.b1, 0}, z1)), cmul({c.b2, 0}, z2));
    const Complex den = cadd(cadd({1.0, 0}, cmul({c.a1, 0}, z1)), cmul({c.a2, 0}, z2));
    const double m = cmag(num) / cmag(den);
    return 20.0 * std::log10(m <= 0 ? 1e-12 : m);
}

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------
struct App {
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    HWND hwndTabs[kTabCount] = {};
    HWND hwndTabPanels[kTabCount] = {};
    int tab = 0;

    std::vector<devices::DeviceInfo> devices;
    int selDevice = -1;
    bool attached = false;

    config::ConfigManager mgr;
    config::DeviceConfig devCfg;
    std::vector<std::wstring> presetNames;

    int selBand = -1;
    int selChain = -1;

    HWND hwndEqView = nullptr;
    HWND hwndMeterIn = nullptr;
    HWND hwndMeterOut = nullptr;

    // Meter state from the bridge header (read-only).
    bridge::SharedSection meterSection;
    bridge::ShmHeader* meterHeader = nullptr;
    std::wstring meterEndpoint;

    // Plugin cache (read by the GUI only).
    std::vector<plugins::PluginEntry> pluginCache;
    HWND hwndPluginDialog = nullptr;
    HWND hwndPluginList = nullptr;
    bool scanning = false;
    ULONGLONG scanStartedAt = 0;
    PROCESS_INFORMATION scanProc{};

    HBRUSH hbrBg = nullptr;
    HBRUSH hbrPanel = nullptr;
    HBRUSH hbrPanel2 = nullptr;
    HICON hIcon = nullptr;
    HFONT hFont = nullptr;
    HFONT hFontBig = nullptr;
    HFONT hFontMono = nullptr;
};

static App g_app;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EqViewProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MeterProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PluginDialogProc(HWND, UINT, WPARAM, LPARAM);
dsp::EqBand& band_from(int idx);
bool InputBox(HWND parent, const wchar_t* prompt, const wchar_t* title,
              wchar_t* out, int outLen);

void refresh_devices(App&);
void on_device_selected(App&);
void refresh_chain_ui(App&);
void refresh_eq_ui(App&);
void refresh_presets(App&);
void refresh_status(App&);
void apply_config(App&, bool restartAudio);
void update_meters(App&);
void spawn_admin(App&, const std::wstring& args);
void signal_config_changed();
void open_plugin_dialog(App&);
void populate_plugin_list(App&);

// ---------------------------------------------------------------------------
// Controls creation helpers
// ---------------------------------------------------------------------------
HWND mk_button(HWND parent, UINT id, const wchar_t* text, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_app.hInst, nullptr);
}
HWND mk_check(HWND parent, UINT id, const wchar_t* text, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                           x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_app.hInst, nullptr);
}
HWND mk_label(HWND parent, UINT id, const wchar_t* text, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_app.hInst, nullptr);
}
HWND mk_list(HWND parent, UINT id, int x, int y, int w, int h, DWORD style = 0)
{
    return CreateWindowExW(0, L"LISTBOX", nullptr,
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
                           LBS_NOTIFY | style,
                           x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_app.hInst, nullptr);
}
HWND mk_edit(HWND parent, UINT id, int x, int y, int w, int h, bool readOnly = false)
{
    return CreateWindowExW(0, L"EDIT", nullptr,
                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL |
                           (readOnly ? ES_READONLY : 0),
                           x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_app.hInst, nullptr);
}
HWND mk_combo(HWND parent, UINT id, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"COMBOBOX", nullptr,
                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_app.hInst, nullptr);
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
static void build_ui(HWND hwnd)
{
    App& a = g_app;
    a.hwnd = hwnd;
    a.hbrBg = CreateSolidBrush(kBg);
    a.hbrPanel = CreateSolidBrush(kPanel);
    a.hbrPanel2 = CreateSolidBrush(kPanel2);
    a.hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    a.hFontBig = CreateFontW(-20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                             0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    a.hFontMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    a.hIcon = (HICON)LoadImageW(a.hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);

    // Tabs (drawn as flat buttons).
    for (int i = 0; i < kTabCount; ++i) {
        a.hwndTabs[i] = CreateWindowExW(0, L"BUTTON", kTabNames[i],
                                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        10 + i * 110, 52, 105, 28,
                                        hwnd, (HMENU)(UINT_PTR)(CtlTabMic + i),
                                        a.hInst, nullptr);
    }

    // Tab panels.
    for (int i = 0; i < kTabCount; ++i) {
        a.hwndTabPanels[i] = CreateWindowExW(0, L"STATIC", L"",
                                             WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                             0, 90, 900, 480, hwnd,
                                             (HMENU)(UINT_PTR)(300 + i), a.hInst, nullptr);
    }

    // --- Microphone tab ---
    HWND p = a.hwndTabPanels[0];
    mk_label(p, 0, L"MICROPHONES", 20, 16, 200, 20);
    mk_list(p, CtlDeviceList, 20, 40, 380, 200, LBS_OWNERDRAWFIXED | LBS_HASSTRINGS);
    mk_button(p, CtlBtnRefresh, L"Refresh", 410, 40, 90, 26);
    mk_button(p, CtlBtnAttach, L"Attach STGR", 410, 74, 90, 26);
    mk_label(p, 0, L"STGR status:", 410, 120, 120, 18);
    mk_label(p, CtlStatusText, L"-", 410, 140, 220, 18);
    mk_button(p, CtlBtnEnable, L"Enable", 20, 260, 100, 30);
    mk_button(p, CtlBtnBypass, L"Bypass", 130, 260, 100, 30);
    mk_button(p, CtlBtnApply, L"APPLY", 240, 260, 110, 30);
    mk_button(p, CtlBtnReset, L"Reset", 360, 260, 80, 30);
    mk_label(p, 0, L"INPUT", 20, 310, 60, 16);
    a.hwndMeterIn = CreateWindowExW(0, L"StgrMeter", L"", WS_CHILD | WS_VISIBLE,
                                    90, 308, 300, 18, p, (HMENU)1, a.hInst, nullptr);
    mk_label(p, 0, L"OUTPUT", 20, 338, 60, 16);
    a.hwndMeterOut = CreateWindowExW(0, L"StgrMeter", L"", WS_CHILD | WS_VISIBLE,
                                     90, 336, 300, 18, p, (HMENU)2, a.hInst, nullptr);
    mk_label(p, CtlLatencyText, L"Latency: -", 20, 366, 460, 40);

    // --- Equalizer tab ---
    p = a.hwndTabPanels[1];
    a.hwndEqView = CreateWindowExW(0, L"StgrEqView", L"", WS_CHILD | WS_VISIBLE,
                                   20, 20, 640, 240, p, (HMENU)10, a.hInst, nullptr);
    mk_button(p, CtlBtnEqAdd, L"Add Band", 680, 20, 100, 26);
    mk_button(p, CtlBtnEqDel, L"Remove Band", 680, 52, 100, 26);
    mk_button(p, CtlBtnEqReset, L"Reset EQ", 680, 84, 100, 26);
    mk_label(p, 0, L"Type:", 20, 280, 50, 18);
    mk_combo(p, CtlBtnEqType, 70, 276, 150, 200);
    mk_label(p, 0, L"Freq (Hz):", 240, 280, 70, 18);
    mk_edit(p, CtlEqFreq, 320, 276, 90, 22);
    mk_label(p, 0, L"Gain (dB):", 430, 280, 70, 18);
    mk_edit(p, CtlEqGain, 510, 276, 90, 22);
    mk_label(p, 0, L"Q:", 620, 280, 30, 18);
    mk_edit(p, CtlEqQ, 650, 276, 80, 22);
    mk_label(p, 0, L"Drag band handles on the graph. Double-click a band to toggle.",
             20, 320, 640, 18);

    // --- Chain tab ---
    p = a.hwndTabPanels[2];
    mk_list(p, CtlChainList, 20, 20, 460, 300, LBS_OWNERDRAWFIXED | LBS_HASSTRINGS);
    mk_button(p, CtlBtnChainUp, L"Up", 500, 20, 90, 26);
    mk_button(p, CtlBtnChainDown, L"Down", 500, 52, 90, 26);
    mk_button(p, CtlBtnChainEnable, L"Enable", 500, 84, 90, 26);
    mk_button(p, CtlBtnChainRemove, L"Remove", 500, 116, 90, 26);
    mk_button(p, CtlBtnChainAdd, L"+ Add Effect", 500, 160, 90, 26);
    mk_button(p, CtlBtnChainPlugin, L"+ Add Plugin...", 500, 192, 110, 26);
    mk_button(p, CtlBtnChainParams, L"Parameters...", 500, 224, 110, 26);
    mk_label(p, 0, L"Processing order: top = first.", 20, 330, 400, 18);

    // --- Presets tab ---
    p = a.hwndTabPanels[3];
    mk_label(p, 0, L"Preset:", 20, 40, 80, 20);
    mk_combo(p, CtlPresetCombo, 100, 36, 260, 200);
    mk_button(p, CtlBtnPresetLoad, L"Load", 380, 36, 80, 26);
    mk_button(p, CtlBtnPresetSave, L"Save...", 470, 36, 80, 26);
    mk_button(p, CtlBtnPresetDelete, L"Delete", 560, 36, 80, 26);
    mk_button(p, CtlBtnPresetDuplicate, L"Duplicate...", 650, 36, 100, 26);
    mk_label(p, 0, L"Built-in presets are shown automatically. Save stores the current",
             20, 90, 640, 18);
    mk_label(p, 0, L"chain for this microphone. Presets are portable JSON files.",
             20, 112, 640, 18);

    // --- Diagnostics tab ---
    p = a.hwndTabPanels[4];
    mk_edit(p, CtlDiagText, 20, 20, 760, 360, true);
    mk_button(p, CtlBtnDiagExport, L"Export Diagnostics", 20, 392, 170, 28);
    mk_button(p, CtlBtnDiagRestartAudio, L"Restart Audio Service", 210, 392, 190, 28);

    // --- About tab ---
    p = a.hwndTabPanels[5];
    mk_label(p, 0, L"STGR MICROPHONE EQUALIZER", 30, 40, 500, 30);
    {
        wchar_t ver[128];
        swprintf(ver, 128, L"Version %S - native Windows 10/11 x64", STGR_VERSION_STRING);
        mk_label(p, 0, ver, 30, 76, 500, 20);
    }
    mk_label(p, 0, L"System-level microphone processing with EQ, dynamics and VST/VST3.", 30, 110, 600, 18);
    mk_label(p, 0, L"GPL-3.0 licensed. No telemetry, no cloud, no audio leaves your PC.", 30, 136, 600, 18);
    mk_label(p, 0, L"STGR", 30, 180, 200, 40);

    // Status bar (bottom).
    mk_label(hwnd, 0, L"", 10, 580, 800, 20);

    // Chain add menu populates dynamically.
    refresh_devices(a);
    refresh_presets(a);
    on_device_selected(a);

    // Only the first tab is visible initially.
    for (int i = 1; i < kTabCount; ++i)
        ShowWindow(a.hwndTabPanels[i], SW_HIDE);
}

// ---------------------------------------------------------------------------
// Panel painting
// ---------------------------------------------------------------------------
static void paint_panel(HWND hwnd, HDC hdc, RECT* rc)
{
    FillRect(hdc, rc, g_app.hbrPanel);
    RECT line{rc->left, rc->top, rc->right, rc->top + 1};
    FillRect(hdc, &line, g_app.hbrPanel2);
}

// ---------------------------------------------------------------------------
// Tab drawing
// ---------------------------------------------------------------------------
static void paint_tab(HWND hwnd, DRAWITEMSTRUCT* dis)
{
    const int idx = (int)dis->CtlID - (int)CtlTabMic;
    const bool selected = (idx == g_app.tab);
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(hdc, &rc, selected ? g_app.hbrPanel2 : g_app.hbrBg);
    if (selected) {
        RECT top{rc.left, rc.top, rc.right, rc.top + 2};
        FillRect(hdc, &top, g_app.hbrPanel2);
        FillRect(hdc, &top, g_app.hbrPanel2);
        HBRUSH red = CreateSolidBrush(kRed);
        RECT marker{rc.left, rc.bottom - 3, rc.right, rc.bottom};
        FillRect(hdc, &marker, red);
        DeleteObject(red);
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selected ? kText : kTextDim);
    HFONT old = (HFONT)SelectObject(hdc, g_app.hFont);
    DrawTextW(hdc, kTabNames[idx], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old);
}

// ---------------------------------------------------------------------------
// Device list drawing
// ---------------------------------------------------------------------------
static void paint_device_list(HWND hwnd, DRAWITEMSTRUCT* dis)
{
    if (dis->itemID >= g_app.devices.size()) return;
    const auto& dev = g_app.devices[dis->itemID];
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(hdc, &rc, (dis->itemState & ODS_SELECTED) ? g_app.hbrPanel2 : g_app.hbrPanel);

    SetBkMode(hdc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(hdc, g_app.hFont);

    // Default marker.
    wchar_t marker[4] = L"  ";
    if (dev.isDefault) {
        marker[0] = L'\x25CF'; // bullet
        SetTextColor(hdc, kGreen);
        TextOutW(hdc, rc.left + 8, rc.top + 4, marker, 1);
    }

    // Name + STGR state.
    const bool stgr = devices::DeviceManager::is_stgr_attached(dev.id);
    RECT nameRc = rc;
    nameRc.left += 30;
    SetTextColor(hdc, (dis->itemState & ODS_SELECTED) ? kText : kText);
    std::wstring line = dev.name;
    if (dev.isDefault) line += L"  [default]";
    DrawTextW(hdc, line.c_str(), -1, &nameRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT stRc = rc;
    stRc.right -= 12;
    stRc.left = stRc.right - 110;
    const wchar_t* state = stgr ? L"STGR: attached" : L"STGR: not attached";
    SetTextColor(hdc, stgr ? kGreen : kTextDim);
    DrawTextW(hdc, state, -1, &stRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old);
}

static void paint_chain_list(HWND hwnd, DRAWITEMSTRUCT* dis)
{
    if (dis->itemID >= g_app.devCfg.chain.size()) return;
    const auto& st = g_app.devCfg.chain[dis->itemID];
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    FillRect(hdc, &rc, (dis->itemState & ODS_SELECTED) ? g_app.hbrPanel2 : g_app.hbrPanel);

    SetBkMode(hdc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(hdc, g_app.hFont);

    wchar_t num[8];
    swprintf(num, 8, L"%d.", (int)dis->itemID + 1);
    RECT numRc = rc;
    numRc.left += 10;
    numRc.right = numRc.left + 30;
    SetTextColor(hdc, kRed);
    DrawTextW(hdc, num, -1, &numRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT nameRc = rc;
    nameRc.left += 44;
    nameRc.right -= 120;
    SetTextColor(hdc, st.enabled ? kText : kTextDim);
    std::wstring name = to_wide(dsp::stage_type_name(st.type));
    if (st.type == dsp::StageType::Plugin) name = to_wide(st.pluginName.empty() ? st.pluginInstanceId : st.pluginName);
    if (!st.enabled) name += L" (disabled)";
    DrawTextW(hdc, name.c_str(), -1, &nameRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT stRc = rc;
    stRc.right -= 10;
    stRc.left = stRc.right - 110;
    SetTextColor(hdc, kTextDim);
    std::wstring detail;
    switch (st.type) {
        case dsp::StageType::HighPass: case dsp::StageType::LowPass:
        case dsp::StageType::Peaking: case dsp::StageType::Notch:
        case dsp::StageType::LowShelf: case dsp::StageType::HighShelf:
            detail = fmt_double_w(st.freq, 0) + L" Hz";
            break;
        case dsp::StageType::Gain:
            detail = fmt_double_w(st.gainDb, 1) + L" dB";
            break;
        case dsp::StageType::Compressor:
            detail = L"thr " + fmt_double_w(st.thresholdDb, 1) + L" dB";
            break;
        case dsp::StageType::Limiter:
            detail = L"ceil " + fmt_double_w(st.ceilingDb, 1) + L" dB";
            break;
        default: break;
    }
    DrawTextW(hdc, detail.c_str(), -1, &stRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old);
}

// ---------------------------------------------------------------------------
// EQ view (custom control)
// ---------------------------------------------------------------------------
struct EqViewState {
    int mouseBand = -1;
    bool dragging = false;
};

static dsp::BiquadCoeffs band_coeff(const dsp::EqBand& b, double sr)
{
    return dsp::design_biquad(b.type, b.freq, b.gainDb, b.q, sr);
}

static void eq_view_paint(HWND hwnd, HDC hdc, RECT* rc)
{
    // Background + grid.
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    const int W = rc->right - rc->left, H = rc->bottom - rc->top;
    const double fMin = 20.0, fMax = 20000.0;
    const double gMin = -12.0, gMax = 12.0;

    auto xOf = [&](double f) -> double {
        return rc->left + (std::log10(f / fMin) / std::log10(fMax / fMin)) * (W - 60) + 40;
    };
    auto yOf = [&](double g) -> double {
        return rc->top + (1.0 - (g - gMin) / (gMax - gMin)) * (H - 30) + 10;
    };

    // Grid lines (decades).
    HPEN gridPen = CreatePen(PS_SOLID, 1, kPanel2);
    HPEN centerPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 68));
    HPEN oldPen = (HPEN)SelectObject(hdc, gridPen);
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_app.hFont);
    SetTextColor(hdc, kTextDim);

    double f = fMin;
    while (f <= fMax) {
        const int x = (int)xOf(f);
        MoveToEx(hdc, x, rc->top + 10, nullptr);
        LineTo(hdc, x, rc->bottom - 20);
        wchar_t buf[32];
        if (f >= 1000) swprintf(buf, 32, L"%dk", (int)(f / 1000));
        else swprintf(buf, 32, L"%d", (int)f);
        TextOutW(hdc, x - 10, rc->bottom - 16, buf, (int)wcslen(buf));
        f *= 10.0;
    }
    for (int g = -12; g <= 12; g += 3) {
        const int y = (int)yOf((double)g);
        SelectObject(hdc, (g == 0) ? centerPen : gridPen);
        MoveToEx(hdc, rc->left + 40, y, nullptr);
        LineTo(hdc, rc->right - 20, y);
        wchar_t buf[16];
        swprintf(buf, 16, L"%+d", g);
        TextOutW(hdc, rc->left + 6, y - 8, buf, (int)wcslen(buf));
    }
    SelectObject(hdc, gridPen);

    // Curve: cascade magnitude of all enabled bands.
    const double sr = 48000.0;
    HPEN curvePen = CreatePen(PS_SOLID, 2, kRed);
    SelectObject(hdc, curvePen);
    MoveToEx(hdc, (int)xOf(fMin), (int)yOf(0), nullptr);
    for (int i = 0; i <= 600; ++i) {
        const double lf = std::log10(fMin) + (std::log10(fMax) - std::log10(fMin)) * i / 600.0;
        const double freq = std::pow(10.0, lf);
        double sumDb = 0.0;
        const double w = 2.0 * 3.14159265358979 * freq / sr;
        // Sum over enabled stages in the current chain.
        for (const auto& st : g_app.devCfg.chain) {
            if (st.type == dsp::StageType::Eq10) {
                for (int b = 0; b < dsp::kMaxEqBands; ++b) {
                    if (!st.bands[b].enabled) continue;
                    sumDb += biquad_mag_db(band_coeff(st.bands[b], sr), w);
                }
            } else if (st.type == dsp::StageType::HighPass ||
                       st.type == dsp::StageType::LowPass ||
                       st.type == dsp::StageType::Peaking ||
                       st.type == dsp::StageType::Notch ||
                       st.type == dsp::StageType::LowShelf ||
                       st.type == dsp::StageType::HighShelf) {
                dsp::BiquadCoeffs c = dsp::design_biquad(st.filterType, st.freq, st.biquadGainDb, st.q, sr);
                sumDb += biquad_mag_db(c, w);
            }
        }
        const int x = (int)xOf(freq);
        const int y = (int)yOf(sumDb);
        if (i == 0) MoveToEx(hdc, x, y, nullptr);
        else LineTo(hdc, x, y);
    }
    SelectObject(hdc, oldPen);

    // Band handles (only for the EQ10 stage).
    for (const auto& st : g_app.devCfg.chain) {
        if (st.type != dsp::StageType::Eq10) continue;
        for (int b = 0; b < dsp::kMaxEqBands; ++b) {
            const auto& band = st.bands[b];
            if (!band.enabled) continue;
            const int x = (int)xOf(band.freq);
            const int y = (int)yOf(band.gainDb);
            HPEN hpen = CreatePen(PS_SOLID, 1, (b == g_app.selBand) ? kText : kRed);
            HBRUSH hbr = CreateSolidBrush((b == g_app.selBand) ? kRed : kBg);
            SelectObject(hdc, hpen);
            SelectObject(hdc, hbr);
            Ellipse(hdc, x - 5, y - 5, x + 5, y + 5);
            DeleteObject(hpen);
            DeleteObject(hbr);
            wchar_t buf[16];
            swprintf(buf, 16, L"%d", b + 1);
            SetTextColor(hdc, (b == g_app.selBand) ? kText : kRed);
            TextOutW(hdc, x - 6, y - 20, buf, (int)wcslen(buf));
        }
    }

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);
    DeleteObject(centerPen);
    DeleteObject(curvePen);
}

static int eq_view_hit_band(HWND hwnd, POINT pt, RECT* rc)
{
    const int W = rc->right - rc->left, H = rc->bottom - rc->top;
    const double fMin = 20.0, fMax = 20000.0;
    auto xOf = [&](double f) { return rc->left + (std::log10(f / fMin) / std::log10(fMax / fMin)) * (W - 60) + 40; };
    auto yOf = [&](double g) { return rc->top + (1.0 - (g + 12.0) / 24.0) * (H - 30) + 10; };

    for (const auto& st : g_app.devCfg.chain) {
        if (st.type != dsp::StageType::Eq10) continue;
        for (int b = 0; b < dsp::kMaxEqBands; ++b) {
            const auto& band = st.bands[b];
            if (!band.enabled) continue;
            const int x = (int)xOf(band.freq);
            const int y = (int)yOf(band.gainDb);
            if (abs(pt.x - x) < 10 && abs(pt.y - y) < 10) return b;
        }
    }
    return -1;
}

static void eq_view_pixel_to_band(HWND hwnd, POINT pt, RECT* rc, double& freq, double& gainDb)
{
    const int W = rc->right - rc->left, H = rc->bottom - rc->top;
    const double fMin = 20.0, fMax = 20000.0;
    double lf = (double)(pt.x - rc->left - 40) / (W - 60);
    lf = lf < 0 ? 0 : (lf > 1 ? 1 : lf);
    freq = std::pow(10.0, std::log10(fMin) + lf * (std::log10(fMax) - std::log10(fMin)));
    double g = 1.0 - (double)(pt.y - rc->top - 10) / (H - 30);
    g = g < 0 ? 0 : (g > 1 ? 1 : g);
    gainDb = -12.0 + g * 24.0;
}

static dsp::EqBand& band_from(int idx)
{
    for (auto& st : g_app.devCfg.chain) {
        if (st.type == dsp::StageType::Eq10) return st.bands[idx];
    }
    static dsp::EqBand fallback;
    return fallback;
}

LRESULT CALLBACK EqViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static EqViewState st;
    RECT rc;
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &rc);
            eq_view_paint(hwnd, hdc, &rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            GetClientRect(hwnd, &rc);
            POINT pt{(short)LOWORD(lParam), (short)HIWORD(lParam)};
            st.mouseBand = eq_view_hit_band(hwnd, pt, &rc);
            if (st.mouseBand >= 0) {
                g_app.selBand = st.mouseBand;
                st.dragging = true;
                SetCapture(hwnd);
                refresh_eq_ui(g_app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!st.dragging || st.mouseBand < 0) return 0;
            GetClientRect(hwnd, &rc);
            POINT pt{(short)LOWORD(lParam), (short)HIWORD(lParam)};
            double freq, gain;
            eq_view_pixel_to_band(hwnd, pt, &rc, freq, gain);
            for (auto& s : g_app.devCfg.chain) {
                if (s.type == dsp::StageType::Eq10) {
                    s.bands[st.mouseBand].freq = (float)freq;
                    s.bands[st.mouseBand].gainDb = (float)gain;
                }
            }
            refresh_eq_ui(g_app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            st.dragging = false;
            ReleaseCapture();
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            GetClientRect(hwnd, &rc);
            POINT pt{(short)LOWORD(lParam), (short)HIWORD(lParam)};
            const int b = eq_view_hit_band(hwnd, pt, &rc);
            if (b >= 0) {
                for (auto& s : g_app.devCfg.chain) {
                    if (s.type == dsp::StageType::Eq10) {
                        s.bands[b].enabled = !s.bands[b].enabled;
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Meter control
// ---------------------------------------------------------------------------
LRESULT CALLBACK MeterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static float inLevel = 0.0f, outLevel = 0.0f;
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_app.hbrPanel2);
            const int id = (int)GetWindowLongPtrW(hwnd, GWLP_ID);
            const float level = (id == 1) ? inLevel : outLevel;
            const int w = (int)((rc.right - rc.left - 4) * level);
            RECT bar{rc.left + 2, rc.top + 2, rc.left + 2 + w, rc.bottom - 2};
            if (bar.right > bar.left) {
                HBRUSH br = CreateSolidBrush(level > 0.98f ? kRed : kGreen);
                FillRect(hdc, &bar, br);
                DeleteObject(br);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_USER + 1: {
            if (GetWindowLongPtrW(hwnd, GWLP_ID) == 1) inLevel = *(float*)wParam;
            else outLevel = *(float*)wParam;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Plugin dialog
// ---------------------------------------------------------------------------
void populate_plugin_list(App& a)
{
    if (!a.hwndPluginList) return;
    SendMessageW(a.hwndPluginList, LB_RESETCONTENT, 0, 0);
    a.pluginCache.clear();
    plugins::load_plugin_cache(a.pluginCache);
    for (const auto& p : a.pluginCache) {
        std::wstring label = p.name.empty() ? p.path : p.name;
        if (p.format == 3) label += L"  [VST3]";
        else label += L"  [VST2]";
        if (p.status != 0) label += L"  (failed scan)";
        SendMessageW(a.hwndPluginList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    if (a.scanning) {
        SendMessageW(a.hwndPluginList, LB_ADDSTRING, 0, (LPARAM)L"Scanning plugins...");
    }
}

LRESULT CALLBACK PluginDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App& a = g_app;
    switch (msg) {
        case WM_CREATE: {
            a.hwndPluginDialog = hwnd;
            a.hwndPluginList = CreateWindowExW(0, L"LISTBOX", nullptr,
                                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
                                               LBS_NOTIFY | LBS_HASSTRINGS,
                                               16, 16, 380, 240, hwnd, (HMENU)101,
                                               a.hInst, nullptr);
            mk_button(hwnd, 102, L"Add", 412, 16, 90, 28);
            mk_button(hwnd, 104, L"Rescan", 412, 52, 90, 28);
            mk_button(hwnd, IDCANCEL, L"Close", 412, 88, 90, 28);
            mk_label(hwnd, 0, L"Plugins are scanned by STGRScan (separate process);",
                     16, 266, 460, 16);
            mk_label(hwnd, 0, L"failed plugins are marked and will not crash STGR.",
                     16, 284, 460, 16);
            populate_plugin_list(a);
            SetTimer(hwnd, 1, 500, nullptr);
            return 0;
        }
        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            if (id == 102) { // Add
                const int sel = (int)SendMessageW(a.hwndPluginList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)a.pluginCache.size()) {
                    const auto& p = a.pluginCache[sel];
                    if (p.status == 0) {
                        dsp::StageParams st;
                        st.type = dsp::StageType::Plugin;
                        st.enabled = true;
                        st.pluginInstanceId = to_utf8(p.path);
                        st.pluginName = to_utf8(p.name);
                        st.pluginPath = to_utf8(p.path);
                        st.pluginFormat = p.format;
                        a.devCfg.chain.push_back(st);
                        refresh_chain_ui(a);
                    }
                }
                return 0;
            }
            if (id == 104) { // Rescan
                wchar_t exePath[MAX_PATH]{};
                if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
                    const std::wstring dir = install_dir();
                    std::wstring cmd = L"\"" + dir + L"\\STGRScan.exe\"";
                    for (const auto& d : plugins::default_vst3_dirs())
                        cmd += L" --dir \"" + d + L"\"";
                    for (const auto& d : plugins::default_vst2_dirs())
                        cmd += L" --dir \"" + d + L"\"";
                    STARTUPINFOW si{sizeof(si)};
                    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &a.scanProc)) {
                        a.scanning = true;
                        a.scanStartedAt = GetTickCount64();
                    }
                }
                return 0;
            }
            if (id == IDCANCEL || id == IDOK) {
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }
        case WM_TIMER:
            if (a.scanning) {
                DWORD code = 0;
                if (!GetExitCodeProcess(a.scanProc.hProcess, &code) || code != STILL_ACTIVE) {
                    a.scanning = false;
                    CloseHandle(a.scanProc.hProcess);
                    CloseHandle(a.scanProc.hThread);
                    populate_plugin_list(a);
                }
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            if (a.scanning && a.scanProc.hProcess) {
                DWORD code = 0;
                if (GetExitCodeProcess(a.scanProc.hProcess, &code) && code == STILL_ACTIVE)
                    TerminateProcess(a.scanProc.hProcess, 0);
                CloseHandle(a.scanProc.hProcess);
                CloseHandle(a.scanProc.hThread);
                a.scanning = false;
            }
            a.hwndPluginDialog = nullptr;
            a.hwndPluginList = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void open_plugin_dialog(App& a)
{
    if (a.hwndPluginDialog) {
        SetForegroundWindow(a.hwndPluginDialog);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = PluginDialogProc;
        wc.hInstance = a.hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"StgrPluginDialog";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }
    RECT wr{};
    GetWindowRect(a.hwnd, &wr);
    const int w = 540, h = 340;
    CreateWindowExW(WS_EX_WINDOWEDGE, L"StgrPluginDialog", L"Add Plugin",
                    WS_POPUP | WS_CAPTION | WS_SYSMENU,
                    wr.left + (wr.right - wr.left - w) / 2,
                    wr.top + (wr.bottom - wr.top - h) / 2,
                    w, h, a.hwnd, nullptr, a.hInst, nullptr);
}

// ---------------------------------------------------------------------------
// Action helpers
// ---------------------------------------------------------------------------
void signal_config_changed()
{
    const HANDLE evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, STGR_EVENT_CFG);
    if (evt) {
        SetEvent(evt);
        CloseHandle(evt);
    }
}

void spawn_admin(App& a, const std::wstring& args)
{
    const std::wstring exe = install_dir() + L"\\STGRAdmin.exe";
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        MessageBoxW(a.hwnd, (L"Could not start the elevated helper.\n\n"
                     L"Run the operation from an administrator console:\n"
                     + exe + L" " + args).c_str(),
                     L"STGR - elevation required", MB_ICONWARNING);
    }
}

void apply_config(App& a, bool restartAudio)
{
    // Persist the current device configuration.
    a.mgr.save_device(a.devCfg);

    // Attach/detach requires elevation.
    if (a.selDevice >= 0) {
        const auto& dev = a.devices[a.selDevice];
        const bool shouldAttach = a.devCfg.enabled;
        const bool isAttached = devices::DeviceManager::is_stgr_attached(dev.id);
        if (shouldAttach && !isAttached) {
            spawn_admin(a, L"--attach \"" + dev.id + L"\"");
        } else if (!shouldAttach && isAttached) {
            spawn_admin(a, L"--detach \"" + dev.id + L"\"");
        }
    }

    signal_config_changed();

    if (restartAudio) {
        if (MessageBoxW(a.hwnd,
                        L"To apply the endpoint effect registration the audio "
                        L"service must be restarted.\n\nAll audio output will pause "
                        L"for a few seconds. Continue?",
                        L"STGR", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            spawn_admin(a, L"--restart-audio");
        }
    }
}

void refresh_devices(App& a)
{
    devices::DeviceManager dm;
    dm.enumerate(a.devices);
    HWND list = GetDlgItem(a.hwnd, CtlDeviceList);
    if (!list) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& d : a.devices) {
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)d.name.c_str());
    }
    // Preselect the default device.
    int sel = 0;
    for (size_t i = 0; i < a.devices.size(); ++i) {
        if (a.devices[i].isDefault) { sel = (int)i; break; }
    }
    SendMessageW(list, LB_SETCURSEL, sel, 0);
    a.selDevice = sel;
    on_device_selected(a);
}

void on_device_selected(App& a)
{
    if (a.selDevice < 0 || a.selDevice >= (int)a.devices.size()) {
        a.devCfg = config::DeviceConfig{};
        return;
    }
    const auto& dev = a.devices[a.selDevice];
    const std::string id = to_utf8(dev.id);

    config::DeviceConfig cfg;
    if (a.mgr.load_device(id, cfg)) {
        a.devCfg = std::move(cfg);
    } else {
        a.devCfg = config::DeviceConfig{};
        a.devCfg.endpointId = id;
        a.devCfg.endpointName = to_utf8(dev.name);
        a.devCfg.enabled = true;
    }
    a.attached = devices::DeviceManager::is_stgr_attached(dev.id);
    refresh_status(a);
    refresh_chain_ui(a);
    refresh_eq_ui(a);
}

void refresh_status(App& a)
{
    HWND st = GetDlgItem(a.hwnd, CtlStatusText);
    if (!st) return;
    std::wstring text;
    if (a.selDevice < 0) {
        text = L"No microphone detected";
    } else {
        text = a.attached ? L"STGR processing active" : L"STGR not attached to this microphone";
    }
    SetWindowTextW(st, text.c_str());
}

void refresh_chain_ui(App& a)
{
    HWND list = GetDlgItem(a.hwnd, CtlChainList);
    if (!list) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < a.devCfg.chain.size(); ++i) {
        std::wstring name = to_wide(dsp::stage_type_name(a.devCfg.chain[i].type));
        if (a.devCfg.chain[i].type == dsp::StageType::Plugin) {
            name = to_wide(a.devCfg.chain[i].pluginName.empty()
                               ? a.devCfg.chain[i].pluginInstanceId
                               : a.devCfg.chain[i].pluginName);
        }
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name.c_str());
    }
    if (a.selChain >= (int)a.devCfg.chain.size()) a.selChain = -1;
    if (a.selChain >= 0)
        SendMessageW(list, LB_SETCURSEL, a.selChain, 0);
}

void refresh_eq_ui(App& a)
{
    // Populate the type combo + band editors from the selected band.
    HWND combo = GetDlgItem(a.hwnd, CtlBtnEqType);
    if (combo) {
        const int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        const wchar_t* types[] = {L"Peaking", L"Low Shelf", L"High Shelf",
                                  L"High Pass", L"Low Pass", L"Notch"};
        for (const wchar_t* t : types)
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)t);
        SendMessageW(combo, CB_SETCURSEL, sel >= 0 ? sel : 0, 0);
    }
    if (a.selBand >= 0 && a.selBand < dsp::kMaxEqBands) {
        const dsp::EqBand& b = band_from(a.selBand);
        SetWindowTextW(GetDlgItem(a.hwnd, CtlEqFreq), fmt_double_w(b.freq, 0).c_str());
        SetWindowTextW(GetDlgItem(a.hwnd, CtlEqGain), fmt_double_w(b.gainDb, 1).c_str());
        SetWindowTextW(GetDlgItem(a.hwnd, CtlEqQ), fmt_double_w(b.q, 2).c_str());
    }
    if (a.hwndEqView) InvalidateRect(a.hwndEqView, nullptr, FALSE);
}

void refresh_presets(App& a)
{
    HWND combo = GetDlgItem(a.hwnd, CtlPresetCombo);
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    a.presetNames.clear();
    for (const auto& p : config::default_presets()) {
        a.presetNames.push_back(to_wide(p.name));
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)to_wide(p.name).c_str());
    }
    for (const auto& name : a.mgr.list_presets()) {
        a.presetNames.push_back(to_wide(name));
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)to_wide(name).c_str());
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void update_meters(App& a)
{
    float in = 0.0f, out = 0.0f;
    // Open the bridge header for the selected endpoint (read-only).
    if (a.selDevice >= 0 && a.selDevice < (int)a.devices.size()) {
        const std::wstring safe = devices::DeviceManager::safe_id(a.devices[a.selDevice].id);
        if (a.meterEndpoint != safe) {
            a.meterSection.close();
            a.meterHeader = nullptr;
            a.meterEndpoint.clear();
            if (a.meterSection.open(bridge::shm_name(safe), false,
                                    bridge::section_bytes(), FILE_MAP_READ)) {
                auto* hdr = (bridge::ShmHeader*)a.meterSection.view();
                if (hdr->magic == bridge::kMagic) {
                    a.meterHeader = hdr;
                    a.meterEndpoint = safe;
                } else {
                    a.meterSection.close();
                }
            }
        }
        if (a.meterHeader) {
            in = a.meterHeader->meterInPeak.load(std::memory_order_relaxed);
            out = a.meterHeader->meterOutPeak.load(std::memory_order_relaxed);
        }
    } else {
        a.meterSection.close();
        a.meterHeader = nullptr;
        a.meterEndpoint.clear();
    }

    if (a.hwndMeterIn) SendMessageW(a.hwndMeterIn, WM_USER + 1, (WPARAM)&in, 0);
    if (a.hwndMeterOut) SendMessageW(a.hwndMeterOut, WM_USER + 1, (WPARAM)&out, 0);

    // Latency display.
    HWND lat = GetDlgItem(a.hwnd, CtlLatencyText);
    if (lat && a.meterHeader) {
        const double sr = a.meterHeader->sampleRate.load(std::memory_order_relaxed);
        const double block = a.meterHeader->blockFrames.load(std::memory_order_relaxed);
        const double pluginUs = a.meterHeader->pluginLatencyUs.load(std::memory_order_relaxed);
        const double bridgeMs = sr > 0 ? block / sr * 1000.0 : 0.0;
        wchar_t buf[160];
        swprintf(buf, 160,
                 L"STGR DSP: ~0 ms    Plugin bridge: %.1f ms    Plugins: %.1f ms",
                 bridgeMs, pluginUs / 1000.0);
        SetWindowTextW(lat, buf);
    } else if (lat) {
        SetWindowTextW(lat, L"STGR DSP: ~0 ms");
    }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void fill_diagnostics(App& a, HWND edit)
{
    std::wstring text;
    wchar_t buf[512];
    text += L"STGR Microphone Equalizer " STGR_VERSION_STRING_W L"\r\n\r\n";

    OSVERSIONINFOW osi{sizeof(osi)};
    GetVersionExW(&osi);
    swprintf(buf, 512, L"Windows: %u.%u.%u\r\n", osi.dwMajorVersion, osi.dwMinorVersion, osi.dwBuildNumber);
    text += buf;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    swprintf(buf, 512, L"CPU: %u logical processors\r\n", si.dwNumberOfProcessors);
    text += buf;

    MEMORYSTATUSEX ms{sizeof(ms)};
    GlobalMemoryStatusEx(&ms);
    swprintf(buf, 512, L"RAM: %.1f GB\r\n", ms.ullTotalPhys / 1073741824.0);
    text += buf;

    text += L"\r\nMicrophone:\r\n";
    if (a.selDevice >= 0 && a.selDevice < (int)a.devices.size()) {
        const auto& d = a.devices[a.selDevice];
        text += L"  Name: " + d.name + L"\r\n";
        text += L"  ID: " + d.id + L"\r\n";
        text += L"  Default: " + std::wstring(d.isDefault ? L"yes" : L"no") + L"\r\n";
        text += L"  STGR attached: " + std::wstring(a.attached ? L"yes" : L"no") + L"\r\n";
        text += L"  STGR enabled: " + std::wstring(a.devCfg.enabled ? L"yes" : L"no") + L"\r\n";
    } else {
        text += L"  (none)\r\n";
    }

    text += L"\r\nConfig path: " + devices_cfg_dir() + L"\r\n";
    text += L"Install path: " + install_dir() + L"\r\n";

    // Bridge state.
    if (a.meterHeader) {
        const uint32_t state = a.meterHeader->serverState.load();
        text += L"\r\nPlugin bridge: " + to_wide(bridge::server_state_name((bridge::ServerState)state)) + L"\r\n";
        const uint32_t n = a.meterHeader->activePluginCount.load();
        swprintf(buf, 512, L"Active plugins: %u\r\n", n);
        text += buf;
    } else {
        text += L"\r\nPlugin bridge: no endpoint section\r\n";
    }

    SetWindowTextW(edit, text.c_str());
}

// ---------------------------------------------------------------------------
// Command handling
// ---------------------------------------------------------------------------
void on_command(App& a, UINT id)
{
    switch (id) {
        case CtlBtnRefresh:
            refresh_devices(a);
            break;
        case CtlBtnAttach: {
            if (a.selDevice >= 0) {
                const auto& dev = a.devices[a.selDevice];
                const bool attach = !devices::DeviceManager::is_stgr_attached(dev.id);
                spawn_admin(a, std::wstring(attach ? L"--attach \"" : L"--detach \"") + dev.id + L"\"");
                apply_config(a, true);
                refresh_devices(a);
            }
            break;
        }
        case CtlBtnEnable:
            a.devCfg.enabled = !a.devCfg.enabled;
            apply_config(a, false);
            refresh_status(a);
            break;
        case CtlBtnBypass: {
            // Bypass = disable the chain without detaching (temp).
            a.devCfg.enabled = !a.devCfg.enabled;
            apply_config(a, false);
            refresh_status(a);
            break;
        }
        case CtlBtnApply:
            apply_config(a, false);
            break;
        case CtlBtnReset: {
            a.devCfg.chain.clear();
            apply_config(a, false);
            refresh_chain_ui(a);
            refresh_eq_ui(a);
            break;
        }
        case CtlBtnEqAdd: {
            bool added = false;
            for (auto& st : a.devCfg.chain) {
                if (st.type == dsp::StageType::Eq10) {
                    for (int i = 0; i < dsp::kMaxEqBands; ++i) {
                        if (!st.bands[i].enabled) {
                            st.bands[i] = dsp::EqBand{true, dsp::FilterType::Peaking,
                                                      1000.0f * (float)(i + 1), 0.0f, 0.707f};
                            a.selBand = i;
                            added = true;
                            break;
                        }
                    }
                    break;
                }
            }
            if (!added) {
                dsp::StageParams st;
                st.type = dsp::StageType::Eq10;
                st.enabled = true;
                st.bands[0] = dsp::EqBand{true, dsp::FilterType::Peaking, 1000.0f, 0.0f, 0.707f};
                a.devCfg.chain.push_back(st);
                a.selBand = 0;
            }
            refresh_chain_ui(a);
            refresh_eq_ui(a);
            break;
        }
        case CtlBtnEqDel: {
            if (a.selBand >= 0) {
                for (auto& st : a.devCfg.chain) {
                    if (st.type == dsp::StageType::Eq10) {
                        st.bands[a.selBand].enabled = false;
                        a.selBand = -1;
                    }
                }
                refresh_eq_ui(a);
            }
            break;
        }
        case CtlBtnEqReset:
            for (auto& st : a.devCfg.chain) {
                if (st.type == dsp::StageType::Eq10) {
                    for (int i = 0; i < dsp::kMaxEqBands; ++i) st.bands[i] = dsp::EqBand{};
                }
            }
            refresh_eq_ui(a);
            break;
        case CtlBtnEqType: {
            if (a.selBand >= 0) {
                const int t = (int)SendMessageW(GetDlgItem(a.hwnd, CtlBtnEqType), CB_GETCURSEL, 0, 0);
                for (auto& st : a.devCfg.chain) {
                    if (st.type == dsp::StageType::Eq10) {
                        st.bands[a.selBand].type = (dsp::FilterType)t;
                    }
                }
                if (a.hwndEqView) InvalidateRect(a.hwndEqView, nullptr, FALSE);
            }
            break;
        }
        case CtlBtnChainUp: case CtlBtnChainDown: {
            const int n = (int)a.devCfg.chain.size();
            const int idx = a.selChain;
            const int to = (id == CtlBtnChainUp) ? idx - 1 : idx + 1;
            if (idx >= 0 && to >= 0 && to < n) {
                std::swap(a.devCfg.chain[idx], a.devCfg.chain[to]);
                a.selChain = to;
                refresh_chain_ui(a);
            }
            break;
        }
        case CtlBtnChainRemove: {
            if (a.selChain >= 0 && a.selChain < (int)a.devCfg.chain.size()) {
                a.devCfg.chain.erase(a.devCfg.chain.begin() + a.selChain);
                a.selChain = -1;
                refresh_chain_ui(a);
            }
            break;
        }
        case CtlBtnChainEnable: {
            if (a.selChain >= 0 && a.selChain < (int)a.devCfg.chain.size()) {
                a.devCfg.chain[a.selChain].enabled = !a.devCfg.chain[a.selChain].enabled;
                refresh_chain_ui(a);
            }
            break;
        }
        case CtlBtnChainParams: {
            if (a.selChain >= 0 && a.selChain < (int)a.devCfg.chain.size() &&
                a.devCfg.chain[a.selChain].type == dsp::StageType::Plugin) {
                MessageBoxW(a.hwnd,
                            L"Plugin parameters are configured in the plugin's own "
                            L"editor and stored with the chain.",
                            L"STGR - plugin parameters", MB_OK);
            }
            break;
        }
        case CtlBtnChainAdd: {
            HMENU menu = CreatePopupMenu();
            const struct { const wchar_t* name; dsp::StageType type; } items[] = {
                {L"Gain", dsp::StageType::Gain},
                {L"High Pass", dsp::StageType::HighPass},
                {L"Low Pass", dsp::StageType::LowPass},
                {L"Low Shelf", dsp::StageType::LowShelf},
                {L"High Shelf", dsp::StageType::HighShelf},
                {L"Peaking", dsp::StageType::Peaking},
                {L"Notch", dsp::StageType::Notch},
                {L"Parametric EQ (10 bands)", dsp::StageType::Eq10},
                {L"Noise Gate", dsp::StageType::Gate},
                {L"Expander", dsp::StageType::Expander},
                {L"Compressor", dsp::StageType::Compressor},
                {L"Limiter", dsp::StageType::Limiter},
            };
            for (int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); ++i)
                AppendMenuW(menu, MF_STRING, 5000 + i, items[i].name);
            POINT pt{};
            GetCursorPos(&pt);
            const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD, pt.x, pt.y, 0, a.hwnd, nullptr);
            DestroyMenu(menu);
            if (cmd >= 5000) {
                dsp::StageParams st;
                st.type = items[cmd - 5000].type;
                st.enabled = true;
                switch (st.type) {
                    case dsp::StageType::Gain: st.gainDb = 0.0f; break;
                    case dsp::StageType::HighPass: st.freq = 80.0f; st.q = 0.707f; break;
                    case dsp::StageType::LowPass: st.freq = 12000.0f; st.q = 0.707f; break;
                    case dsp::StageType::Limiter: st.ceilingDb = -1.0f; break;
                    case dsp::StageType::Gate: st.thresholdDb = -40.0f; break;
                    case dsp::StageType::Compressor: st.thresholdDb = -20.0f; st.ratio = 3.0f; break;
                    default: break;
                }
                a.devCfg.chain.push_back(st);
                a.selChain = (int)a.devCfg.chain.size() - 1;
                refresh_chain_ui(a);
            }
            break;
        }
        case CtlBtnChainPlugin:
            open_plugin_dialog(a);
            break;
        case CtlPresetCombo: {
            // Load the selected preset.
            const int sel = (int)SendMessageW(GetDlgItem(a.hwnd, CtlPresetCombo), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)a.presetNames.size()) {
                config::Preset preset;
                const std::wstring& name = a.presetNames[sel];
                if (a.mgr.load_preset(to_utf8(name), preset)) {
                    a.devCfg.chain = preset.chain;
                    refresh_chain_ui(a);
                    refresh_eq_ui(a);
                } else {
                    // Built-in preset.
                    for (const auto& p : config::default_presets()) {
                        if (p.name == to_utf8(name)) {
                            a.devCfg.chain = p.chain;
                            refresh_chain_ui(a);
                            refresh_eq_ui(a);
                            break;
                        }
                    }
                }
            }
            break;
        }
        case CtlBtnPresetLoad: {
            const int sel = (int)SendMessageW(GetDlgItem(a.hwnd, CtlPresetCombo), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)a.presetNames.size()) {
                config::Preset preset;
                const std::wstring& name = a.presetNames[sel];
                if (a.mgr.load_preset(to_utf8(name), preset)) {
                    a.devCfg.chain = preset.chain;
                } else {
                    for (const auto& p : config::default_presets()) {
                        if (p.name == to_utf8(name)) {
                            a.devCfg.chain = p.chain;
                            break;
                        }
                    }
                }
                refresh_chain_ui(a);
                refresh_eq_ui(a);
            }
            break;
        }
        case CtlBtnPresetSave: {
            wchar_t nameBuf[256]{};
            if (InputBox(a.hwnd, L"Save preset as:", L"STGR - save preset", nameBuf, 256)) {
                config::Preset p;
                p.name = to_utf8(nameBuf);
                p.chain = a.devCfg.chain;
                a.mgr.save_preset(p);
                refresh_presets(a);
            }
            break;
        }
        case CtlBtnPresetDelete: {
            const int sel = (int)SendMessageW(GetDlgItem(a.hwnd, CtlPresetCombo), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)a.presetNames.size()) {
                a.mgr.remove_preset(to_utf8(a.presetNames[sel]));
                refresh_presets(a);
            }
            break;
        }
        case CtlBtnPresetDuplicate: {
            const int sel = (int)SendMessageW(GetDlgItem(a.hwnd, CtlPresetCombo), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)a.presetNames.size()) {
                wchar_t nameBuf[256]{};
                if (InputBox(a.hwnd, L"Duplicate as:", L"STGR - duplicate preset", nameBuf, 256)) {
                    config::Preset p;
                    p.name = to_utf8(nameBuf);
                    p.chain = a.devCfg.chain;
                    a.mgr.save_preset(p);
                    refresh_presets(a);
                }
            }
            break;
        }
        case CtlBtnDiagExport: {
            wchar_t path[MAX_PATH]{};
            OPENFILENAMEW ofn{sizeof(ofn)};
            ofn.hwndOwner = a.hwnd;
            ofn.lpstrFilter = L"Text files\0*.txt\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"txt";
            if (GetSaveFileNameW(&ofn)) {
                wchar_t buf[8192];
                GetWindowTextW(GetDlgItem(a.hwnd, CtlDiagText), buf, 8192);
                std::wstring w = buf;
                write_file_text(path, to_utf8(w));
            }
            break;
        }
        case CtlBtnDiagRestartAudio:
            spawn_admin(a, L"--restart-audio");
            break;
        default:
            break;
    }
}

// Simple input box helper (dialog with a single edit field).
bool InputBox(HWND parent, const wchar_t* prompt, const wchar_t* title,
              wchar_t* out, int outLen)
{
    // Minimal implementation: use a dialog template-free approach.
    static wchar_t result[512];
    result[0] = 0;
    wcscpy_s(result, out);

    // Create a small modal dialog manually.
    const int W = 360, H = 120;
    RECT wr{};
    GetWindowRect(parent, &wr);
    const int x = wr.left + (wr.right - wr.left - W) / 2;
    const int y = wr.top + (wr.bottom - wr.top - H) / 2;

    HWND dlg = CreateWindowExW(WS_EX_WINDOWEDGE, L"STATIC", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               x, y, W, H, parent, nullptr, g_app.hInst, nullptr);
    CreateWindowExW(0, L"STATIC", prompt, WS_CHILD | WS_VISIBLE, 16, 14, W - 32, 18,
                    dlg, nullptr, g_app.hInst, nullptr);
    HWND edit = CreateWindowExW(0, L"EDIT", out, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                16, 40, W - 32, 24, dlg, (HMENU)1, g_app.hInst, nullptr);
    HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                              W - 180, 80, 80, 26, dlg, (HMENU)IDOK, g_app.hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                    W - 92, 80, 76, 26, dlg, (HMENU)IDCANCEL, g_app.hInst, nullptr);
    SendMessageW(dlg, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
    ShowWindow(dlg, SW_SHOW);

    // Local message loop for the modal dialog.
    bool done = false;
    bool okResult = false;
    while (!done) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
                GetWindowTextW(edit, result, 512);
                okResult = true;
                done = true;
                break;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                done = true;
                break;
            }
            if (msg.message == WM_COMMAND) {
                if (LOWORD(msg.wParam) == IDOK) {
                    GetWindowTextW(edit, result, 512);
                    okResult = true;
                    done = true;
                    break;
                }
                if (LOWORD(msg.wParam) == IDCANCEL) {
                    done = true;
                    break;
                }
            }
            if (msg.message == WM_DESTROY) { done = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done) break;
        if (!IsWindow(dlg)) break;
        WaitMessage();
    }
    DestroyWindow(dlg);

    if (okResult) {
        wcsncpy_s(out, outLen, result, _TRUNCATE);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Main window proc
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App& a = g_app;
    switch (msg) {
        case WM_CREATE:
            build_ui(hwnd);
            SetTimer(hwnd, 1, 33, nullptr); // meter refresh ~30fps
            return 0;

        case WM_TIMER:
            if (wParam == 1) update_meters(a);
            return 0;

        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            // EQ band edit fields: apply on focus loss.
            if (HIWORD(wParam) == EN_KILLFOCUS &&
                (id == CtlEqFreq || id == CtlEqGain || id == CtlEqQ)) {
                if (a.selBand >= 0 && a.selBand < dsp::kMaxEqBands) {
                    wchar_t buf[64];
                    for (auto& st : a.devCfg.chain) {
                        if (st.type != dsp::StageType::Eq10) continue;
                        GetWindowTextW(GetDlgItem(hwnd, CtlEqFreq), buf, 64);
                        st.bands[a.selBand].freq = (float)_wtof(buf);
                        GetWindowTextW(GetDlgItem(hwnd, CtlEqGain), buf, 64);
                        st.bands[a.selBand].gainDb = (float)_wtof(buf);
                        GetWindowTextW(GetDlgItem(hwnd, CtlEqQ), buf, 64);
                        st.bands[a.selBand].q = (float)_wtof(buf);
                    }
                    InvalidateRect(a.hwndEqView, nullptr, FALSE);
                }
                return 0;
            }
            if (id >= CtlTabMic && id <= CtlTabAbout) {
                const int tab = id - CtlTabMic;
                if (tab != a.tab) {
                    a.tab = tab;
                    for (int i = 0; i < kTabCount; ++i) {
                        ShowWindow(a.hwndTabPanels[i], i == tab ? SW_SHOW : SW_HIDE);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    for (int i = 0; i < kTabCount; ++i) InvalidateRect(a.hwndTabs[i], nullptr, FALSE);
                    if (tab == 4) fill_diagnostics(a, GetDlgItem(hwnd, CtlDiagText));
                }
                return 0;
            }
            if (id == CtlDeviceList && HIWORD(wParam) == LBN_SELCHANGE) {
                a.selDevice = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
                on_device_selected(a);
                return 0;
            }
            if (id == CtlChainList && HIWORD(wParam) == LBN_SELCHANGE) {
                a.selChain = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
                return 0;
            }
            on_command(a, id);
            return 0;
        }

        case WM_MEASUREITEM: {
            auto* mis = (MEASUREITEMSTRUCT*)lParam;
            mis->itemHeight = 24;
            return TRUE;
        }

        case WM_DRAWITEM: {
            auto* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlID >= CtlTabMic && dis->CtlID <= CtlTabAbout) {
                paint_tab(hwnd, dis);
                return TRUE;
            }
            if (dis->CtlID == CtlDeviceList) {
                paint_device_list(hwnd, dis);
                return TRUE;
            }
            if (dis->CtlID == CtlChainList) {
                paint_chain_list(hwnd, dis);
                return TRUE;
            }
            if (dis->CtlID >= 300 && dis->CtlID < 300 + kTabCount) {
                RECT rc = dis->rcItem;
                paint_panel(hwnd, dis->hDC, &rc);
                return TRUE;
            }
            return FALSE;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, kPanel);
            SetTextColor(hdc, kText);
            return (LRESULT)a.hbrPanel;
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, kPanel);
            SetTextColor(hdc, kText);
            return (LRESULT)a.hbrPanel;
        }
        case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, kBg);
            SetTextColor(hdc, kText);
            return (LRESULT)a.hbrBg;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, a.hbrBg);

            // Header.
            SetBkMode(hdc, TRANSPARENT);
            HFONT old = (HFONT)SelectObject(hdc, a.hFontBig);
            SetTextColor(hdc, kText);
            RECT title{20, 10, 600, 40};
            DrawTextW(hdc, L"STGR MICROPHONE EQUALIZER", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, a.hFont);
            SetTextColor(hdc, kTextDim);
            RECT sub{22, 40, 600, 58};
            DrawTextW(hdc, L"System-level microphone processing", -1, &sub, DT_LEFT | DT_SINGLELINE);
            SelectObject(hdc, old);

            // Red accent line.
            HBRUSH red = CreateSolidBrush(kRed);
            RECT line{0, 88, rc.right, 90};
            FillRect(hdc, &line, red);
            DeleteObject(red);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace stgr::gui

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    using namespace stgr;
    using namespace stgr::gui;

    g_app.hInst = hInstance;

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // Single instance.
    CreateMutexW(nullptr, TRUE, L"Local\\STGR_GUI_Singleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"StgrMainWnd", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    // Register classes.
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"StgrMainWnd";
    RegisterClassExW(&wc);

    WNDCLASSEXW wcEq{sizeof(wcEq)};
    wcEq.lpfnWndProc = EqViewProc;
    wcEq.hInstance = hInstance;
    wcEq.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wcEq.lpszClassName = L"StgrEqView";
    RegisterClassExW(&wcEq);

    WNDCLASSEXW wcMeter{sizeof(wcMeter)};
    wcMeter.lpfnWndProc = MeterProc;
    wcMeter.hInstance = hInstance;
    wcMeter.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcMeter.lpszClassName = L"StgrMeter";
    RegisterClassExW(&wcMeter);

    HWND hwnd = CreateWindowExW(0, L"StgrMainWnd",
                                L"STGR Microphone Equalizer",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 900, 640,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
