// STGRTray.exe - lightweight system tray application.
//
// The tray manages STGR lifecycle flags and provides quick access to the
// GUI. Selecting "Exit" only closes the tray: the APO keeps processing.
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

#include "../common/version.h"
#include "../common/paths.h"
#include "../common/util.h"
#include "../config/manager.h"
#include "../devices/device_manager.h"
#include "../gui/resource.h"

#pragma comment(lib, "shell32.lib")

namespace {

constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kMenuOpen = 1, kMenuToggle = 2, kMenuReload = 3, kMenuDiag = 4,
               kMenuStartup = 5, kMenuAbout = 6, kMenuExit = 7, kMenuMicrophoneBase = 100,
               kMenuPresetBase = 200;

HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid{};
bool g_processing = true;
std::vector<stgr::devices::DeviceInfo> g_devices;
std::vector<std::wstring> g_presets;
std::wstring g_currentDevice;

using namespace stgr;

void refresh_processing_flag()
{
    config::GlobalConfig gc;
    if (config::ConfigManager().load_global(gc)) {
        g_processing = gc.processingEnabled;
    }
}

void set_processing(bool on)
{
    config::ConfigManager mgr;
    config::GlobalConfig gc;
    if (!mgr.load_global(gc)) gc = config::GlobalConfig{};
    gc.processingEnabled = on;
    mgr.save_global(gc);
    g_processing = on;

    // Notify the APO (it watches the config file anyway).
    const HANDLE evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, STGR_EVENT_CFG);
    if (evt) { SetEvent(evt); CloseHandle(evt); }
}

bool is_startup_enabled()
{
    HKEY hk = nullptr;
    wchar_t value[MAX_PATH]{};
    DWORD size = sizeof(value);
    const bool ok = RegOpenKeyExW(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0, KEY_READ, &hk) == ERROR_SUCCESS &&
                    RegQueryValueExW(hk, L"STGRTray", nullptr, nullptr,
                                     (LPBYTE)value, &size) == ERROR_SUCCESS;
    if (hk) RegCloseKey(hk);
    return ok;
}

void set_startup(bool on)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_WRITE, &hk) != ERROR_SUCCESS)
        return;

    const std::wstring tray = install_dir() + L"\\STGRTray.exe";
    const std::wstring server = install_dir() + L"\\STGRAudioServer.exe";
    if (on) {
        std::wstring q = L"\"" + tray + L"\"";
        RegSetValueExW(hk, L"STGRTray", 0, REG_SZ, (const BYTE*)q.c_str(),
                       (DWORD)(q.size() + 1) * sizeof(wchar_t));
        q = L"\"" + server + L"\"";
        RegSetValueExW(hk, L"STGRAudioServer", 0, REG_SZ, (const BYTE*)q.c_str(),
                       (DWORD)(q.size() + 1) * sizeof(wchar_t));
    } else {
        RegDeleteValueW(hk, L"STGRTray");
        RegDeleteValueW(hk, L"STGRAudioServer");
    }
    RegCloseKey(hk);
}

void open_gui()
{
    const std::wstring exe = install_dir() + L"\\STGRMicrophoneEqualizer.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_diagnostics()
{
    const std::wstring exe = install_dir() + L"\\STGRMicrophoneEqualizer.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), L"--diagnostics", nullptr, SW_SHOWNORMAL);
}

void refresh_lists()
{
    devices::DeviceManager dm;
    dm.enumerate(g_devices);
    config::ConfigManager mgr;
    config::GlobalConfig gc;
    if (mgr.load_global(gc)) g_processing = gc.processingEnabled;
    g_presets.clear();
    for (const auto& name : mgr.list_presets())
        g_presets.push_back(to_wide(name));
}

void update_tray_icon(bool processing)
{
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"tray.ico",
                                    IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wcscpy_s(g_nid.szTip, processing ? L"STGR: processing active" : L"STGR: processing disabled");
    Shell_NotifyIconW(processing ? NIM_MODIFY : NIM_MODIFY, &g_nid);
}

void build_menu(HMENU menu)
{
    MENUITEMINFOW mii{sizeof(mii)};

    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open STGR Microphone Equalizer");
    AppendMenuW(menu, MF_STRING | (g_processing ? MF_CHECKED : 0), kMenuToggle,
                L"Processing Enabled");

    // Microphone submenu.
    HMENU micMenu = CreatePopupMenu();
    for (size_t i = 0; i < g_devices.size(); ++i) {
        const auto& d = g_devices[i];
        std::wstring label = d.name;
        if (d.isDefault) label += L"  (default)";
        AppendMenuW(micMenu, MF_STRING, kMenuMicrophoneBase + (UINT)i, label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)micMenu, L"Microphone");

    // Presets submenu.
    HMENU presetMenu = CreatePopupMenu();
    for (size_t i = 0; i < g_presets.size(); ++i)
        AppendMenuW(presetMenu, MF_STRING, kMenuPresetBase + (UINT)i, g_presets[i].c_str());
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)presetMenu, L"Presets");

    AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload Configuration");
    AppendMenuW(menu, MF_STRING, kMenuDiag, L"Diagnostics");
    AppendMenuW(menu, MF_STRING | (is_startup_enabled() ? MF_CHECKED : 0), kMenuStartup,
                L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"About");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
}

void apply_preset(const std::wstring& name)
{
    config::ConfigManager mgr;
    config::Preset preset;
    if (!mgr.load_preset(to_utf8(name), preset)) return;
    if (g_currentDevice.empty()) return;

    config::DeviceConfig cfg;
    if (!mgr.load_device(to_utf8(g_currentDevice), cfg)) return;
    cfg.chain = preset.chain;
    mgr.save_device(cfg);

    const HANDLE evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, STGR_EVENT_CFG);
    if (evt) { SetEvent(evt); CloseHandle(evt); }
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case kTrayMsg:
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP: case WM_CONTEXTMENU: {
                    POINT pt{};
                    GetCursorPos(&pt);
                    HMENU menu = CreatePopupMenu();
                    build_menu(menu);
                    SetForegroundWindow(hwnd);
                    const UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                          pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(menu);

                    if (cmd == kMenuOpen) open_gui();
                    else if (cmd == kMenuToggle) set_processing(!g_processing);
                    else if (cmd == kMenuReload) {
                        const HANDLE evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, STGR_EVENT_CFG);
                        if (evt) { SetEvent(evt); CloseHandle(evt); }
                    } else if (cmd == kMenuDiag) open_diagnostics();
                    else if (cmd == kMenuStartup) set_startup(!is_startup_enabled());
                    else if (cmd == kMenuAbout) {
                        MessageBoxW(hwnd,
                                    L"STGR Microphone Equalizer " STGR_VERSION_STRING_W L"\n"
                                    L"GPL-3.0\n\n"
                                    L"Closing this tray does not stop audio processing.",
                                    L"STGR", MB_OK);
                    } else if (cmd == kMenuExit) {
                        // Only exit the tray; processing keeps running.
                        Shell_NotifyIconW(NIM_DELETE, &g_nid);
                        PostQuitMessage(0);
                    } else if (cmd >= kMenuMicrophoneBase && cmd < kMenuMicrophoneBase + g_devices.size()) {
                        g_currentDevice = g_devices[cmd - kMenuMicrophoneBase].id;
                    } else if (cmd >= kMenuPresetBase && cmd < kMenuPresetBase + g_presets.size()) {
                        apply_preset(g_presets[cmd - kMenuPresetBase]);
                    }
                    return 0;
                }
                case WM_LBUTTONDBLCLK:
                    open_gui();
                    return 0;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"StgrTrayWnd";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, L"StgrTrayWnd", L"STGRTray", 0, 0, 0, 0, 0,
                             nullptr, nullptr, hInstance, nullptr);

    refresh_lists();

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TRAY), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wcscpy_s(g_nid.szTip, L"STGR Microphone Equalizer");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    update_tray_icon(g_processing);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
