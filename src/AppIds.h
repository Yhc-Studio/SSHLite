#pragma once

#include <windows.h>

inline constexpr wchar_t APP_NAME[] = L"SSHLite";
inline constexpr wchar_t APP_VERSION[] = L"1.0.0";

inline constexpr UINT WM_APP_LOG           = WM_APP + 1;
inline constexpr UINT WM_APP_STATE         = WM_APP + 2;
inline constexpr UINT WM_APP_SESSION_LOG   = WM_APP + 3;
inline constexpr UINT WM_APP_SESSION_STATE = WM_APP + 4;

inline constexpr int IDC_HISTORY          = 1001;
inline constexpr int IDC_COMMAND          = 1002;
inline constexpr int IDC_AUTHMODE         = 1003;
inline constexpr int IDC_PASSWORD         = 1004;
inline constexpr int IDC_KEYPATH          = 1005;
inline constexpr int IDC_BROWSEKEY        = 1006;
inline constexpr int IDC_KEYPASS          = 1007;
inline constexpr int IDC_CONNECT          = 1008;
inline constexpr int IDC_DISCONNECT       = 1009;
inline constexpr int IDC_OPEN_BROWSER     = 1010;
inline constexpr int IDC_AUTOBROWSER      = 1011;
inline constexpr int IDC_LOCALURL         = 1012;
inline constexpr int IDC_FAVORITES        = 1013;
inline constexpr int IDC_FAVORITE_NAME    = 1014;
inline constexpr int IDC_SAVE_FAVORITE    = 1015;
inline constexpr int IDC_DELETE_FAVORITE  = 1016;
inline constexpr int IDC_LOG              = 1017;
inline constexpr int IDC_CLEAR_LOG        = 1018;
inline constexpr int IDC_NAV_QUICK        = 1101;
inline constexpr int IDC_NAV_HISTORY      = 1102;
inline constexpr int IDC_NAV_FAVORITES    = 1103;
inline constexpr int IDC_LANGUAGE         = 1201;
inline constexpr int IDC_THEME            = 1202;
inline constexpr int IDC_CUSTOM_COLOR     = 1203;
inline constexpr int IDC_SESSIONS         = 1204;
inline constexpr int IDC_STOP_ALL         = 1205;

inline constexpr int IDC_HISTORY_USE       = 1301;
inline constexpr int IDC_HISTORY_DELETE    = 1302;
inline constexpr int IDC_HISTORY_CLEAR     = 1303;
inline constexpr int IDC_FAVORITE_USE      = 1311;
inline constexpr int IDC_FAVORITE_PASSWORD = 1312;

inline constexpr int IDM_LANGUAGE_ZH   = 3001;
inline constexpr int IDM_LANGUAGE_EN   = 3002;
inline constexpr int IDM_THEME_DARK    = 3010;
inline constexpr int IDM_THEME_LIGHT   = 3011;
inline constexpr int IDM_THEME_OCEAN   = 3012;
inline constexpr int IDM_THEME_EMERALD = 3013;
inline constexpr int IDM_THEME_ROSE    = 3014;
inline constexpr int IDM_THEME_CUSTOM  = 3015;
inline constexpr int IDM_URL_BASE      = 3100;

