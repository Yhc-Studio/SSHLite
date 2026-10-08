
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cwctype>
#include <cwchar>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <set>

#include "AppIds.h"
#include "AppTypes.h"
#include "SshCommand.h"
#include "SshBackend.h"
#include "AppServices.h"
#include "MainWindow.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {

HINSTANCE g_hInst = nullptr;
HWND g_hwnd = nullptr;
HFONT g_font = nullptr;
HFONT g_fontBold = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSmall = nullptr;
HFONT g_fontMono = nullptr;
HBRUSH g_brushBg = nullptr;
HBRUSH g_brushEdit = nullptr;
HBRUSH g_brushLog = nullptr;

COLORREF C_BG        = RGB(15, 17, 21);
COLORREF C_SIDEBAR   = RGB(18, 21, 26);
COLORREF C_CARD      = RGB(24, 28, 34);
COLORREF C_CARD_ALT  = RGB(21, 24, 30);
COLORREF C_EDIT      = RGB(31, 36, 43);
COLORREF C_LOG       = RGB(12, 14, 18);
COLORREF C_BORDER    = RGB(49, 56, 66);
COLORREF C_TEXT      = RGB(238, 241, 245);
COLORREF C_MUTED     = RGB(151, 160, 173);
COLORREF C_ACCENT    = RGB(96, 142, 255);
COLORREF C_ACCENT_2  = RGB(121, 162, 255);
COLORREF C_DANGER    = RGB(205, 78, 86);
COLORREF C_GREEN     = RGB(62, 166, 115);
COLORREF C_WARNING   = RGB(221, 167, 63);

enum class ThemeMode { Dark=0, Light, Ocean, Emerald, Rose, Custom };
std::atomic<int> g_language{0};
ThemeMode g_themeMode = ThemeMode::Light;
COLORREF g_customAccent = RGB(96, 142, 255);
int g_connectionState = STATE_DISCONNECTED;
int g_navSection = IDC_NAV_QUICK;
std::mutex g_launchEnvMutex;
bool g_warnedPlainSession = false;
bool g_plaintextFavoriteWarningAccepted = false;

RECT g_cardConnection{};
RECT g_cardForward{};
RECT g_cardFavorites{};
RECT g_cardSessions{};
RECT g_cardLog{};
RECT g_cardPageList{};
RECT g_cardPageDetail{};
RECT g_frameCommand{};
RECT g_frameHistory{};
RECT g_frameAuth{};
RECT g_framePassword{};
RECT g_frameKeyPath{};
RECT g_frameKeyPass{};
RECT g_frameUrl{};
RECT g_frameFavorites{};
RECT g_frameFavName{};
RECT g_frameFavPassword{};
RECT g_cardTips{};
RECT g_headerStatus{};

AuthMode g_authMode = AuthMode::Password;
std::vector<std::wstring> g_browserUrls;
int g_browserUrlIndex = 0;
std::vector<LocalForwardInfo> g_localForwards;

std::vector<SavedEntry> g_history;
std::vector<SavedEntry> g_favorites;
std::vector<SessionView> g_sessionViews;

HMENU ControlIdToMenu(int id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

const wchar_t* Tr(const wchar_t* zh, const wchar_t* en) {
    return g_language.load() == 0 ? zh : en;
}

std::wstring GetWindowTextString(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return L"";
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(static_cast<size_t>(len));
    return s;
}

std::wstring GetAppDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

std::wstring GetIniPath() {
    return GetAppDirectory() + L"\\SSHLite.ini";
}

void IniWrite(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), GetIniPath().c_str());
}

std::wstring IniRead(const std::wstring& section, const std::wstring& key,
                     const std::wstring& def = L"") {
    std::vector<wchar_t> buf(4096);
    GetPrivateProfileStringW(section.c_str(), key.c_str(), def.c_str(),
                             buf.data(), static_cast<DWORD>(buf.size()), GetIniPath().c_str());
    return buf.data();
}

void IniDeleteSection(const std::wstring& section) {
    WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, GetIniPath().c_str());
}

int IniReadInt(const std::wstring& section, const std::wstring& key, int def = 0) {
    return static_cast<int>(
        GetPrivateProfileIntW(section.c_str(), key.c_str(), def, GetIniPath().c_str())
    );
}

void PostLog(const std::wstring& s) {
    if (!g_hwnd) return;
    auto* p = new std::wstring(s);
    PostMessageW(g_hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(p));
}

void PostState(int state) {
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_STATE, static_cast<WPARAM>(state), 0);
}

void PostSessionLog(int id, const std::wstring& text) {
    if (!g_hwnd) return;
    auto* ev = new SessionEvent();
    ev->id = id;
    ev->text = text;
    PostMessageW(g_hwnd, WM_APP_SESSION_LOG, 0, reinterpret_cast<LPARAM>(ev));
}

void PostSessionState(int id, int state) {
    if (!g_hwnd) return;
    auto* ev = new SessionEvent();
    ev->id = id;
    ev->state = state;
    PostMessageW(g_hwnd, WM_APP_SESSION_STATE, 0, reinterpret_cast<LPARAM>(ev));
}

SessionManager g_sessionManager;



void DeleteUiBrushes() {
    if (g_brushBg) { DeleteObject(g_brushBg); g_brushBg = nullptr; }
    if (g_brushEdit) { DeleteObject(g_brushEdit); g_brushEdit = nullptr; }
    if (g_brushLog) { DeleteObject(g_brushLog); g_brushLog = nullptr; }
}

void RecreateUiBrushes() {
    DeleteUiBrushes();
    g_brushBg = CreateSolidBrush(C_BG);
    g_brushEdit = CreateSolidBrush(C_EDIT);
    g_brushLog = CreateSolidBrush(C_LOG);
}

COLORREF ShiftColor(COLORREF c, int delta) {
    auto clamp = [](int v) { return (std::max)(0, (std::min)(255, v)); };
    return RGB(clamp((int)GetRValue(c)+delta), clamp((int)GetGValue(c)+delta), clamp((int)GetBValue(c)+delta));
}

void ApplyThemePalette(ThemeMode mode) {
    g_themeMode = mode;
    switch (mode) {
    case ThemeMode::Light:
        C_BG=RGB(244,246,249); C_SIDEBAR=RGB(235,239,244); C_CARD=RGB(255,255,255);
        C_CARD_ALT=RGB(247,249,252); C_EDIT=RGB(249,250,252); C_LOG=RGB(250,251,253);
        C_BORDER=RGB(206,213,223); C_TEXT=RGB(31,36,43); C_MUTED=RGB(100,110,124);
        C_ACCENT=RGB(55,105,235); C_ACCENT_2=RGB(86,130,240); C_DANGER=RGB(196,66,75);
        C_GREEN=RGB(39,145,93); C_WARNING=RGB(190,132,33); break;
    case ThemeMode::Ocean:
        C_BG=RGB(10,20,28); C_SIDEBAR=RGB(12,25,35); C_CARD=RGB(18,34,45);
        C_CARD_ALT=RGB(15,30,40); C_EDIT=RGB(23,43,56); C_LOG=RGB(8,16,22);
        C_BORDER=RGB(42,70,88); C_TEXT=RGB(235,244,249); C_MUTED=RGB(141,166,179);
        C_ACCENT=RGB(45,165,218); C_ACCENT_2=RGB(88,190,232); C_DANGER=RGB(213,82,91);
        C_GREEN=RGB(53,177,128); C_WARNING=RGB(226,168,71); break;
    case ThemeMode::Emerald:
        C_BG=RGB(13,20,18); C_SIDEBAR=RGB(16,25,22); C_CARD=RGB(22,33,29);
        C_CARD_ALT=RGB(19,29,26); C_EDIT=RGB(29,43,38); C_LOG=RGB(10,15,13);
        C_BORDER=RGB(48,69,61); C_TEXT=RGB(238,244,241); C_MUTED=RGB(148,166,158);
        C_ACCENT=RGB(55,177,116); C_ACCENT_2=RGB(87,197,143); C_DANGER=RGB(210,80,88);
        C_GREEN=RGB(55,177,116); C_WARNING=RGB(224,170,70); break;
    case ThemeMode::Rose:
        C_BG=RGB(24,15,21); C_SIDEBAR=RGB(30,18,26); C_CARD=RGB(38,24,33);
        C_CARD_ALT=RGB(33,21,29); C_EDIT=RGB(48,30,41); C_LOG=RGB(17,10,15);
        C_BORDER=RGB(72,48,63); C_TEXT=RGB(247,238,244); C_MUTED=RGB(176,148,166);
        C_ACCENT=RGB(218,79,154); C_ACCENT_2=RGB(231,111,174); C_DANGER=RGB(220,79,90);
        C_GREEN=RGB(66,170,115); C_WARNING=RGB(226,168,71); break;
    case ThemeMode::Custom:
        // Custom is intentionally a light neutral canvas so the selected accent
        // becomes the visual identity instead of another dark-only theme.
        C_BG=RGB(246,247,250); C_SIDEBAR=RGB(238,241,246); C_CARD=RGB(255,255,255);
        C_CARD_ALT=RGB(248,250,253); C_EDIT=RGB(250,251,253); C_LOG=RGB(247,249,252);
        C_BORDER=RGB(205,212,222); C_TEXT=RGB(31,36,43); C_MUTED=RGB(101,111,125);
        C_ACCENT=g_customAccent; C_ACCENT_2=ShiftColor(g_customAccent,24);
        C_DANGER=RGB(196,66,75); C_GREEN=RGB(39,145,93); C_WARNING=RGB(190,132,33); break;
    case ThemeMode::Dark:
    default:
        C_BG=RGB(15,17,21); C_SIDEBAR=RGB(18,21,26); C_CARD=RGB(24,28,34);
        C_CARD_ALT=RGB(21,24,30); C_EDIT=RGB(31,36,43); C_LOG=RGB(12,14,18);
        C_BORDER=RGB(49,56,66); C_TEXT=RGB(238,241,245); C_MUTED=RGB(151,160,173);
        C_ACCENT=RGB(96,142,255); C_ACCENT_2=RGB(121,162,255); C_DANGER=RGB(205,78,86);
        C_GREEN=RGB(62,166,115); C_WARNING=RGB(221,167,63); break;
    }
    RecreateUiBrushes();
}

bool IsLightTheme() {
    return g_themeMode == ThemeMode::Light || g_themeMode == ThemeMode::Custom;
}

void ApplyDwmTheme(HWND hwnd) {
    BOOL dark = !IsLightTheme();
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ = 20;
    constexpr DWORD DWMWA_BORDER_COLOR_ = 34;
    constexpr DWORD DWMWA_CAPTION_COLOR_ = 35;
    constexpr DWORD DWMWA_TEXT_COLOR_ = 36;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_, &dark, sizeof(dark));

    // On Windows 11 this prevents the system accent color (for example a
    // bright pink title bar) from clashing with the selected SSHLite theme.
    COLORREF caption = C_BG;
    COLORREF text = C_TEXT;
    COLORREF border = C_BORDER;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR_, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR_, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR_, &border, sizeof(border));
}

std::wstring SessionDisplayText(const SessionView& v) {
    std::wstring state = Tr(L"已断开", L"Disconnected");
    if (v.state == STATE_CONNECTED) state = Tr(L"已连接", L"Connected");
    else if (v.state == STATE_CONNECTING) state = Tr(L"连接中", L"Connecting");

    std::wstring endpoint = v.destination.empty()
        ? std::wstring(Tr(L"未知服务器", L"Unknown server"))
        : v.destination;
    endpoint += L" :" + std::to_wstring(v.sshPort);

    std::wstring ports;
    for (size_t i = 0; i < v.localPorts.size(); ++i) {
        if (i) ports += L", ";
        ports += L":" + std::to_wstring(v.localPorts[i]);
    }

    if (!ports.empty()) {
        return L"#" + std::to_wstring(v.id) + L"  " + state +
               L"  |  " + endpoint + L"  |  " +
               Tr(L"本地 ", L"Local ") + ports;
    }

    return L"#" + std::to_wstring(v.id) + L"  " + state +
           L"  |  " + endpoint + L"  |  " +
           Tr(L"SSH 保持连接", L"SSH keepalive");
}

void SetBrowserUrls(HWND hwnd, const std::vector<int>& ports);

void RefreshSessionsList(HWND hwnd) {
    HWND list=GetDlgItem(hwnd,IDC_SESSIONS); if(!list) return;
    int selectedId=0; LRESULT sel=SendMessageW(list,LB_GETCURSEL,0,0);
    if(sel!=LB_ERR && (size_t)sel<g_sessionViews.size()) selectedId=g_sessionViews[(size_t)sel].id;
    SendMessageW(list,LB_RESETCONTENT,0,0);
    int newSel=-1;
    for(size_t i=0;i<g_sessionViews.size();++i){
        std::wstring text=SessionDisplayText(g_sessionViews[i]);
        SendMessageW(list,LB_ADDSTRING,0,(LPARAM)text.c_str());
        if(g_sessionViews[i].id==selectedId) newSel=(int)i;
    }
    if(newSel<0 && !g_sessionViews.empty()) newSel=(int)g_sessionViews.size()-1;
    if(newSel>=0) SendMessageW(list,LB_SETCURSEL,newSel,0);
    bool canStop=false;
    if(newSel>=0){ int st=g_sessionViews[(size_t)newSel].state; canStop=st==STATE_CONNECTED||st==STATE_CONNECTING; }
    EnableWindow(GetDlgItem(hwnd,IDC_DISCONNECT),canStop);
    bool hasPorts = false;
    if (newSel >= 0 && static_cast<size_t>(newSel) < g_sessionViews.size()) {
        const auto& ports = g_sessionViews[static_cast<size_t>(newSel)].localPorts;
        hasPorts = !ports.empty();
        SetBrowserUrls(hwnd, ports);
    } else {
        const auto ports = ExtractLocalPorts(GetWindowTextString(GetDlgItem(hwnd, IDC_COMMAND)));
        hasPorts = !ports.empty();
        SetBrowserUrls(hwnd, ports);
    }
    EnableWindow(GetDlgItem(hwnd, IDC_OPEN_BROWSER), canStop && hasPorts);
    int active=0,connecting=0;
    for(const auto& v:g_sessionViews){ if(v.state==STATE_CONNECTED) ++active; else if(v.state==STATE_CONNECTING) ++connecting; }
    g_connectionState=active>0?STATE_CONNECTED:(connecting>0?STATE_CONNECTING:STATE_DISCONNECTED);
    EnableWindow(GetDlgItem(hwnd, IDC_STOP_ALL), (active + connecting) > 0);
    InvalidateRect(hwnd,nullptr,FALSE);
}

int SelectedSessionId(HWND hwnd){
    HWND list=GetDlgItem(hwnd,IDC_SESSIONS); LRESULT sel=SendMessageW(list,LB_GETCURSEL,0,0);
    if(sel==LB_ERR || (size_t)sel>=g_sessionViews.size()) return 0;
    return g_sessionViews[(size_t)sel].id;
}

void ApplyLanguage(HWND hwnd);
void RefreshNativeThemes(HWND hwnd);
void ChooseCustomAccent(HWND hwnd);

void UpdateLanguageButton(HWND hwnd) {
    HWND c = GetDlgItem(hwnd, IDC_LANGUAGE);
    if (!c) return;
    SetWindowTextW(c, g_language.load() == 0 ? L"中文  ▾" : L"English  ▾");
}

std::wstring ThemeDisplayName(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:   return Tr(L"浅色  ▾", L"Light  ▾");
    case ThemeMode::Ocean:   return Tr(L"海洋蓝  ▾", L"Ocean  ▾");
    case ThemeMode::Emerald: return Tr(L"翡翠绿  ▾", L"Emerald  ▾");
    case ThemeMode::Rose:    return Tr(L"玫瑰  ▾", L"Rose  ▾");
    case ThemeMode::Custom:  return Tr(L"自定义  ▾", L"Custom  ▾");
    case ThemeMode::Dark:
    default:                 return Tr(L"深色  ▾", L"Dark  ▾");
    }
}

void UpdateThemeButton(HWND hwnd) {
    HWND c = GetDlgItem(hwnd, IDC_THEME);
    if (!c) return;
    const auto name = ThemeDisplayName(g_themeMode);
    SetWindowTextW(c, name.c_str());
}

void ShowLanguageMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (g_language.load() == 0 ? MF_CHECKED : 0),
                IDM_LANGUAGE_ZH, L"中文");
    AppendMenuW(menu, MF_STRING | (g_language.load() == 1 ? MF_CHECKED : 0),
                IDM_LANGUAGE_EN, L"English");

    RECT r{};
    GetWindowRect(GetDlgItem(hwnd, IDC_LANGUAGE), &r);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                              r.left, r.bottom + 4, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == IDM_LANGUAGE_ZH || cmd == IDM_LANGUAGE_EN) {
        g_language.store(cmd == IDM_LANGUAGE_EN ? 1 : 0);
        IniWrite(L"UI", L"Language", g_language.load() == 1 ? L"1" : L"0");
        ApplyLanguage(hwnd);
    }
}

void ApplyThemeSelection(HWND hwnd, ThemeMode mode) {
    g_themeMode = mode;
    ApplyThemePalette(g_themeMode);
    ApplyDwmTheme(hwnd);
    RefreshNativeThemes(hwnd);
    IniWrite(L"UI", L"Theme", std::to_wstring(static_cast<int>(mode)));
    UpdateThemeButton(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void ShowThemeMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    struct ThemeMenuItem { UINT id; ThemeMode mode; const wchar_t* zh; const wchar_t* en; };
    ThemeMenuItem items[] = {
        { IDM_THEME_LIGHT,   ThemeMode::Light,   L"浅色",   L"Light" },
        { IDM_THEME_DARK,    ThemeMode::Dark,    L"深色",   L"Dark" },
        { IDM_THEME_OCEAN,   ThemeMode::Ocean,   L"海洋蓝", L"Ocean" },
        { IDM_THEME_EMERALD, ThemeMode::Emerald, L"翡翠绿", L"Emerald" },
        { IDM_THEME_ROSE,    ThemeMode::Rose,    L"玫瑰",   L"Rose" },
        { IDM_THEME_CUSTOM,  ThemeMode::Custom,  L"自定义", L"Custom" },
    };
    for (const auto& item : items) {
        AppendMenuW(menu, MF_STRING | (g_themeMode == item.mode ? MF_CHECKED : 0),
                    item.id, Tr(item.zh, item.en));
    }

    RECT r{};
    GetWindowRect(GetDlgItem(hwnd, IDC_THEME), &r);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                              r.left, r.bottom + 4, 0, hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_THEME_LIGHT:   ApplyThemeSelection(hwnd, ThemeMode::Light); break;
    case IDM_THEME_DARK:    ApplyThemeSelection(hwnd, ThemeMode::Dark); break;
    case IDM_THEME_OCEAN:   ApplyThemeSelection(hwnd, ThemeMode::Ocean); break;
    case IDM_THEME_EMERALD: ApplyThemeSelection(hwnd, ThemeMode::Emerald); break;
    case IDM_THEME_ROSE:    ApplyThemeSelection(hwnd, ThemeMode::Rose); break;
    case IDM_THEME_CUSTOM:
        if (g_themeMode != ThemeMode::Custom) ApplyThemeSelection(hwnd, ThemeMode::Custom);
        ChooseCustomAccent(hwnd);
        break;
    default: break;
    }
}


void ApplyLanguage(HWND hwnd){

    std::wstring title = std::wstring(APP_NAME) + L" " + APP_VERSION +
        Tr(L" — 轻量 SSH 客户端", L" — Lightweight SSH Client");
    SetWindowTextW(hwnd, title.c_str());
    SetWindowTextW(GetDlgItem(hwnd,IDC_NAV_QUICK),Tr(L"快速连接",L"Quick Connect"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_NAV_HISTORY),Tr(L"服务器历史",L"Server History"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_NAV_FAVORITES),Tr(L"隧道收藏",L"Tunnel Favorites"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_CONNECT),Tr(L"新建连接",L"New Connection"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_DISCONNECT),Tr(L"断开选中",L"Disconnect"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_OPEN_BROWSER),Tr(L"打开浏览器",L"Open Browser"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_BROWSEKEY),Tr(L"浏览",L"Browse"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_SAVE_FAVORITE),Tr(L"保存收藏",L"Save Favorite"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_DELETE_FAVORITE),Tr(L"删除",L"Delete"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_CLEAR_LOG),Tr(L"清空日志",L"Clear Log"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_STOP_ALL),Tr(L"全部断开",L"Stop All"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_CUSTOM_COLOR),Tr(L"强调色",L"Accent"));
    SetWindowTextW(GetDlgItem(hwnd,IDC_AUTOBROWSER),Tr(L"连接后自动打开",L"Open automatically after connect"));
    SetWindowTextW(GetDlgItem(hwnd, IDC_AUTHMODE),
                   g_authMode == AuthMode::Password
                       ? Tr(L"密码认证  ▾", L"Password  ▾")
                       : Tr(L"SSH 私钥  ▾", L"SSH Private Key  ▾"));
    SetWindowTextW(GetDlgItem(hwnd, IDC_HISTORY_USE), Tr(L"使用此记录", L"Use Entry"));
    SetWindowTextW(GetDlgItem(hwnd, IDC_HISTORY_DELETE), Tr(L"删除记录", L"Delete"));
    SetWindowTextW(GetDlgItem(hwnd, IDC_HISTORY_CLEAR), Tr(L"清空历史", L"Clear History"));
    SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_USE), Tr(L"使用此收藏", L"Use Favorite"));
    UpdateLanguageButton(hwnd);
    UpdateThemeButton(hwnd);
    RefreshSessionsList(hwnd);
    InvalidateRect(hwnd,nullptr,FALSE);
}

void RefreshNativeThemes(HWND hwnd) {
    const wchar_t* editTheme = IsLightTheme() ? L"Explorer" : L"DarkMode_Explorer";
    const wchar_t* comboTheme = IsLightTheme() ? L"Explorer" : L"DarkMode_Explorer";
    for (int id : {IDC_COMMAND, IDC_PASSWORD, IDC_KEYPATH, IDC_KEYPASS,
                       IDC_FAVORITE_NAME, IDC_FAVORITE_PASSWORD, IDC_LOG}) {
        HWND c = GetDlgItem(hwnd, id); if (c) SetWindowTheme(c, editTheme, nullptr);
    }
    for (int id : {IDC_HISTORY, IDC_FAVORITES, IDC_SESSIONS}) {
        HWND c = GetDlgItem(hwnd, id); if (c) SetWindowTheme(c, comboTheme, nullptr);
    }
}

void ChooseCustomAccent(HWND hwnd){
    static COLORREF custom[16]{}; CHOOSECOLORW cc{}; cc.lStructSize=sizeof(cc); cc.hwndOwner=hwnd;
    cc.lpCustColors=custom; cc.rgbResult=g_customAccent; cc.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(ChooseColorW(&cc)){
        g_customAccent=cc.rgbResult; g_themeMode=ThemeMode::Custom; ApplyThemePalette(g_themeMode); ApplyDwmTheme(hwnd); RefreshNativeThemes(hwnd);
        IniWrite(L"UI",L"Theme",L"5"); IniWrite(L"UI",L"CustomAccent",std::to_wstring((unsigned long)g_customAccent));
        UpdateThemeButton(hwnd);
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void SetControlFont(HWND h, HFONT f) {
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
}

void EnableDarkTitleBar(HWND hwnd) {
    ApplyDwmTheme(hwnd);

    constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
    constexpr int DWMWCP_ROUND = 2;
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_,
                          &corner, sizeof(corner));

    // Windows 11: prefer Mica backdrop. Older systems simply ignore this.
    constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE_ = 38;
    constexpr int DWMSBT_MAINWINDOW = 2;
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE_,
                          &backdrop, sizeof(backdrop));
}

void FillRoundRect(HDC dc, const RECT& r, int radius, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(pen);
    DeleteObject(br);
}

void StrokeRoundRect(HDC dc, const RECT& r, int radius, COLORREF color) {
    HBRUSH hollow = reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBr = SelectObject(dc, hollow);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(pen);
}

void DrawTextAt(HDC dc, const std::wstring& text, const RECT& r,
                HFONT font, COLORREF color, UINT flags) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ old = SelectObject(dc, font);
    RECT copy = r;
    DrawTextW(dc, text.c_str(), -1, &copy, flags);
    SelectObject(dc, old);
}

void DrawInputFrame(HDC dc, HWND hwnd, const RECT& r, int controlId) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    HWND control = GetDlgItem(hwnd, controlId);
    if (!control || !IsWindowVisible(control)) return;

    COLORREF border = C_BORDER;
    HWND focus = GetFocus();
    if (focus == control) border = C_ACCENT;

    FillRoundRect(dc, r, 12, C_EDIT);
    StrokeRoundRect(dc, r, 12, border);
}

void DrawModernButton(const DRAWITEMSTRUCT* dis) {
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;

    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const int id = GetDlgCtrlID(dis->hwndItem);

    if (id == IDC_NAV_QUICK || id == IDC_NAV_HISTORY || id == IDC_NAV_FAVORITES) {
        const bool selected = (g_navSection == id);
        COLORREF bg = selected
            ? (IsLightTheme() ? ShiftColor(C_ACCENT, 120) : ShiftColor(C_SIDEBAR, 15))
            : C_SIDEBAR;
        if (pressed) bg = IsLightTheme() ? ShiftColor(C_ACCENT, 105) : ShiftColor(C_SIDEBAR, 22);

        HBRUSH navBrush = CreateSolidBrush(bg);
        FillRect(dc, &r, navBrush);
        DeleteObject(navBrush);
        if (selected) {
            RECT stripe{r.left, r.top + 8, r.left + 3, r.bottom - 8};
            FillRoundRect(dc, stripe, 3, C_ACCENT);
        }

        wchar_t text[128]{};
        GetWindowTextW(dis->hwndItem, text, 128);
        RECT tr{r.left + 18, r.top, r.right - 10, r.bottom};
        DrawTextAt(dc, text, tr, selected ? g_fontBold : g_font,
                   selected ? C_TEXT : C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const bool headerButton =
        id == IDC_LANGUAGE || id == IDC_THEME || id == IDC_CUSTOM_COLOR;
    HBRUSH parentBrush = CreateSolidBrush(headerButton ? C_BG : C_CARD);
    FillRect(dc, &r, parentBrush);
    DeleteObject(parentBrush);

    COLORREF bg = C_ACCENT;
    COLORREF fg = RGB(255,255,255);
    bool outlineOnly = false;

    if (id == IDC_CONNECT || id == IDC_HISTORY_USE || id == IDC_FAVORITE_USE) {
        bg = C_ACCENT;
    } else if (id == IDC_DISCONNECT || id == IDC_STOP_ALL) {
        bg = C_DANGER;
    } else if (id == IDC_SAVE_FAVORITE) {
        bg = C_GREEN;
    } else if (id == IDC_DELETE_FAVORITE || id == IDC_HISTORY_DELETE || id == IDC_HISTORY_CLEAR) {
        bg = C_CARD;
        fg = C_DANGER;
        outlineOnly = true;
    } else if (id == IDC_OPEN_BROWSER || id == IDC_BROWSEKEY || id == IDC_CLEAR_LOG ||
               id == IDC_AUTHMODE || id == IDC_LOCALURL || id == IDC_LANGUAGE ||
               id == IDC_THEME || id == IDC_CUSTOM_COLOR) {
        bg = C_CARD_ALT;
        fg = C_TEXT;
        outlineOnly = true;
    }

    if (disabled) {
        bg = IsLightTheme() ? RGB(229, 233, 239) : RGB(55, 60, 68);
        fg = IsLightTheme() ? RGB(154, 162, 174) : RGB(132, 138, 148);
        outlineOnly = true;
    } else if (pressed) {
        bg = RGB(
            (std::max)(0, static_cast<int>(GetRValue(bg)) - 18),
            (std::max)(0, static_cast<int>(GetGValue(bg)) - 18),
            (std::max)(0, static_cast<int>(GetBValue(bg)) - 18)
        );
    }

    FillRoundRect(dc, r, 12, bg);
    if (outlineOnly) StrokeRoundRect(dc, r, 12, C_BORDER);

    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);
    DrawTextAt(dc, text, r, g_fontBold, fg,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}


LRESULT CALLBACK ToggleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, BST_UNCHECKED);
        return 0;

    case BM_SETCHECK:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          wParam == BST_CHECKED ? BST_CHECKED : BST_UNCHECKED);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case BM_GETCHECK:
        return GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    case WM_LBUTTONUP: {
        const bool checked =
            GetWindowLongPtrW(hwnd, GWLP_USERDATA) == BST_CHECKED;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          checked ? BST_UNCHECKED : BST_CHECKED);
        InvalidateRect(hwnd, nullptr, TRUE);

        HWND parent = GetParent(hwnd);
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH cardBrush = CreateSolidBrush(C_CARD);
        FillRect(dc, &rc, cardBrush);
        DeleteObject(cardBrush);

        const bool checked =
            GetWindowLongPtrW(hwnd, GWLP_USERDATA) == BST_CHECKED;

        RECT switchRect{0, 4, 42, 26};
        FillRoundRect(dc, switchRect, 22,
                      checked ? C_ACCENT : C_BORDER);

        const int knobX = checked ? 22 : 3;
        RECT knob{knobX, 7, knobX + 16, 23};
        FillRoundRect(dc, knob, 16, RGB(250, 250, 252));

        wchar_t text[128]{};
        GetWindowTextW(hwnd, text, 128);
        RECT tr{52, 0, rc.right, rc.bottom};
        DrawTextAt(dc, text, tr, g_font, C_TEXT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

AuthMode GetAuthMode(HWND) {
    return g_authMode;
}

void SetAuthUi(HWND hwnd, AuthMode mode) {
    g_authMode = mode;
    const bool isPassword = mode == AuthMode::Password;
    const bool quickPage = g_navSection == IDC_NAV_QUICK;

    ShowWindow(GetDlgItem(hwnd, IDC_PASSWORD), quickPage && isPassword ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_KEYPATH), quickPage && !isPassword ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_BROWSEKEY), quickPage && !isPassword ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_KEYPASS), quickPage && !isPassword ? SW_SHOW : SW_HIDE);

    SetWindowTextW(GetDlgItem(hwnd, IDC_AUTHMODE),
                   mode == AuthMode::Password
                       ? Tr(L"密码认证  ▾", L"Password  ▾")
                       : Tr(L"SSH 私钥  ▾", L"SSH Private Key  ▾"));
    InvalidateRect(hwnd, nullptr, FALSE);
}

void SetAuthMode(HWND hwnd, AuthMode mode) {
    SetAuthUi(hwnd, mode);
}

void SetBrowserForwards(HWND hwnd, const std::vector<LocalForwardInfo>& forwards) {
    g_localForwards = forwards;
    g_browserUrls.clear();
    g_browserUrlIndex = 0;

    for (const auto& forward : g_localForwards) {
        g_browserUrls.push_back(forward.BrowserUrl());
    }

    HWND button = GetDlgItem(hwnd, IDC_LOCALURL);
    if (!button) return;

    if (g_localForwards.empty()) {
        SetWindowTextW(button, Tr(L"无本地转发", L"No local forward"));
        return;
    }

    std::wstring text = g_localForwards[0].DisplayText();
    if (g_localForwards.size() > 1) {
        text += L"  ▾";
    }
    SetWindowTextW(button, text.c_str());
}

void SetBrowserUrls(HWND hwnd, const std::vector<int>& ports) {
    // Compatibility path for a selected historical SessionView, which stores
    // local ports but not the original remote -L target.
    std::vector<LocalForwardInfo> forwards;
    for (int port : ports) {
        LocalForwardInfo info;
        info.localPort = port;
        info.remoteHost = L"?";
        info.remotePort = port;
        forwards.push_back(std::move(info));
    }
    SetBrowserForwards(hwnd, forwards);
}

void UpdateBrowserUrl(HWND hwnd) {
    const std::wstring command =
        GetWindowTextString(GetDlgItem(hwnd, IDC_COMMAND));
    SetBrowserForwards(hwnd, ExtractLocalForwards(command));
}

void ShowBrowserUrlMenu(HWND hwnd) {
    if (g_localForwards.size() <= 1) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    for (size_t i = 0; i < g_localForwards.size() && i < 100; ++i) {
        std::wstring label = g_localForwards[i].DisplayText();
        AppendMenuW(
            menu,
            MF_STRING | (static_cast<int>(i) == g_browserUrlIndex ? MF_CHECKED : 0),
            IDM_URL_BASE + static_cast<UINT>(i),
            label.c_str()
        );
    }

    RECT r{};
    GetWindowRect(GetDlgItem(hwnd, IDC_LOCALURL), &r);

    UINT cmd = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        r.left, r.bottom + 4, 0, hwnd, nullptr
    );

    DestroyMenu(menu);

    if (cmd >= IDM_URL_BASE && cmd < IDM_URL_BASE + g_localForwards.size()) {
        g_browserUrlIndex = static_cast<int>(cmd - IDM_URL_BASE);

        std::wstring text =
            g_localForwards[static_cast<size_t>(g_browserUrlIndex)].DisplayText();
        if (g_localForwards.size() > 1) {
            text += L"  ▾";
        }
        SetWindowTextW(GetDlgItem(hwnd, IDC_LOCALURL), text.c_str());
    }
}

void OpenLocalBrowser(HWND hwnd) {
    if (g_browserUrls.empty()) {
        UpdateBrowserUrl(hwnd);
    }
    if (g_browserUrls.empty()) {
        MessageBoxW(hwnd,
                    Tr(L"当前连接没有可打开的本地转发地址。请在 SSH 命令中加入 -L。",
                       L"This connection has no local forwarding URL. Add -L to the SSH command."),
                    Tr(L"没有本地转发", L"No local forward"),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_browserUrlIndex < 0 ||
        static_cast<size_t>(g_browserUrlIndex) >= g_browserUrls.size()) {
        g_browserUrlIndex = 0;
    }

    const std::wstring& url =
        g_browserUrls[static_cast<size_t>(g_browserUrlIndex)];

    ShellExecuteW(hwnd, L"open", url.c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}


void BrowsePrivateKey(HWND hwnd) {
    wchar_t fileName[MAX_PATH]{};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        L"SSH Private Keys (id_*;*.pem;*.key)\0id_*;*.pem;*.key\0"
        L"All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(GetDlgItem(hwnd, IDC_KEYPATH), fileName);
    }
}

void SaveHistoryToIni() {
    constexpr int MAX_HISTORY = 20;
    if (g_history.size() > MAX_HISTORY) g_history.resize(MAX_HISTORY);

    IniWrite(L"History", L"Count", std::to_wstring(g_history.size()));

    for (size_t i = 0; i < g_history.size(); ++i) {
        const std::wstring sec = L"History." + std::to_wstring(i);
        IniWrite(sec, L"Command", g_history[i].command);
        IniWrite(sec, L"Auth",
                 g_history[i].authMode == AuthMode::PrivateKey
                     ? L"key" : L"password");
        IniWrite(sec, L"KeyPath", g_history[i].keyPath);
        IniWrite(sec, L"AutoBrowser",
                 g_history[i].autoBrowser ? L"1" : L"0");
    }
}

void LoadHistoryFromIni() {
    g_history.clear();
    const int count = (std::min)(IniReadInt(L"History", L"Count", 0), 20);

    for (int i = 0; i < count; ++i) {
        const std::wstring sec = L"History." + std::to_wstring(i);
        SavedEntry e;
        e.command = IniRead(sec, L"Command");
        if (e.command.empty()) continue;
        e.authMode = IniRead(sec, L"Auth") == L"key"
            ? AuthMode::PrivateKey
            : AuthMode::Password;
        e.keyPath = IniRead(sec, L"KeyPath");
        e.autoBrowser = IniReadInt(sec, L"AutoBrowser", 1) != 0;
        g_history.push_back(std::move(e));
    }
}

void SaveFavoritesToIni() {
    // Clear all old indexed sections first. This guarantees that deleting or
    // shrinking the Favorites list also removes any old plaintext password
    // values from SSHLite.ini.
    const int oldCount = (std::min)(IniReadInt(L"Favorites", L"Count", 0), 200);
    for (int i = 0; i < oldCount; ++i) {
        IniDeleteSection(L"Favorite." + std::to_wstring(i));
    }

    IniWrite(L"Favorites", L"Count", std::to_wstring(g_favorites.size()));

    for (size_t i = 0; i < g_favorites.size(); ++i) {
        const std::wstring sec = L"Favorite." + std::to_wstring(i);
        IniWrite(sec, L"Name", g_favorites[i].name);
        IniWrite(sec, L"Command", g_favorites[i].command);
        IniWrite(sec, L"Auth",
                 g_favorites[i].authMode == AuthMode::PrivateKey
                     ? L"key" : L"password");
        IniWrite(sec, L"KeyPath", g_favorites[i].keyPath);
        IniWrite(sec, L"PasswordPlain", g_favorites[i].passwordPlain);
        IniWrite(sec, L"AutoBrowser",
                 g_favorites[i].autoBrowser ? L"1" : L"0");
    }
}

void LoadFavoritesFromIni() {
    g_favorites.clear();
    const int count = (std::min)(IniReadInt(L"Favorites", L"Count", 0), 100);

    for (int i = 0; i < count; ++i) {
        const std::wstring sec = L"Favorite." + std::to_wstring(i);
        SavedEntry e;
        e.name = IniRead(sec, L"Name");
        e.command = IniRead(sec, L"Command");
        if (e.name.empty() || e.command.empty()) continue;
        e.authMode = IniRead(sec, L"Auth") == L"key"
            ? AuthMode::PrivateKey
            : AuthMode::Password;
        e.keyPath = IniRead(sec, L"KeyPath");
        e.passwordPlain = IniRead(sec, L"PasswordPlain");
        e.autoBrowser = IniReadInt(sec, L"AutoBrowser", 1) != 0;
        g_favorites.push_back(std::move(e));
    }
}

void RefreshPageActionState(HWND hwnd);
int SelectedFavoriteIndex(HWND hwnd);

void RefreshHistoryCombo(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_HISTORY);
    if (!list) return;
    LRESULT oldSel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& e : g_history) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.command.c_str()));
    }
    if (!g_history.empty()) {
        int sel = oldSel == LB_ERR ? 0 : (std::min)(static_cast<int>(oldSel), static_cast<int>(g_history.size()) - 1);
        SendMessageW(list, LB_SETCURSEL, sel, 0);
    }
    RefreshPageActionState(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void RefreshFavoritesCombo(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FAVORITES);
    if (!list) return;
    LRESULT oldSel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& e : g_favorites) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.name.c_str()));
    }
    if (!g_favorites.empty()) {
        int sel = oldSel == LB_ERR ? 0 : (std::min)(static_cast<int>(oldSel), static_cast<int>(g_favorites.size()) - 1);
        SendMessageW(list, LB_SETCURSEL, sel, 0);
    }
    RefreshPageActionState(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ApplySavedEntry(HWND hwnd, const SavedEntry& e) {
    SetWindowTextW(GetDlgItem(hwnd, IDC_COMMAND), e.command.c_str());
    SetAuthMode(hwnd, e.authMode);
    SetWindowTextW(GetDlgItem(hwnd, IDC_KEYPATH), e.keyPath.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_PASSWORD),
                   e.authMode == AuthMode::Password
                       ? e.passwordPlain.c_str()
                       : L"");
    SendMessageW(
        GetDlgItem(hwnd, IDC_AUTOBROWSER),
        BM_SETCHECK,
        e.autoBrowser ? BST_CHECKED : BST_UNCHECKED,
        0
    );
    UpdateBrowserUrl(hwnd);
}

void AddCurrentToHistory(HWND hwnd) {
    SavedEntry current;
    current.command =
        GetWindowTextString(GetDlgItem(hwnd, IDC_COMMAND));
    current.authMode = GetAuthMode(hwnd);
    current.keyPath =
        GetWindowTextString(GetDlgItem(hwnd, IDC_KEYPATH));
    current.autoBrowser =
        SendMessageW(
            GetDlgItem(hwnd, IDC_AUTOBROWSER),
            BM_GETCHECK, 0, 0
        ) == BST_CHECKED;

    if (current.command.empty()) return;

    g_history.erase(
        std::remove_if(
            g_history.begin(), g_history.end(),
            [&](const SavedEntry& e) {
                return e.command == current.command;
            }
        ),
        g_history.end()
    );

    g_history.insert(g_history.begin(), std::move(current));
    if (g_history.size() > 20) g_history.resize(20);

    SaveHistoryToIni();
    RefreshHistoryCombo(hwnd);
}

void SaveFavorite(HWND hwnd) {
    SavedEntry e;

    const int selectedIndex = SelectedFavoriteIndex(hwnd);
    if (selectedIndex >= 0) {
        e = g_favorites[static_cast<size_t>(selectedIndex)];
    }

    e.name = GetWindowTextString(GetDlgItem(hwnd, IDC_FAVORITE_NAME));
    e.command = GetWindowTextString(GetDlgItem(hwnd, IDC_COMMAND));
    e.authMode = GetAuthMode(hwnd);
    e.keyPath = GetWindowTextString(GetDlgItem(hwnd, IDC_KEYPATH));
    e.autoBrowser =
        SendMessageW(GetDlgItem(hwnd, IDC_AUTOBROWSER),
                     BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (e.name.empty()) {
        MessageBoxW(hwnd,
                    Tr(L"请输入收藏名称。", L"Enter a favorite name."),
                    Tr(L"隧道收藏", L"Tunnel Favorites"),
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (e.command.empty()) {
        MessageBoxW(hwnd,
                    Tr(L"SSH 命令不能为空。", L"SSH command cannot be empty."),
                    Tr(L"隧道收藏", L"Tunnel Favorites"),
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (e.authMode == AuthMode::Password) {
        std::wstring editorPassword =
            GetWindowTextString(GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD));
        if (editorPassword.empty()) {
            editorPassword =
                GetWindowTextString(GetDlgItem(hwnd, IDC_PASSWORD));
        }
        e.passwordPlain = editorPassword;

        if (!e.passwordPlain.empty() && !g_plaintextFavoriteWarningAccepted) {
            const int answer = MessageBoxW(
                hwnd,
                Tr(
                    L"此收藏将把 SSH 密码以明文写入 SSHLite.ini。\n\n"
                    L"任何能读取该文件的人都可以直接看到密码。请不要把 SSHLite.ini 上传到 GitHub、网盘或发送给他人。\n\n"
                    L"确定继续保存明文密码吗？",
                    L"This favorite will store the SSH password in plaintext in SSHLite.ini.\n\n"
                    L"Anyone who can read that file can directly see the password. Do not upload SSHLite.ini to GitHub/cloud storage or share it with others.\n\n"
                    L"Continue and save the plaintext password?"
                ),
                Tr(L"明文密码警告", L"Plaintext Password Warning"),
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
            );
            if (answer != IDYES) return;
            g_plaintextFavoriteWarningAccepted = true;
        }
    } else {
        e.passwordPlain.clear();
    }

    if (selectedIndex >= 0) {
        g_favorites[static_cast<size_t>(selectedIndex)] = e;
    } else {
        auto sameName = std::find_if(
            g_favorites.begin(), g_favorites.end(),
            [&](const SavedEntry& item) { return item.name == e.name; }
        );
        if (sameName != g_favorites.end()) {
            *sameName = e;
        } else {
            g_favorites.push_back(e);
        }
    }

    // Enforce unique favorite names after rename/update.
    for (size_t i = 0; i < g_favorites.size(); ++i) {
        for (size_t j = i + 1; j < g_favorites.size();) {
            if (g_favorites[i].name == g_favorites[j].name) {
                g_favorites.erase(g_favorites.begin() + j);
            } else {
                ++j;
            }
        }
    }

    SaveFavoritesToIni();
    RefreshFavoritesCombo(hwnd);

    for (size_t i = 0; i < g_favorites.size(); ++i) {
        if (g_favorites[i].name == e.name) {
            SendMessageW(GetDlgItem(hwnd, IDC_FAVORITES),
                         LB_SETCURSEL, static_cast<WPARAM>(i), 0);
            SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_NAME),
                           g_favorites[i].name.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD),
                           g_favorites[i].passwordPlain.c_str());
            break;
        }
    }

    RefreshPageActionState(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);

    PostLog(Tr(
        L"[收藏] 收藏已保存/更新。密码认证收藏可以按要求保存明文密码到 SSHLite.ini。\r\n",
        L"[Favorites] Favorite saved/updated. Password-authentication favorites can store a plaintext password in SSHLite.ini.\r\n"
    ));
}

void DeleteFavorite(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_FAVORITES);
    const LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= g_favorites.size()) {
        MessageBoxW(hwnd, Tr(L"请先选择一个收藏。", L"Select a favorite first."),
                    Tr(L"收藏", L"Favorites"), MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring name = g_favorites[static_cast<size_t>(sel)].name;
    std::wstring question = Tr(L"确定删除收藏“", L"Delete favorite \"") + name +
        Tr(L"”吗？", L"\"?");
    if (MessageBoxW(hwnd, question.c_str(), Tr(L"删除收藏", L"Delete Favorite"),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    g_favorites.erase(g_favorites.begin() + static_cast<size_t>(sel));
    SaveFavoritesToIni();
    RefreshFavoritesCombo(hwnd);
    SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_NAME), L"");
    SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD), L"");
}

int SelectedHistoryIndex(HWND hwnd) {
    LRESULT sel = SendMessageW(GetDlgItem(hwnd, IDC_HISTORY), LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= g_history.size()) return -1;
    return static_cast<int>(sel);
}

int SelectedFavoriteIndex(HWND hwnd) {
    LRESULT sel = SendMessageW(GetDlgItem(hwnd, IDC_FAVORITES), LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= g_favorites.size()) return -1;
    return static_cast<int>(sel);
}

void DeleteSelectedHistory(HWND hwnd) {
    const int index = SelectedHistoryIndex(hwnd);
    if (index < 0) {
        MessageBoxW(hwnd, Tr(L"请先选择一条历史记录。", L"Select a history entry first."),
                    Tr(L"服务器历史", L"Server History"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(hwnd,
                    Tr(L"确定删除这条服务器历史记录吗？", L"Delete this server history entry?"),
                    Tr(L"删除历史", L"Delete History"),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    g_history.erase(g_history.begin() + index);
    SaveHistoryToIni();
    RefreshHistoryCombo(hwnd);
}

void ClearHistory(HWND hwnd) {
    if (g_history.empty()) return;
    if (MessageBoxW(hwnd,
                    Tr(L"确定清空全部服务器历史吗？此操作不可撤销。",
                       L"Clear all server history? This cannot be undone."),
                    Tr(L"清空历史", L"Clear History"),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    g_history.clear();
    SaveHistoryToIni();
    RefreshHistoryCombo(hwnd);
}

void RefreshPageActionState(HWND hwnd) {
    const int hi = SelectedHistoryIndex(hwnd);
    EnableWindow(GetDlgItem(hwnd, IDC_HISTORY_USE), hi >= 0);
    EnableWindow(GetDlgItem(hwnd, IDC_HISTORY_DELETE), hi >= 0);
    EnableWindow(GetDlgItem(hwnd, IDC_HISTORY_CLEAR), !g_history.empty());

    const int fi = SelectedFavoriteIndex(hwnd);
    EnableWindow(GetDlgItem(hwnd, IDC_FAVORITE_USE), fi >= 0);
    EnableWindow(GetDlgItem(hwnd, IDC_DELETE_FAVORITE), fi >= 0);
    if (fi >= 0 && g_navSection == IDC_NAV_FAVORITES) {
        const auto& favorite = g_favorites[static_cast<size_t>(fi)];
        SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_NAME),
                       favorite.name.c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD),
                       favorite.passwordPlain.c_str());
    } else if (g_navSection == IDC_NAV_FAVORITES) {
        SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD),
                       GetWindowTextString(GetDlgItem(hwnd, IDC_PASSWORD)).c_str());
    }
}

void ShowPage(HWND hwnd, int page);

void UseSelectedHistory(HWND hwnd) {
    const int index = SelectedHistoryIndex(hwnd);
    if (index < 0) return;
    ApplySavedEntry(hwnd, g_history[static_cast<size_t>(index)]);
    ShowPage(hwnd, IDC_NAV_QUICK);
    SetFocus(GetDlgItem(hwnd, IDC_COMMAND));
}

void UseSelectedFavorite(HWND hwnd) {
    const int index = SelectedFavoriteIndex(hwnd);
    if (index < 0) return;
    ApplySavedEntry(hwnd, g_favorites[static_cast<size_t>(index)]);
    ShowPage(hwnd, IDC_NAV_QUICK);
    SetFocus(GetDlgItem(hwnd, IDC_COMMAND));
}


int ActiveSessionCount() {
    int count = 0;
    for (const auto& v : g_sessionViews) {
        if (v.state == STATE_CONNECTED || v.state == STATE_CONNECTING) ++count;
    }
    return count;
}

bool SessionUsesLocalPort(int port) {
    for (const auto& v : g_sessionViews) {
        if (v.state != STATE_CONNECTED && v.state != STATE_CONNECTING) continue;
        if (std::find(v.localPorts.begin(), v.localPorts.end(), port) != v.localPorts.end())
            return true;
    }
    return false;
}

bool HasGatewayExposure(const std::wstring& command) {
    const auto tokens = TokenizeCommand(command);
    for (const auto& t : tokens) {
        if (t.size() >= 2 && t[0] == L'-' && t.find(L'g', 1) != std::wstring::npos)
            return true;
        if (t.find(L"0.0.0.0:") != std::wstring::npos ||
            t.find(L"*:") == 0)
            return true;
    }
    return false;
}

bool IsDuplicateActiveCommand(const std::wstring& command) {
    for (const auto& v : g_sessionViews) {
        if ((v.state == STATE_CONNECTED || v.state == STATE_CONNECTING) &&
            v.command == command) return true;
    }
    return false;
}

bool ValidateConnectionRequest(HWND hwnd, const std::wstring& command) {
    if (ExtractDestination(command).empty()) {
        MessageBoxW(hwnd,
                    Tr(L"无法识别 SSH 目标服务器。请使用类似 ssh user@host -p 22 的命令。",
                       L"Cannot identify the SSH destination. Use a command such as ssh user@host -p 22."),
                    Tr(L"SSH 命令无效", L"Invalid SSH Command"),
                    MB_OK | MB_ICONWARNING);
        return false;
    }

    if (ActiveSessionCount() >= 16) {
        MessageBoxW(hwnd,
                    Tr(L"当前已有 16 个活动 SSH 会话。为避免误操作和资源占用，请先断开不再使用的连接。",
                       L"There are already 16 active SSH sessions. Disconnect unused sessions before creating more."),
                    Tr(L"会话数量已达上限", L"Session Limit"),
                    MB_OK | MB_ICONWARNING);
        return false;
    }

    const auto ports = ExtractLocalPorts(command);
    std::set<int> uniquePorts;
    for (int port : ports) {
        if (!uniquePorts.insert(port).second) {
            std::wstring text = Tr(L"同一命令中重复使用了本地端口 ",
                                   L"The same command uses local port ") +
                                std::to_wstring(port) +
                                Tr(L"。请删除重复的 -L 规则。", L" more than once. Remove the duplicate -L rule.");
            MessageBoxW(hwnd, text.c_str(), Tr(L"端口重复", L"Duplicate Port"),
                        MB_OK | MB_ICONWARNING);
            return false;
        }
        if (SessionUsesLocalPort(port)) {
            std::wstring text = Tr(L"本地端口 ", L"Local port ") + std::to_wstring(port) +
                Tr(L" 已被 SSHLite 中的其他活动会话使用。请更换端口或先断开原会话。",
                   L" is already used by another active SSHLite session. Choose another port or disconnect the existing session.");
            MessageBoxW(hwnd, text.c_str(), Tr(L"端口冲突", L"Port Conflict"),
                        MB_OK | MB_ICONERROR);
            return false;
        }
        if (TcpPortOpen(port)) {
            std::wstring text = Tr(L"本地端口 ", L"Local port ") + std::to_wstring(port) +
                Tr(L" 当前已经被其他程序监听。继续连接会导致 OpenSSH 转发失败。",
                   L" is already listening in another program. OpenSSH local forwarding would fail.");
            MessageBoxW(hwnd, text.c_str(), Tr(L"端口已占用", L"Port In Use"),
                        MB_OK | MB_ICONERROR);
            return false;
        }
    }

    if (IsDuplicateActiveCommand(command)) {
        if (MessageBoxW(hwnd,
                        Tr(L"已有一个完全相同的 SSH 会话正在运行。\n\n仍然新建另一个相同连接吗？",
                           L"An identical SSH session is already active.\n\nCreate another identical connection anyway?"),
                        Tr(L"重复连接", L"Duplicate Connection"),
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return false;
        }
    }

    if (HasGatewayExposure(command)) {
        if (MessageBoxW(hwnd,
                        Tr(L"此命令包含 -g 或公开绑定地址，可能允许局域网中的其他设备访问本地转发端口。\n\n确认继续吗？",
                           L"This command contains -g or a public bind address, which may expose forwarded ports to other devices on your network.\n\nContinue?"),
                        Tr(L"网络暴露警告", L"Network Exposure Warning"),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return false;
        }
    }

    if (ports.empty() && !g_warnedPlainSession) {
        if (MessageBoxW(hwnd,
                        Tr(L"当前命令没有 -L 本地转发。SSHLite 不是交互式终端，因此会把普通 SSH 连接作为非交互保持连接会话（自动加入 -N）。\n\n如果你只是需要保持 SSH 连接，可以继续；如果要输入 Shell 命令，请使用 Windows Terminal。\n\n继续吗？",
                           L"This command has no -L local forward. SSHLite is not an interactive terminal, so a plain SSH connection is kept as a non-interactive session (with -N added automatically).\n\nContinue for a keepalive session, or use Windows Terminal for an interactive shell?"),
                        Tr(L"非交互 SSH 会话", L"Non-interactive SSH Session"),
                        MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1) != IDYES) {
            return false;
        }
        g_warnedPlainSession = true;
    }

    return true;
}

void TrimLogControl(HWND hLog) {
    if (!hLog) return;
    constexpr int MAX_CHARS = 240000;
    constexpr int TRIM_CHARS = 60000;
    const int len = GetWindowTextLengthW(hLog);
    if (len <= MAX_CHARS) return;

    SendMessageW(hLog, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hLog, EM_SETSEL, 0, TRIM_CHARS);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    SendMessageW(hLog, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hLog, nullptr, FALSE);
}

void MoveEditInsideFrame(HWND hwnd, int id, const RECT& r, int leftPad = 12, int rightPad = 12) {
    HWND c = GetDlgItem(hwnd, id);
    if (!c) return;

    const int h = 28;
    const int frameWidth = static_cast<int>(r.right - r.left);
    const int frameHeight = static_cast<int>(r.bottom - r.top);
    const int x = static_cast<int>(r.left) + leftPad;
    const int y = static_cast<int>(r.top) + (frameHeight - h) / 2;

    int width = frameWidth - leftPad - rightPad;
    if (width < 20) width = 20;

    MoveWindow(c, x, y, width, h, TRUE);
}

void Layout(HWND hwnd);

void ShowControl(HWND hwnd, int id, bool show) {
    HWND c = GetDlgItem(hwnd, id);
    if (c) ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

void ShowPage(HWND hwnd, int page) {
    g_navSection = page;

    const bool quick = page == IDC_NAV_QUICK;
    const bool history = page == IDC_NAV_HISTORY;
    const bool favorites = page == IDC_NAV_FAVORITES;

    for (int id : {
        IDC_COMMAND, IDC_AUTHMODE, IDC_CONNECT, IDC_DISCONNECT,
        IDC_OPEN_BROWSER, IDC_AUTOBROWSER, IDC_LOCALURL, IDC_SESSIONS,
        IDC_STOP_ALL, IDC_LOG, IDC_CLEAR_LOG
    }) ShowControl(hwnd, id, quick);

    ShowControl(hwnd, IDC_PASSWORD, quick && g_authMode == AuthMode::Password);
    ShowControl(hwnd, IDC_KEYPATH, quick && g_authMode == AuthMode::PrivateKey);
    ShowControl(hwnd, IDC_BROWSEKEY, quick && g_authMode == AuthMode::PrivateKey);
    ShowControl(hwnd, IDC_KEYPASS, quick && g_authMode == AuthMode::PrivateKey);

    ShowControl(hwnd, IDC_HISTORY, history);
    ShowControl(hwnd, IDC_HISTORY_USE, history);
    ShowControl(hwnd, IDC_HISTORY_DELETE, history);
    ShowControl(hwnd, IDC_HISTORY_CLEAR, history);

    ShowControl(hwnd, IDC_FAVORITES, favorites);
    ShowControl(hwnd, IDC_FAVORITE_NAME, favorites);
    ShowControl(hwnd, IDC_SAVE_FAVORITE, favorites);
    ShowControl(hwnd, IDC_DELETE_FAVORITE, favorites);
    ShowControl(hwnd, IDC_FAVORITE_USE, favorites);
    ShowControl(hwnd, IDC_FAVORITE_PASSWORD, favorites);

    RefreshPageActionState(hwnd);
    Layout(hwnd);

    // Owner-draw navigation buttons are child windows. Repaint them
    // explicitly when the active page changes, otherwise the previous
    // selected background can remain visible until another child repaint.
    for (int navId : {IDC_NAV_QUICK, IDC_NAV_HISTORY, IDC_NAV_FAVORITES}) {
        HWND nav = GetDlgItem(hwnd, navId);
        if (nav) {
            RedrawWindow(nav, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

void Layout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int w = rc.right;
    const int h = rc.bottom;
    const int sidebarW = 210;
    const int outer = 24;
    const int gap = 18;
    const int mainX = sidebarW + 28;
    const int mainW = w - mainX - outer;
    const int top = 92;

    MoveWindow(GetDlgItem(hwnd, IDC_NAV_QUICK), 14, 116, sidebarW - 28, 42, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_NAV_HISTORY), 14, 164, sidebarW - 28, 42, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_NAV_FAVORITES), 14, 212, sidebarW - 28, 42, TRUE);

    // Header toolbar uses a reserved status column. This fixes the previous
    // overlap between Accent and the status pill at 1180-1300px widths.
    const int statusW = 132;
    const int statusRight = w - outer;
    const int statusLeft = statusRight - statusW;
    g_headerStatus = { statusLeft, 29, statusRight, 65 };

    const int toolGap = 10;
    const int customW = 92;
    const int themeW = 132;
    const int langW = 116;
    const int customX = statusLeft - toolGap - customW;
    const int themeX = customX - toolGap - themeW;
    const int langX = themeX - toolGap - langW;

    MoveWindow(GetDlgItem(hwnd, IDC_LANGUAGE), langX, 28, langW, 36, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_THEME), themeX, 28, themeW, 36, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CUSTOM_COLOR), customX, 28, customW, 36, TRUE);

    if (g_navSection == IDC_NAV_QUICK) {
        const int rightW = (std::max)(340, mainW * 36 / 100);
        const int leftW = mainW - rightW - gap;

        g_cardConnection = { mainX, top, mainX + leftW, top + 310 };
        g_cardForward = { mainX, g_cardConnection.bottom + 18,
                          mainX + leftW, g_cardConnection.bottom + 172 };
        g_cardTips = { mainX, g_cardForward.bottom + 18,
                       mainX + leftW, h - outer };
        g_cardSessions = { mainX + leftW + gap, top, w - outer, top + 252 };
        g_cardLog = { mainX + leftW + gap, g_cardSessions.bottom + 18,
                      w - outer, h - outer };
        g_cardFavorites = {};
        g_cardPageList = {};
        g_cardPageDetail = {};

        const int cx = g_cardConnection.left + 18;
        const int cr = g_cardConnection.right - 18;
        const int fullW = cr - cx;

        g_frameCommand = { cx, g_cardConnection.top + 76, cr,
                           g_cardConnection.top + 118 };

        g_frameAuth = { cx, g_cardConnection.top + 150,
                        cx + 190, g_cardConnection.top + 192 };

        const int credLeft = g_frameAuth.right + 12;
        g_framePassword = { credLeft, g_cardConnection.top + 150,
                            cr, g_cardConnection.top + 192 };

        const int keyBrowseW = 76;
        const int keyPassW = (std::max)(150, fullW * 28 / 100);
        const int keyPathRight = cr - keyPassW - keyBrowseW - 24;
        g_frameKeyPath = { credLeft, g_cardConnection.top + 150,
                           keyPathRight, g_cardConnection.top + 192 };
        g_frameKeyPass = { cr - keyPassW, g_cardConnection.top + 222,
                           cr, g_cardConnection.top + 264 };

        MoveEditInsideFrame(hwnd, IDC_COMMAND, g_frameCommand);
        MoveWindow(GetDlgItem(hwnd, IDC_AUTHMODE),
                   g_frameAuth.left, g_frameAuth.top,
                   g_frameAuth.right - g_frameAuth.left,
                   g_frameAuth.bottom - g_frameAuth.top, TRUE);
        MoveEditInsideFrame(hwnd, IDC_PASSWORD, g_framePassword);
        MoveEditInsideFrame(hwnd, IDC_KEYPATH, g_frameKeyPath);
        MoveWindow(GetDlgItem(hwnd, IDC_BROWSEKEY),
                   keyPathRight + 12, g_cardConnection.top + 152,
                   keyBrowseW, 38, TRUE);
        MoveEditInsideFrame(hwnd, IDC_KEYPASS, g_frameKeyPass);

        const int buttonY = g_cardConnection.bottom - 48;
        MoveWindow(GetDlgItem(hwnd, IDC_CONNECT), cx, buttonY, 126, 40, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DISCONNECT), cx + 138, buttonY, 126, 40, TRUE);

        const int fx = g_cardForward.left + 18;
        const int fr = g_cardForward.right - 18;
        g_frameUrl = { fx, g_cardForward.top + 62,
                       fr - 142, g_cardForward.top + 106 };
        MoveWindow(GetDlgItem(hwnd, IDC_LOCALURL),
                   g_frameUrl.left, g_frameUrl.top,
                   g_frameUrl.right - g_frameUrl.left,
                   g_frameUrl.bottom - g_frameUrl.top, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_OPEN_BROWSER),
                   fr - 130, g_cardForward.top + 64, 130, 40, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_AUTOBROWSER),
                   fx, g_cardForward.top + 112, 220, 30, TRUE);

        const int sx = g_cardSessions.left + 16;
        const int sr = g_cardSessions.right - 16;
        MoveWindow(GetDlgItem(hwnd, IDC_STOP_ALL),
                   sr - 104, g_cardSessions.top + 16, 104, 36, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_SESSIONS),
                   sx, g_cardSessions.top + 62,
                   sr - sx, g_cardSessions.bottom - g_cardSessions.top - 78, TRUE);

        const int logX = g_cardLog.left + 16;
        const int logR = g_cardLog.right - 16;
        MoveWindow(GetDlgItem(hwnd, IDC_CLEAR_LOG),
                   logR - 96, g_cardLog.top + 16, 96, 36, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LOG),
                   logX, g_cardLog.top + 64,
                   logR - logX, g_cardLog.bottom - g_cardLog.top - 80, TRUE);
    } else {
        // Separate, full pages for History and Favorites.
        const int listW = (std::max)(390, mainW * 46 / 100);
        g_cardPageList = { mainX, top, mainX + listW, h - outer };
        g_cardPageDetail = { g_cardPageList.right + gap, top,
                             w - outer, h - outer };
        g_cardConnection = {};
        g_cardForward = {};
        g_cardFavorites = {};
        g_cardSessions = {};
        g_cardLog = {};
        g_cardTips = {};

        const int lx = g_cardPageList.left + 16;
        const int lr = g_cardPageList.right - 16;
        const int dx = g_cardPageDetail.left + 18;
        const int dr = g_cardPageDetail.right - 18;

        if (g_navSection == IDC_NAV_HISTORY) {
            MoveWindow(GetDlgItem(hwnd, IDC_HISTORY),
                       lx, g_cardPageList.top + 64,
                       lr - lx, g_cardPageList.bottom - g_cardPageList.top - 82, TRUE);

            MoveWindow(GetDlgItem(hwnd, IDC_HISTORY_USE),
                       dx, g_cardPageDetail.bottom - 54, 128, 40, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_HISTORY_DELETE),
                       dx + 140, g_cardPageDetail.bottom - 54, 116, 40, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_HISTORY_CLEAR),
                       dr - 126, g_cardPageDetail.bottom - 54, 126, 40, TRUE);
        } else {
            MoveWindow(GetDlgItem(hwnd, IDC_FAVORITES),
                       lx, g_cardPageList.top + 64,
                       lr - lx, g_cardPageList.bottom - g_cardPageList.top - 82, TRUE);

            g_frameFavName = { dx, g_cardPageDetail.top + 84,
                               dr, g_cardPageDetail.top + 128 };
            MoveEditInsideFrame(hwnd, IDC_FAVORITE_NAME, g_frameFavName);

            g_frameFavPassword = { dx, g_cardPageDetail.top + 154,
                                   dr, g_cardPageDetail.top + 198 };
            MoveEditInsideFrame(hwnd, IDC_FAVORITE_PASSWORD, g_frameFavPassword);

            MoveWindow(GetDlgItem(hwnd, IDC_FAVORITE_USE),
                       dx, g_cardPageDetail.bottom - 54, 128, 40, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_SAVE_FAVORITE),
                       dx + 140, g_cardPageDetail.bottom - 54, 132, 40, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_DELETE_FAVORITE),
                       dr - 112, g_cardPageDetail.bottom - 54, 112, 40, TRUE);
        }
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_language.store(IniReadInt(L"UI", L"Language", 0) == 1 ? 1 : 0);
        int savedTheme = IniReadInt(L"UI", L"Theme", 1);
        if (savedTheme < 0 || savedTheme > 5) savedTheme = 0;
        g_themeMode = static_cast<ThemeMode>(savedTheme);
        g_customAccent = static_cast<COLORREF>(std::wcstoul(
            IniRead(L"UI", L"CustomAccent", std::to_wstring((unsigned long)RGB(96,142,255))).c_str(),
            nullptr, 10));
        ApplyThemePalette(g_themeMode);
        EnableDarkTitleBar(hwnd);

        g_font = CreateFontW(
            -17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"
        );
        g_fontBold = CreateFontW(
            -17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"
        );
        g_fontTitle = CreateFontW(
            -28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"
        );
        g_fontSmall = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"
        );
        g_fontMono = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH, L"Microsoft YaHei UI"
        );

        HWND hNavQuick = CreateWindowExW(
            0, L"BUTTON", Tr(L"快速连接", L"Quick Connect"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            14, 116, 182, 42, hwnd,
            ControlIdToMenu(IDC_NAV_QUICK), g_hInst, nullptr
        );
        HWND hNavHistory = CreateWindowExW(
            0, L"BUTTON", Tr(L"服务器历史", L"Server History"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            14, 164, 182, 42, hwnd,
            ControlIdToMenu(IDC_NAV_HISTORY), g_hInst, nullptr
        );
        HWND hNavFavorites = CreateWindowExW(
            0, L"BUTTON", Tr(L"隧道收藏", L"Tunnel Favorites"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            14, 212, 182, 42, hwnd,
            ControlIdToMenu(IDC_NAV_FAVORITES), g_hInst, nullptr
        );

        HWND hHistory = CreateWindowExW(
            0, L"LISTBOX", L"",
            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            0, 0, 100, 240,
            hwnd, ControlIdToMenu(IDC_HISTORY), g_hInst, nullptr
        );

        HWND hCommand = CreateWindowExW(
            0, L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 28,
            hwnd, ControlIdToMenu(IDC_COMMAND), g_hInst, nullptr
        );

        HWND hAuth = CreateWindowExW(
            0, L"BUTTON", Tr(L"密码认证  ▾", L"Password  ▾"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 42,
            hwnd, ControlIdToMenu(IDC_AUTHMODE), g_hInst, nullptr
        );

        HWND hPassword = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
            0, 0, 100, 28,
            hwnd, ControlIdToMenu(IDC_PASSWORD), g_hInst, nullptr
        );

        HWND hKeyPath = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 100, 28,
            hwnd, ControlIdToMenu(IDC_KEYPATH), g_hInst, nullptr
        );

        HWND hBrowse = CreateWindowExW(
            0, L"BUTTON", Tr(L"浏览", L"Browse"),
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 74, 38,
            hwnd, ControlIdToMenu(IDC_BROWSEKEY), g_hInst, nullptr
        );

        HWND hKeyPass = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL,
            0, 0, 100, 28,
            hwnd, ControlIdToMenu(IDC_KEYPASS), g_hInst, nullptr
        );

        HWND hConnect = CreateWindowExW(
            0, L"BUTTON", Tr(L"新建连接", L"New Connection"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 116, 38,
            hwnd, ControlIdToMenu(IDC_CONNECT), g_hInst, nullptr
        );

        HWND hDisconnect = CreateWindowExW(
            0, L"BUTTON", Tr(L"断开选中", L"Disconnect"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 116, 38,
            hwnd, ControlIdToMenu(IDC_DISCONNECT), g_hInst, nullptr
        );

        HWND hBrowser = CreateWindowExW(
            0, L"BUTTON", Tr(L"打开浏览器", L"Open Browser"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 124, 38,
            hwnd, ControlIdToMenu(IDC_OPEN_BROWSER), g_hInst, nullptr
        );

        HWND hAuto = CreateWindowExW(
            0, L"SSHLiteModernToggle", Tr(L"连接后自动打开", L"Open automatically after connect"),
            WS_CHILD | WS_VISIBLE,
            0, 0, 196, 30,
            hwnd, ControlIdToMenu(IDC_AUTOBROWSER), g_hInst, nullptr
        );
        SendMessageW(hAuto, BM_SETCHECK, BST_CHECKED, 0);

        HWND hUrl = CreateWindowExW(
            0, L"BUTTON", Tr(L"无本地转发", L"No local forward"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 42,
            hwnd, ControlIdToMenu(IDC_LOCALURL), g_hInst, nullptr
        );

        HWND hFavorites = CreateWindowExW(
            0, L"LISTBOX", L"",
            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            0, 0, 100, 240,
            hwnd, ControlIdToMenu(IDC_FAVORITES), g_hInst, nullptr
        );

        HWND hFavName = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 28,
            hwnd, ControlIdToMenu(IDC_FAVORITE_NAME), g_hInst, nullptr
        );

        // Intentionally visible plaintext on the Favorites page because the
        // requested feature is explicit plaintext password storage.
        HWND hFavPassword = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 100, 28,
            hwnd, ControlIdToMenu(IDC_FAVORITE_PASSWORD), g_hInst, nullptr
        );

        HWND hSaveFav = CreateWindowExW(
            0, L"BUTTON", Tr(L"保存收藏", L"Save Favorite"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 118, 38,
            hwnd, ControlIdToMenu(IDC_SAVE_FAVORITE), g_hInst, nullptr
        );

        HWND hDelFav = CreateWindowExW(
            0, L"BUTTON", Tr(L"删除", L"Delete"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 118, 38,
            hwnd, ControlIdToMenu(IDC_DELETE_FAVORITE), g_hInst, nullptr
        );

        HWND hClear = CreateWindowExW(
            0, L"BUTTON", Tr(L"清空日志", L"Clear Log"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 92, 34,
            hwnd, ControlIdToMenu(IDC_CLEAR_LOG), g_hInst, nullptr
        );

        HWND hLanguage = CreateWindowExW(
            0, L"BUTTON", L"中文  ▾",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 116, 36, hwnd, ControlIdToMenu(IDC_LANGUAGE), g_hInst, nullptr);

        HWND hTheme = CreateWindowExW(
            0, L"BUTTON", Tr(L"浅色  ▾", L"Light  ▾"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 132, 36, hwnd, ControlIdToMenu(IDC_THEME), g_hInst, nullptr);

        HWND hCustomColor = CreateWindowExW(
            0, L"BUTTON", Tr(L"强调色", L"Accent"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 88, 34, hwnd, ControlIdToMenu(IDC_CUSTOM_COLOR), g_hInst, nullptr);

        HWND hSessions = CreateWindowExW(
            0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            0, 0, 100, 120, hwnd, ControlIdToMenu(IDC_SESSIONS), g_hInst, nullptr);

        HWND hStopAll = CreateWindowExW(
            0, L"BUTTON", Tr(L"全部断开", L"Stop All"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 96, 34, hwnd, ControlIdToMenu(IDC_STOP_ALL), g_hInst, nullptr);

        HWND hHistoryUse = CreateWindowExW(
            0, L"BUTTON", Tr(L"使用此记录", L"Use Entry"),
            WS_CHILD | BS_OWNERDRAW, 0, 0, 128, 40,
            hwnd, ControlIdToMenu(IDC_HISTORY_USE), g_hInst, nullptr);
        HWND hHistoryDelete = CreateWindowExW(
            0, L"BUTTON", Tr(L"删除记录", L"Delete"),
            WS_CHILD | BS_OWNERDRAW, 0, 0, 116, 40,
            hwnd, ControlIdToMenu(IDC_HISTORY_DELETE), g_hInst, nullptr);
        HWND hHistoryClear = CreateWindowExW(
            0, L"BUTTON", Tr(L"清空历史", L"Clear History"),
            WS_CHILD | BS_OWNERDRAW, 0, 0, 126, 40,
            hwnd, ControlIdToMenu(IDC_HISTORY_CLEAR), g_hInst, nullptr);
        HWND hFavoriteUse = CreateWindowExW(
            0, L"BUTTON", Tr(L"使用此收藏", L"Use Favorite"),
            WS_CHILD | BS_OWNERDRAW, 0, 0, 128, 40,
            hwnd, ControlIdToMenu(IDC_FAVORITE_USE), g_hInst, nullptr);

        HWND hLog = CreateWindowExW(
            0, L"EDIT",
            Tr(L"[SSHLite] Windows OpenSSH 后端已就绪。\r\n[安全] 快速连接/历史不会保存密码；隧道收藏可按用户要求保存明文密码。\r\n[提示] 粘贴 SSH 命令后即可建立本地隧道。\r\n\r\n",
               L"[SSHLite] Windows OpenSSH backend is ready.\r\n[Security] Quick Connect/History do not save passwords; Tunnel Favorites may store a plaintext password when requested.\r\n[Hint] Paste an SSH command and create a connection.\r\n\r\n"),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 100, 100,
            hwnd, ControlIdToMenu(IDC_LOG), g_hInst, nullptr
        );

        for (HWND c : {
            hNavQuick, hNavHistory, hNavFavorites,
            hHistory, hCommand, hAuth, hPassword, hKeyPath, hBrowse, hKeyPass,
            hConnect, hDisconnect, hBrowser, hAuto, hUrl,
            hFavorites, hFavName, hFavPassword, hSaveFav, hDelFav, hClear,
            hLanguage, hTheme, hCustomColor, hSessions, hStopAll,
            hHistoryUse, hHistoryDelete, hHistoryClear, hFavoriteUse
        }) {
            SetControlFont(c, g_font);
        }
        SetControlFont(hLog, g_fontMono);

        for (HWND edit : {hCommand, hPassword, hKeyPath, hKeyPass,
                            hFavName, hFavPassword}) {
            SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(0, 0));
            SetWindowTheme(edit, L"DarkMode_Explorer", nullptr);
        }

        SetWindowTheme(hLog, L"DarkMode_Explorer", nullptr);
        RefreshNativeThemes(hwnd);
        SendMessageW(hSessions, LB_SETITEMHEIGHT, 0, 52);
        SendMessageW(hHistory, LB_SETITEMHEIGHT, 0, 64);
        SendMessageW(hFavorites, LB_SETITEMHEIGHT, 0, 70);
        UpdateLanguageButton(hwnd);
        UpdateThemeButton(hwnd);

        EnableWindow(hDisconnect, FALSE);
        EnableWindow(hBrowser, FALSE);
        SetAuthUi(hwnd, AuthMode::Password);

        LoadHistoryFromIni();
        LoadFavoritesFromIni();
        RefreshHistoryCombo(hwnd);
        RefreshFavoritesCombo(hwnd);

        UpdateBrowserUrl(hwnd);
        ApplyLanguage(hwnd);
        ShowPage(hwnd, IDC_NAV_QUICK);
        return 0;
    }

    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_COMMAND && code == EN_CHANGE) {
            UpdateBrowserUrl(hwnd);
            return 0;
        }

        if ((id == IDC_COMMAND || id == IDC_PASSWORD ||
             id == IDC_KEYPATH || id == IDC_KEYPASS ||
             id == IDC_FAVORITE_NAME || id == IDC_FAVORITE_PASSWORD) &&
            (code == EN_SETFOCUS || code == EN_KILLFOCUS)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (id == IDC_LANGUAGE) {
            ShowLanguageMenu(hwnd);
            return 0;
        }

        if (id == IDC_THEME) {
            ShowThemeMenu(hwnd);
            return 0;
        }

        if (id == IDC_CUSTOM_COLOR) {
            ChooseCustomAccent(hwnd);
            return 0;
        }

        if (id == IDC_SESSIONS && code == LBN_SELCHANGE) {
            RefreshSessionsList(hwnd);
            return 0;
        }
        if (id == IDC_SESSIONS && code == LBN_DBLCLK) {
            OpenLocalBrowser(hwnd);
            return 0;
        }

        if (id == IDC_STOP_ALL) {
            const int active = ActiveSessionCount();
            if (active > 0 &&
                MessageBoxW(hwnd,
                    Tr(L"确定断开全部活动 SSH 会话吗？正在使用的隧道会立即中断。",
                       L"Disconnect all active SSH sessions? Any tunnels currently in use will be interrupted immediately."),
                    Tr(L"全部断开", L"Stop All Sessions"),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                std::thread([] { g_sessionManager.StopAll(); }).detach();
            }
            return 0;
        }

        if (id == IDC_NAV_QUICK) {
            ShowPage(hwnd, IDC_NAV_QUICK);
            SetFocus(GetDlgItem(hwnd, IDC_COMMAND));
            return 0;
        }

        if (id == IDC_NAV_HISTORY) {
            ShowPage(hwnd, IDC_NAV_HISTORY);
            SetFocus(GetDlgItem(hwnd, IDC_HISTORY));
            return 0;
        }

        if (id == IDC_NAV_FAVORITES) {
            ShowPage(hwnd, IDC_NAV_FAVORITES);
            SetFocus(GetDlgItem(hwnd, IDC_FAVORITES));
            return 0;
        }

        if (id == IDC_CLEAR_LOG) {
            SetWindowTextW(GetDlgItem(hwnd, IDC_LOG), L"");
            return 0;
        }

        if (id == IDC_AUTHMODE) {
            SetAuthMode(hwnd, g_authMode == AuthMode::Password
                                 ? AuthMode::PrivateKey
                                 : AuthMode::Password);
            return 0;
        }

        if (id == IDC_LOCALURL) {
            ShowBrowserUrlMenu(hwnd);
            return 0;
        }

        if (id == IDC_HISTORY && code == LBN_SELCHANGE) {
            RefreshPageActionState(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (id == IDC_HISTORY && code == LBN_DBLCLK) {
            UseSelectedHistory(hwnd);
            return 0;
        }

        if (id == IDC_FAVORITES && code == LBN_SELCHANGE) {
            const int index = SelectedFavoriteIndex(hwnd);
            if (index >= 0) {
                const auto& favorite = g_favorites[static_cast<size_t>(index)];
                SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_NAME),
                               favorite.name.c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD),
                               favorite.passwordPlain.c_str());
                // Synchronize the hidden Quick Connect model so Save Favorite
                // updates exactly the selected profile rather than an unrelated
                // previous connection.
                ApplySavedEntry(hwnd, favorite);
            }
            RefreshPageActionState(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (id == IDC_FAVORITES && code == LBN_DBLCLK) {
            UseSelectedFavorite(hwnd);
            return 0;
        }

        if (id == IDC_HISTORY_USE) {
            UseSelectedHistory(hwnd);
            return 0;
        }

        if (id == IDC_HISTORY_DELETE) {
            DeleteSelectedHistory(hwnd);
            return 0;
        }

        if (id == IDC_HISTORY_CLEAR) {
            ClearHistory(hwnd);
            return 0;
        }

        if (id == IDC_FAVORITE_USE) {
            UseSelectedFavorite(hwnd);
            return 0;
        }

        if (id == IDC_BROWSEKEY) {
            BrowsePrivateKey(hwnd);
            return 0;
        }

        if (id == IDC_SAVE_FAVORITE) {
            SaveFavorite(hwnd);
            return 0;
        }

        if (id == IDC_DELETE_FAVORITE) {
            DeleteFavorite(hwnd);
            return 0;
        }

        if (id == IDC_CONNECT) {
            const std::wstring sshExe = FindSshExe();
            if (sshExe.empty()) {
                MessageBoxW(
                    hwnd,
                    Tr(L"未找到 Windows OpenSSH Client (ssh.exe)。\n\n请在 Windows 设置 → 可选功能中安装 OpenSSH Client。",
                       L"Windows OpenSSH Client (ssh.exe) was not found.\n\nInstall OpenSSH Client from Windows Optional Features."),
                    Tr(L"缺少 OpenSSH Client", L"OpenSSH Client required"),
                    MB_ICONERROR
                );
                return 0;
            }

            const std::wstring command =
                GetWindowTextString(GetDlgItem(hwnd, IDC_COMMAND));

            if (command.empty()) {
                MessageBoxW(hwnd, Tr(L"请输入 SSH 命令。", L"Enter an SSH command."),
                            L"SSH", MB_ICONWARNING);
                return 0;
            }

            const AuthMode authMode = GetAuthMode(hwnd);
            std::wstring password =
                GetWindowTextString(GetDlgItem(hwnd, IDC_PASSWORD));
            const std::wstring keyPath =
                GetWindowTextString(GetDlgItem(hwnd, IDC_KEYPATH));
            std::wstring keyPass =
                GetWindowTextString(GetDlgItem(hwnd, IDC_KEYPASS));

            if (authMode == AuthMode::Password && password.empty()) {
                MessageBoxW(hwnd, Tr(L"请输入 SSH 密码。", L"Enter the SSH password."),
                            Tr(L"认证", L"Authentication"), MB_ICONWARNING);
                return 0;
            }

            if (authMode == AuthMode::PrivateKey) {
                if (keyPath.empty()) {
                    MessageBoxW(hwnd, Tr(L"请选择 SSH 私钥文件。", L"Select an SSH private key file."),
                                Tr(L"认证", L"Authentication"), MB_ICONWARNING);
                    return 0;
                }

                const DWORD attr = GetFileAttributesW(keyPath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES ||
                    (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    MessageBoxW(hwnd, Tr(L"私钥文件不存在。", L"The private key file does not exist."),
                                Tr(L"认证", L"Authentication"), MB_ICONERROR);
                    return 0;
                }
            }

            if (!ValidateConnectionRequest(hwnd, command)) {
                return 0;
            }

            AddCurrentToHistory(hwnd);

            PostLog(L"\r\n");
            const int sessionId = g_sessionManager.Start(
                command, authMode, password, keyPath, keyPass
            );

            // Do not keep secrets in the UI longer than necessary.
            if (!password.empty()) {
                SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
                password.clear();
                SetWindowTextW(GetDlgItem(hwnd, IDC_PASSWORD), L"");
            }
            if (!keyPass.empty()) {
                SecureZeroMemory(keyPass.data(), keyPass.size() * sizeof(wchar_t));
                keyPass.clear();
                SetWindowTextW(GetDlgItem(hwnd, IDC_KEYPASS), L"");
            }

            SessionView view;
            view.id = sessionId;
            view.state = STATE_CONNECTING;
            view.command = command;
            view.destination = ExtractDestination(command);
            view.sshPort = ExtractSshPort(command);
            view.localPorts = ExtractLocalPorts(command);

            // Keep the UI history bounded. Active sessions are never removed here.
            if (g_sessionViews.size() >= 64) {
                g_sessionViews.erase(
                    std::remove_if(g_sessionViews.begin(), g_sessionViews.end(),
                        [](const SessionView& item) {
                            return item.state == STATE_DISCONNECTED;
                        }),
                    g_sessionViews.end());
                if (g_sessionViews.size() > 48) {
                    g_sessionViews.erase(g_sessionViews.begin(),
                                         g_sessionViews.begin() + (g_sessionViews.size() - 48));
                }
            }

            g_sessionViews.push_back(std::move(view));
            RefreshSessionsList(hwnd);
            return 0;
        }

        if (id == IDC_DISCONNECT) {
            const int sessionId = SelectedSessionId(hwnd);
            if (sessionId > 0) {
                EnableWindow(GetDlgItem(hwnd, IDC_DISCONNECT), FALSE);
                std::thread([sessionId] { g_sessionManager.Stop(sessionId); }).detach();
            }
            return 0;
        }

        if (id == IDC_OPEN_BROWSER) {
            OpenLocalBrowser(hwnd);
            return 0;
        }

        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC windowDc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        HDC dc = CreateCompatibleDC(windowDc);
        HBITMAP backBitmap = CreateCompatibleBitmap(
            windowDc,
            (std::max)(1, static_cast<int>(rc.right - rc.left)),
            (std::max)(1, static_cast<int>(rc.bottom - rc.top))
        );
        HGDIOBJ oldBitmap = SelectObject(dc, backBitmap);

        FillRect(dc, &rc, g_brushBg);

        // Sidebar.
        RECT sidebar{0, 0, 210, rc.bottom};
        HBRUSH sideBrush = CreateSolidBrush(C_SIDEBAR);
        FillRect(dc, &sidebar, sideBrush);
        DeleteObject(sideBrush);

        HPEN divider = CreatePen(PS_SOLID, 1, C_BORDER);
        HGDIOBJ oldPen = SelectObject(dc, divider);
        MoveToEx(dc, 209, 0, nullptr);
        LineTo(dc, 209, rc.bottom);
        SelectObject(dc, oldPen);
        DeleteObject(divider);

        RECT mark{20, 24, 58, 62};
        FillRoundRect(dc, mark, 12, C_ACCENT);
        DrawTextAt(dc, L"S", mark, g_fontBold, RGB(255,255,255),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT appTitle{70, 21, 190, 47};
        DrawTextAt(dc, L"SSHLite", appTitle, g_fontBold, C_TEXT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT appSub{70, 46, 198, 68};
        std::wstring productSub = std::wstring(L"OpenSSH  •  v") + APP_VERSION;
        DrawTextAt(dc, productSub, appSub, g_fontSmall, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT navLabel{20, 88, 190, 108};
        DrawTextAt(dc, Tr(L"工作区", L"WORKSPACE"), navLabel, g_fontSmall, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT foot1{20, rc.bottom - 68, 190, rc.bottom - 46};
        DrawTextAt(dc, Tr(L"OpenSSH 后端", L"OpenSSH backend"), foot1,
                   g_fontSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT foot2{20, rc.bottom - 44, 190, rc.bottom - 22};
        DrawTextAt(dc, FindSshExe().empty() ? Tr(L"未安装", L"Not installed")
                                            : Tr(L"就绪", L"Ready"),
                   foot2, g_fontBold,
                   FindSshExe().empty() ? C_DANGER : C_GREEN,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Page header.
        const wchar_t* pageTitle = L"";
        const wchar_t* pageSubtitle = L"";
        if (g_navSection == IDC_NAV_HISTORY) {
            pageTitle = Tr(L"服务器历史", L"Server History");
            pageSubtitle = Tr(L"查看、复用或清理最近连接记录",
                              L"Review, reuse, or clean up recent connection entries");
        } else if (g_navSection == IDC_NAV_FAVORITES) {
            pageTitle = Tr(L"隧道收藏", L"Tunnel Favorites");
            pageSubtitle = Tr(L"管理常用服务器和本地转发配置",
                              L"Manage frequently used servers and forwarding profiles");
        } else {
            pageTitle = Tr(L"SSH 隧道", L"SSH Tunnel");
            pageSubtitle = Tr(L"建立和管理多个 Windows OpenSSH 会话",
                              L"Create and manage multiple Windows OpenSSH sessions");
        }

        // RECT members are LONG. Avoid std::max(int, LONG), which MSVC
        // cannot deduce as one template type (C2672).
        const int headerStatusLeft = static_cast<int>(g_headerStatus.left);
        int headerTextRight = headerStatusLeft - 380;
        if (headerTextRight < 430) {
            headerTextRight = 430;
        }

        RECT title{238, 24, headerTextRight, 58};
        DrawTextAt(dc, pageTitle, title, g_fontTitle, C_TEXT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT subtitle{240, 57, headerTextRight + 40, 80};
        DrawTextAt(dc, pageSubtitle, subtitle, g_fontSmall, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::wstring statusText = Tr(L"已断开", L"Disconnected");
        COLORREF statusColor = C_MUTED;
        if (g_connectionState == STATE_CONNECTING) {
            statusText = Tr(L"连接中", L"Connecting");
            statusColor = C_WARNING;
        } else if (g_connectionState == STATE_CONNECTED) {
            statusText = Tr(L"已连接", L"Connected");
            statusColor = C_GREEN;
        }

        RECT status = g_headerStatus;
        FillRoundRect(dc, status, 18, C_CARD_ALT);
        StrokeRoundRect(dc, status, 18, C_BORDER);
        RECT dot{status.left + 12, status.top + 12,
                 status.left + 22, status.top + 22};
        FillRoundRect(dc, dot, 10, statusColor);
        RECT statText{status.left + 30, status.top,
                      status.right - 8, status.bottom};
        DrawTextAt(dc, statusText, statText, g_fontBold, C_TEXT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (g_navSection == IDC_NAV_QUICK) {
            for (const RECT* card : {&g_cardConnection, &g_cardForward,
                                     &g_cardTips, &g_cardSessions, &g_cardLog}) {
                FillRoundRect(dc, *card, 18, C_CARD);
                StrokeRoundRect(dc, *card, 18, C_BORDER);
            }

            RECT cTitle{g_cardConnection.left + 18, g_cardConnection.top + 14,
                        g_cardConnection.right - 18, g_cardConnection.top + 40};
            DrawTextAt(dc, Tr(L"连接配置", L"Connection"), cTitle, g_fontBold,
                       C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT cSub{g_cardConnection.left + 18, g_cardConnection.top + 36,
                      g_cardConnection.right - 18, g_cardConnection.top + 58};
            DrawTextAt(dc,
                       Tr(L"每次新建连接都会创建独立的 OpenSSH 会话",
                          L"Each new connection creates an independent OpenSSH session"),
                       cSub, g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT lblCmd{g_frameCommand.left, g_frameCommand.top - 22,
                        g_frameCommand.right, g_frameCommand.top - 4};
            DrawTextAt(dc, Tr(L"SSH 命令", L"SSH COMMAND"), lblCmd,
                       g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT lblAuth{g_frameAuth.left, g_frameAuth.top - 22,
                         g_frameAuth.right, g_frameAuth.top - 4};
            DrawTextAt(dc, Tr(L"认证方式", L"AUTHENTICATION"), lblAuth,
                       g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT lblCred{g_framePassword.left, g_framePassword.top - 22,
                         g_framePassword.right, g_framePassword.top - 4};
            DrawTextAt(dc,
                       g_authMode == AuthMode::Password
                           ? Tr(L"密码", L"PASSWORD")
                           : Tr(L"私钥文件", L"PRIVATE KEY"),
                       lblCred, g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            DrawInputFrame(dc, hwnd, g_frameCommand, IDC_COMMAND);
            if (g_authMode == AuthMode::Password) {
                DrawInputFrame(dc, hwnd, g_framePassword, IDC_PASSWORD);
            } else {
                DrawInputFrame(dc, hwnd, g_frameKeyPath, IDC_KEYPATH);
                RECT passLabel{g_frameKeyPass.left, g_frameKeyPass.top - 22,
                               g_frameKeyPass.right, g_frameKeyPass.top - 4};
                DrawTextAt(dc, Tr(L"私钥口令（可选）", L"KEY PASSPHRASE (OPTIONAL)"),
                           passLabel, g_fontSmall, C_MUTED,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                DrawInputFrame(dc, hwnd, g_frameKeyPass, IDC_KEYPASS);
            }

            RECT fTitle{g_cardForward.left + 18, g_cardForward.top + 16,
                        g_cardForward.right - 18, g_cardForward.top + 42};
            DrawTextAt(dc, Tr(L"本地转发", L"Local Forwarding"), fTitle,
                       g_fontBold, C_TEXT,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT fSub{g_cardForward.left + 18, g_cardForward.top + 38,
                      g_cardForward.right - 18, g_cardForward.top + 60};
            DrawTextAt(dc,
                       Tr(L"实时解析 -L：本地监听 → SSH 服务器侧远端目标",
                          L"Live -L mapping: local listener → remote target from the SSH server"),
                       fSub, g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT tipTitle{g_cardTips.left + 18, g_cardTips.top + 16,
                          g_cardTips.right - 18, g_cardTips.top + 42};
            DrawTextAt(dc, Tr(L"使用提示", L"Usage Notes"), tipTitle,
                       g_fontBold, C_TEXT,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT tipBody{g_cardTips.left + 18, g_cardTips.top + 50,
                         g_cardTips.right - 18, g_cardTips.bottom - 14};
            DrawTextAt(
                dc,
                Tr(
                    L"• -L 8888:xx.xx.xx.xx:8888 表示本机 127.0.0.1:8888 → 远端目标 xx.xx.xx.xx:8888。\n"
                    L"• 同一个 SSH 会话可以写多个 -L，例如同时转发 6006 和 8800。\n"
                    L"• 不同活动会话不能占用同一个本地端口。\n"
                    L"• 使用 -g 或 0.0.0.0 会把转发端口暴露给局域网，请确认防火墙与访问范围。\n"
                    L"• SSHLite 是连接/隧道管理器；纯 ssh user@host 会作为非交互 Keepalive 会话。",
                    L"• -L 8888:xx.xx.xx.xx:8888 means local 127.0.0.1:8888 → remote target xx.xx.xx.xx:8888.\n"
                    L"• One SSH session can contain multiple -L rules, for example 6006 and 8800.\n"
                    L"• Active sessions cannot share the same local listening port.\n"
                    L"• -g or 0.0.0.0 exposes a forward to the LAN; review firewall/access scope.\n"
                    L"• SSHLite is a connection/tunnel manager; plain ssh user@host runs as a non-interactive keepalive session."
                ),
                tipBody, g_fontSmall, C_MUTED,
                DT_LEFT | DT_TOP | DT_WORDBREAK
            );

            RECT sessionsTitle{g_cardSessions.left + 18, g_cardSessions.top + 16,
                               g_cardSessions.right - 118, g_cardSessions.top + 42};
            DrawTextAt(dc, Tr(L"SSH 会话", L"SSH Sessions"), sessionsTitle,
                       g_fontBold, C_TEXT,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            const int activeCount = ActiveSessionCount();
            std::wstring sessionSub = std::to_wstring(activeCount) +
                std::wstring(Tr(L" 个活动连接", L" active connections"));
            RECT sessionsSub{g_cardSessions.left + 18, g_cardSessions.top + 38,
                             g_cardSessions.right - 118, g_cardSessions.top + 58};
            DrawTextAt(dc, sessionSub, sessionsSub, g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT logTitle{g_cardLog.left + 18, g_cardLog.top + 16,
                          g_cardLog.right - 112, g_cardLog.top + 42};
            DrawTextAt(dc, Tr(L"活动日志", L"Activity"), logTitle, g_fontBold,
                       C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT logSub{g_cardLog.left + 18, g_cardLog.top + 38,
                        g_cardLog.right - 112, g_cardLog.top + 60};
            DrawTextAt(dc, Tr(L"OpenSSH 输出与边界提示", L"OpenSSH output and safeguards"),
                       logSub, g_fontSmall, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            FillRoundRect(dc, g_cardPageList, 18, C_CARD);
            StrokeRoundRect(dc, g_cardPageList, 18, C_BORDER);
            FillRoundRect(dc, g_cardPageDetail, 18, C_CARD);
            StrokeRoundRect(dc, g_cardPageDetail, 18, C_BORDER);

            if (g_navSection == IDC_NAV_HISTORY) {
                RECT lt{g_cardPageList.left + 18, g_cardPageList.top + 16,
                        g_cardPageList.right - 18, g_cardPageList.top + 42};
                DrawTextAt(dc, Tr(L"最近连接", L"Recent Connections"), lt,
                           g_fontBold, C_TEXT,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                RECT ls{g_cardPageList.left + 18, g_cardPageList.top + 38,
                        g_cardPageList.right - 18, g_cardPageList.top + 60};
                std::wstring countText = std::to_wstring(g_history.size()) +
                    std::wstring(Tr(L" 条记录，最多保留 20 条", L" entries; up to 20 are retained"));
                DrawTextAt(dc, countText, ls, g_fontSmall, C_MUTED,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                RECT dt{g_cardPageDetail.left + 18, g_cardPageDetail.top + 16,
                        g_cardPageDetail.right - 18, g_cardPageDetail.top + 42};
                DrawTextAt(dc, Tr(L"记录详情", L"Entry Details"), dt,
                           g_fontBold, C_TEXT,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                const int index = SelectedHistoryIndex(hwnd);
                if (index >= 0) {
                    const auto& e = g_history[static_cast<size_t>(index)];
                    RECT cmdLabel{g_cardPageDetail.left + 18, g_cardPageDetail.top + 70,
                                  g_cardPageDetail.right - 18, g_cardPageDetail.top + 92};
                    DrawTextAt(dc, Tr(L"SSH 命令", L"SSH COMMAND"), cmdLabel,
                               g_fontSmall, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    RECT cmdBox{g_cardPageDetail.left + 18, g_cardPageDetail.top + 98,
                                g_cardPageDetail.right - 18, g_cardPageDetail.top + 190};
                    FillRoundRect(dc, cmdBox, 12, C_EDIT);
                    StrokeRoundRect(dc, cmdBox, 12, C_BORDER);
                    RECT cmdText{cmdBox.left + 12, cmdBox.top + 10,
                                 cmdBox.right - 12, cmdBox.bottom - 10};
                    DrawTextAt(dc, e.command, cmdText, g_font, C_TEXT,
                               DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

                    std::wstring authText = e.authMode == AuthMode::PrivateKey
                        ? Tr(L"SSH 私钥", L"SSH Private Key")
                        : Tr(L"密码认证", L"Password");
                    RECT info1{cmdBox.left, cmdBox.bottom + 24, cmdBox.right, cmdBox.bottom + 48};
                    DrawTextAt(dc, std::wstring(Tr(L"认证：", L"Authentication: ")) + authText,
                               info1, g_font, C_TEXT,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    RECT info2{cmdBox.left, cmdBox.bottom + 56, cmdBox.right, cmdBox.bottom + 82};
                    DrawTextAt(dc,
                               std::wstring(Tr(L"自动打开浏览器：", L"Auto-open browser: ")) +
                                   (e.autoBrowser ? Tr(L"是", L"Yes") : Tr(L"否", L"No")),
                               info2, g_font, C_TEXT,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    if (!e.keyPath.empty()) {
                        RECT info3{cmdBox.left, cmdBox.bottom + 88, cmdBox.right, cmdBox.bottom + 118};
                        DrawTextAt(dc, std::wstring(Tr(L"私钥：", L"Key: ")) + e.keyPath,
                                   info3, g_fontSmall, C_MUTED,
                                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                } else {
                    RECT empty{g_cardPageDetail.left + 18, g_cardPageDetail.top + 88,
                               g_cardPageDetail.right - 18, g_cardPageDetail.top + 150};
                    DrawTextAt(dc,
                               Tr(L"暂无历史记录。成功发起连接后会自动记录服务器配置。",
                                  L"No history yet. Server settings are recorded when a connection is started."),
                               empty, g_font, C_MUTED,
                               DT_LEFT | DT_TOP | DT_WORDBREAK);
                }
            } else {
                RECT lt{g_cardPageList.left + 18, g_cardPageList.top + 16,
                        g_cardPageList.right - 18, g_cardPageList.top + 42};
                DrawTextAt(dc, Tr(L"收藏列表", L"Favorite Profiles"), lt,
                           g_fontBold, C_TEXT,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                RECT ls{g_cardPageList.left + 18, g_cardPageList.top + 38,
                        g_cardPageList.right - 18, g_cardPageList.top + 60};
                std::wstring countText = std::to_wstring(g_favorites.size()) +
                    std::wstring(Tr(L" 个收藏", L" favorites"));
                DrawTextAt(dc, countText, ls, g_fontSmall, C_MUTED,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                RECT dt{g_cardPageDetail.left + 18, g_cardPageDetail.top + 16,
                        g_cardPageDetail.right - 18, g_cardPageDetail.top + 42};
                DrawTextAt(dc, Tr(L"收藏详情", L"Favorite Details"), dt,
                           g_fontBold, C_TEXT,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                RECT nameLabel{g_frameFavName.left, g_frameFavName.top - 22,
                               g_frameFavName.right, g_frameFavName.top - 4};
                DrawTextAt(dc, Tr(L"收藏名称", L"FAVORITE NAME"), nameLabel,
                           g_fontSmall, C_MUTED,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                DrawInputFrame(dc, hwnd, g_frameFavName, IDC_FAVORITE_NAME);

                RECT passwordLabel{g_frameFavPassword.left, g_frameFavPassword.top - 22,
                                   g_frameFavPassword.right, g_frameFavPassword.top - 4};
                DrawTextAt(dc,
                           Tr(L"明文密码（密码认证收藏）",
                              L"PLAINTEXT PASSWORD (PASSWORD AUTH)"),
                           passwordLabel, g_fontSmall, C_DANGER,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                DrawInputFrame(dc, hwnd, g_frameFavPassword, IDC_FAVORITE_PASSWORD);

                RECT passwordWarn{g_frameFavPassword.left,
                                  g_frameFavPassword.bottom + 6,
                                  g_frameFavPassword.right,
                                  g_frameFavPassword.bottom + 42};
                DrawTextAt(dc,
                           Tr(L"警告：该值直接写入 SSHLite.ini，不加密。请勿上传或分享该配置文件。",
                              L"Warning: this value is written directly to SSHLite.ini without encryption. Do not upload or share that file."),
                           passwordWarn, g_fontSmall, C_DANGER,
                           DT_LEFT | DT_TOP | DT_WORDBREAK);

                const int index = SelectedFavoriteIndex(hwnd);
                if (index >= 0) {
                    const auto& e = g_favorites[static_cast<size_t>(index)];
                    RECT cmdLabel{g_cardPageDetail.left + 18, g_cardPageDetail.top + 254,
                                  g_cardPageDetail.right - 18, g_cardPageDetail.top + 276};
                    DrawTextAt(dc, Tr(L"已保存的 SSH 命令", L"SAVED SSH COMMAND"),
                               cmdLabel, g_fontSmall, C_MUTED,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    RECT cmdBox{g_cardPageDetail.left + 18, g_cardPageDetail.top + 282,
                                g_cardPageDetail.right - 18, g_cardPageDetail.top + 382};
                    FillRoundRect(dc, cmdBox, 12, C_EDIT);
                    StrokeRoundRect(dc, cmdBox, 12, C_BORDER);
                    RECT cmdText{cmdBox.left + 12, cmdBox.top + 10,
                                 cmdBox.right - 12, cmdBox.bottom - 10};
                    DrawTextAt(dc, e.command, cmdText, g_font, C_TEXT,
                               DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

                    RECT help{cmdBox.left, cmdBox.bottom + 24, cmdBox.right, cmdBox.bottom + 88};
                    DrawTextAt(dc,
                               Tr(L"“保存收藏”会保存当前连接配置。密码认证时，可以同时保存上方明文密码；私钥口令仍不会保存。",
                                  L"Save Favorite stores the current connection profile. For password authentication, the plaintext password above can also be stored; private-key passphrases are still not stored."),
                               help, g_fontSmall, C_MUTED,
                               DT_LEFT | DT_TOP | DT_WORDBREAK);
                } else {
                    RECT help{g_cardPageDetail.left + 18, g_cardPageDetail.top + 254,
                              g_cardPageDetail.right - 18, g_cardPageDetail.top + 330};
                    DrawTextAt(dc,
                               Tr(L"输入收藏名称后点击“保存收藏”，即可保存“快速连接”页当前配置。",
                                  L"Enter a favorite name and click Save Favorite to store the current Quick Connect profile."),
                               help, g_font, C_MUTED,
                               DT_LEFT | DT_TOP | DT_WORDBREAK);
                }
            }
        }

        BitBlt(windowDc, 0, 0,
               rc.right - rc.left, rc.bottom - rc.top,
               dc, 0, 0, SRCCOPY);

        SelectObject(dc, oldBitmap);
        DeleteObject(backBitmap);
        DeleteDC(dc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 1180;
        mmi->ptMinTrackSize.y = 780;
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        if (dis->CtlType == ODT_LISTBOX &&
            (dis->CtlID == IDC_SESSIONS ||
             dis->CtlID == IDC_HISTORY ||
             dis->CtlID == IDC_FAVORITES)) {
            RECT r = dis->rcItem;
            const bool selected = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF bg = selected ? C_CARD_ALT : C_CARD;

            HBRUSH br = CreateSolidBrush(bg);
            FillRect(dis->hDC, &r, br);
            DeleteObject(br);

            if (selected) {
                RECT accent{r.left, r.top + 6, r.left + 3, r.bottom - 6};
                FillRoundRect(dis->hDC, accent, 3, C_ACCENT);
            }

            if (dis->itemID != static_cast<UINT>(-1)) {
                if (dis->CtlID == IDC_SESSIONS &&
                    dis->itemID < g_sessionViews.size()) {
                    const auto& v = g_sessionViews[dis->itemID];
                    COLORREF dotColor = C_MUTED;
                    if (v.state == STATE_CONNECTED) dotColor = C_GREEN;
                    else if (v.state == STATE_CONNECTING) dotColor = C_WARNING;

                    RECT dot{r.left + 12, r.top + 14, r.left + 22, r.top + 24};
                    FillRoundRect(dis->hDC, dot, 10, dotColor);

                    std::wstring endpoint = v.destination.empty()
                        ? std::wstring(Tr(L"未知服务器", L"Unknown server"))
                        : v.destination;
                    endpoint += L":" + std::to_wstring(v.sshPort);

                    RECT first{r.left + 32, r.top + 6, r.right - 8, r.top + 28};
                    DrawTextAt(dis->hDC, endpoint, first, g_fontBold, C_TEXT,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                    std::wstring second;
                    if (!v.localPorts.empty()) {
                        second = Tr(L"本地端口 ", L"Local ");
                        for (size_t i = 0; i < v.localPorts.size(); ++i) {
                            if (i) second += L", ";
                            second += L":" + std::to_wstring(v.localPorts[i]);
                        }
                    } else {
                        second = Tr(L"SSH 保持连接", L"SSH keepalive");
                    }
                    RECT secondRect{r.left + 32, r.top + 27, r.right - 8, r.bottom - 4};
                    DrawTextAt(dis->hDC, second, secondRect, g_fontSmall, C_MUTED,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                } else if (dis->CtlID == IDC_HISTORY &&
                           dis->itemID < g_history.size()) {
                    const auto& e = g_history[dis->itemID];
                    std::wstring dest = ExtractDestination(e.command);
                    if (dest.empty()) dest = Tr(L"未知服务器", L"Unknown server");
                    dest += L":" + std::to_wstring(ExtractSshPort(e.command));

                    RECT first{r.left + 14, r.top + 7, r.right - 10, r.top + 30};
                    DrawTextAt(dis->hDC, dest, first, g_fontBold, C_TEXT,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT second{r.left + 14, r.top + 31, r.right - 10, r.bottom - 6};
                    DrawTextAt(dis->hDC, e.command, second, g_fontSmall, C_MUTED,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                } else if (dis->CtlID == IDC_FAVORITES &&
                           dis->itemID < g_favorites.size()) {
                    const auto& e = g_favorites[dis->itemID];
                    RECT first{r.left + 14, r.top + 7, r.right - 10, r.top + 31};
                    DrawTextAt(dis->hDC, e.name, first, g_fontBold, C_TEXT,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT second{r.left + 14, r.top + 32, r.right - 10, r.bottom - 6};
                    DrawTextAt(dis->hDC, e.command, second, g_fontSmall, C_MUTED,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }

            return TRUE;
        }

        DrawModernButton(dis);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_BG);
        return reinterpret_cast<LRESULT>(g_brushBg);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        const int id = GetDlgCtrlID(control);

        if (id == IDC_LOG) {
            SetTextColor(dc, C_TEXT);
            SetBkColor(dc, C_LOG);
            return reinterpret_cast<LRESULT>(g_brushLog);
        }

        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_EDIT);
        return reinterpret_cast<LRESULT>(g_brushEdit);
    }

    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_EDIT);
        return reinterpret_cast<LRESULT>(g_brushEdit);
    }

    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> p(
            reinterpret_cast<std::wstring*>(lParam)
        );

        HWND hLog = GetDlgItem(hwnd, IDC_LOG);
        const int len = GetWindowTextLengthW(hLog);

        SendMessageW(hLog, EM_SETSEL, len, len);
        SendMessageW(
            hLog,
            EM_REPLACESEL,
            FALSE,
            reinterpret_cast<LPARAM>(p->c_str())
        );
        SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
        TrimLogControl(hLog);
        return 0;
    }

    case WM_APP_SESSION_LOG: {
        std::unique_ptr<SessionEvent> ev(reinterpret_cast<SessionEvent*>(lParam));
        HWND hLog = GetDlgItem(hwnd, IDC_LOG);
        const int len = GetWindowTextLengthW(hLog);
        std::wstring combined = L"[#" + std::to_wstring(ev->id) + L"] " + ev->text;
        SendMessageW(hLog, EM_SETSEL, len, len);
        SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(combined.c_str()));
        SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
        TrimLogControl(hLog);
        return 0;
    }

    case WM_APP_SESSION_STATE: {
        std::unique_ptr<SessionEvent> ev(reinterpret_cast<SessionEvent*>(lParam));
        for (auto& v : g_sessionViews) {
            if (v.id == ev->id) { v.state = ev->state; break; }
        }
        RefreshSessionsList(hwnd);
        if (ev->state == STATE_CONNECTED &&
            SendMessageW(GetDlgItem(hwnd, IDC_AUTOBROWSER), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            SelectedSessionId(hwnd) == ev->id) {
            OpenLocalBrowser(hwnd);
        }
        return 0;
    }

    case WM_APP_STATE: {
        g_connectionState = static_cast<int>(wParam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            g_sessionManager.StopAll();
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE: {
        const int active = ActiveSessionCount();
        std::wstring message;
        if (active > 0) {
            if (g_language.load() == 0) {
                message = L"当前仍有 " + std::to_wstring(active) +
                    L" 个活动 SSH 会话。\n\n退出 SSHLite 将立即断开全部连接和本地转发。\n只有点击“是”才会退出。\n\n确定退出吗？";
            } else {
                message = L"There are still " + std::to_wstring(active) +
                    L" active SSH sessions.\n\nExiting SSHLite will immediately disconnect all sessions and local forwards.\nThe application closes only if you choose Yes.\n\nExit now?";
            }
        } else {
            message = Tr(
                L"确定退出 SSHLite 吗？\n\n只有点击“是”才会关闭软件。",
                L"Exit SSHLite?\n\nThe application closes only if you choose Yes.");
        }

        if (MessageBoxW(hwnd, message.c_str(),
                        Tr(L"确认退出", L"Confirm Exit"),
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return 0;
        }

        ShowWindow(hwnd, SW_HIDE);
        g_sessionManager.StopAll();
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY: {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd, &placement)) {
            const RECT& wr = placement.rcNormalPosition;
            const int ww = static_cast<int>(wr.right - wr.left);
            const int wh = static_cast<int>(wr.bottom - wr.top);
            if (ww >= 1180 && wh >= 780) {
                IniWrite(L"UI", L"WindowW", std::to_wstring(ww));
                IniWrite(L"UI", L"WindowH", std::to_wstring(wh));
            }
        }
        {
            HWND favPassword = GetDlgItem(hwnd, IDC_FAVORITE_PASSWORD);
            if (favPassword) {
                std::wstring secret = GetWindowTextString(favPassword);
                SetWindowTextW(favPassword, L"");
                if (!secret.empty()) {
                    SecureZeroMemory(secret.data(),
                                     secret.size() * sizeof(wchar_t));
                }
            }
        }

        if (g_font) DeleteObject(g_font);
        if (g_fontBold) DeleteObject(g_fontBold);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontSmall) DeleteObject(g_fontSmall);
        if (g_fontMono) DeleteObject(g_fontMono);
        DeleteUiBrushes();
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void BackendPostSessionLog(int sessionId, const std::wstring& text) {
    PostSessionLog(sessionId, text);
}

void BackendPostSessionState(int sessionId, int state) {
    PostSessionState(sessionId, state);
}

const wchar_t* BackendTr(const wchar_t* zh, const wchar_t* en) {
    return Tr(zh, en);
}

std::wstring BackendAppDirectory() {
    return GetAppDirectory();
}

std::mutex& BackendLaunchEnvironmentMutex() {
    return g_launchEnvMutex;
}

bool HandleStandardEditShortcuts(const MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) {
        return false;
    }

    if (msg.wParam != L'A' && msg.wParam != L'a') {
        return false;
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) == 0) {
        return false;
    }

    // Do not turn Alt+Ctrl+A into Select All.
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        return false;
    }

    HWND focus = GetFocus();
    if (!focus) {
        return false;
    }

    wchar_t className[32]{};
    if (GetClassNameW(focus, className, static_cast<int>(std::size(className))) <= 0) {
        return false;
    }

    // Apply only to native EDIT controls. This includes the SSH command,
    // password/key fields, favorite editors and the read-only Activity log.
    if (_wcsicmp(className, L"Edit") != 0) {
        return false;
    }

    SendMessageW(focus, EM_SETSEL, 0, static_cast<LPARAM>(-1));
    return true;
}

int RunSSHLiteApplication(HINSTANCE hInstance, int nCmdShow) {
    g_hInst = hInstance;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW toggleClass{ sizeof(toggleClass) };
    toggleClass.lpfnWndProc = ToggleWndProc;
    toggleClass.hInstance = hInstance;
    toggleClass.lpszClassName = L"SSHLiteModernToggle";
    toggleClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    toggleClass.hbrBackground = CreateSolidBrush(C_CARD);
    RegisterClassExW(&toggleClass);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SSHLiteWin32Class";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = CreateSolidBrush(C_BG);

    if (!RegisterClassExW(&wc)) return 1;

    const int initialW = (std::max)(1180, IniReadInt(L"UI", L"WindowW", 1320));
    const int initialH = (std::max)(780, IniReadInt(L"UI", L"WindowH", 840));

    const std::wstring windowTitle =
        std::wstring(APP_NAME) + L" " + APP_VERSION +
        Tr(L" — 轻量 SSH 客户端", L" — Lightweight SSH Client");

    g_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initialW, initialH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hwnd) return 2;

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (HandleStandardEditShortcuts(msg)) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
