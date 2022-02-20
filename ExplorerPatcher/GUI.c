#include <initguid.h>
DEFINE_GUID(LiveSetting_Property_GUID, 0xc12bcd8e, 0x2a8e, 0x4950, 0x8a, 0xe7, 0x36, 0x25, 0x11, 0x1d, 0x58, 0xeb);
#include <oleacc.h>
#include "GUI.h"

TCHAR GUI_title[260];
FILE* AuditFile = NULL;
LANGID locale;
void* GUI_FileMapping = NULL;
DWORD GUI_FileSize = 0;
BOOL g_darkModeEnabled = FALSE;
static void(*RefreshImmersiveColorPolicyState)() = NULL;
static BOOL(*ShouldAppsUseDarkMode)() = NULL;
DWORD dwTaskbarPosition = 3;
BOOL gui_bOldTaskbar = TRUE;

int GUI_DeleteWeatherFolder()
{
    WCHAR wszWorkFolder[MAX_PATH + 1];
    ZeroMemory(wszWorkFolder, (MAX_PATH + 1) * sizeof(WCHAR));
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, wszWorkFolder);
    wcscat_s(wszWorkFolder, MAX_PATH + 1, L"\\ExplorerPatcher\\ep_weather_host");
    wszWorkFolder[wcslen(wszWorkFolder) + 1] = 0;
    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof(SHFILEOPSTRUCTW));
    op.wFunc = FO_DELETE;
    op.pFrom = wszWorkFolder;
    op.fFlags = FOF_NO_UI;
    if (!SHFileOperationW(&op))
    {
        return IDOK;
    }
    else
    {
        if (op.fAnyOperationsAborted)
        {
            return IDCANCEL;
        }
        else
        {
            return IDABORT;
        }
    }
}

BOOL GUI_Internal_DeleteWeatherFolder(int* res)
{
    if (!FindWindowW(_T(EPW_WEATHER_CLASSNAME), NULL))
    {
        if (!*res)
        {
            if (PleaseWait_UpdateTimeout(3000))
            {
                *res = IDRETRY;
            }
            else
            {
                *res = IDABORT;
                return FALSE;
            }
        }
        else if (*res == IDRETRY)
        {
            *res = GUI_DeleteWeatherFolder();
            if (*res != IDRETRY)
            {
                return FALSE;
            }
        }
    }
    else
    {
    }
    return TRUE;
}

void PlayHelpMessage(GUI* _this)
{
    unsigned int max_section = 0;
    for (unsigned int i = 0; i < 100; ++i)
    {
        if (_this->sectionNames[i][0] == 0)
        {
            max_section = i - 1;
            break;
        }
    }

    WCHAR wszAccText[1000];
    swprintf_s(
        wszAccText,
        1000,
        L"Welcome to ExplorerPatcher. "
        L"Selected page is: %s: %d of %d. "
        L"To switch pages, press the Left or Right arrow keys or press a number (%d to %d). "
        L"To select an item, press the Up or Down arrow keys or Shift+Tab and Tab. "
        L"To interact with the selected item, press Space or Return. "
        L"To close this window, press Escape. "
        L"Press a number to switch to the corresponding page: ",
        _this->sectionNames[_this->section],
        _this->section + 1,
        max_section + 1,
        1,
        max_section + 1
    );
    for (unsigned int i = 0; i < 100; ++i)
    {
        if (_this->sectionNames[i][0] == 0)
        {
            break;
        }
        WCHAR wszAdd[100];
        swprintf_s(wszAdd, 100, L"%d: %s, ", i + 1, _this->sectionNames[i]);
        wcscat_s(wszAccText, 1000, wszAdd);
    }
    wcscat_s(wszAccText, 1000, L"\nTo listen to this message again, press the F1 key at any time.\n");
    SetWindowTextW(_this->hAccLabel, wszAccText);
    NotifyWinEvent(
        EVENT_OBJECT_LIVEREGIONCHANGED,
        _this->hAccLabel,
        OBJID_CLIENT,
        CHILDID_SELF
    );
}

NTSTATUS NTAPI hookRtlQueryElevationFlags(DWORD* pFlags)
{
    *pFlags = 0;
    return 0;
}

PVOID pvRtlQueryElevationFlags;

LONG NTAPI OnVex(PEXCEPTION_POINTERS ExceptionInfo)
{
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP &&
        ExceptionInfo->ExceptionRecord->ExceptionAddress == pvRtlQueryElevationFlags)
    {
        ExceptionInfo->ContextRecord->
#if defined(_X86_)
            Eip
#elif defined (_AMD64_)
            Rip
#else
#error not implemented
#endif
            = (ULONG_PTR)hookRtlQueryElevationFlags;

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL IsColorSchemeChangeMessage(LPARAM lParam)
{
    BOOL is = FALSE;
    if (lParam && CompareStringOrdinal(lParam, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
    {
        is = TRUE;
    }
    return is;
}

LSTATUS GUI_Internal_RegSetValueExW(
    HKEY       hKey,
    LPCWSTR    lpValueName,
    DWORD      Reserved,
    DWORD      dwType,
    const BYTE* lpData,
    DWORD      cbData
)
{
    if (!lpValueName || wcsncmp(lpValueName, L"Virtualized_" _T(EP_CLSID), 50))
    {
        return RegSetValueExW(hKey, lpValueName, 0, dwType, lpData, cbData);
    }
    if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_TaskbarPosition"))
    {
        StuckRectsData srd;
        DWORD pcbData = sizeof(StuckRectsData);
        RegGetValueW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRectsLegacy",
            L"Settings",
            REG_BINARY,
            NULL,
            &srd,
            &pcbData);
        if (pcbData == sizeof(StuckRectsData) && srd.pvData[0] == sizeof(StuckRectsData) && srd.pvData[1] == -2)
        {
            srd.pvData[3] = *(DWORD*)lpData;
            dwTaskbarPosition = *(DWORD*)lpData;
            RegSetKeyValueW(
                HKEY_CURRENT_USER,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRectsLegacy",
                L"Settings",
                REG_BINARY,
                &srd,
                sizeof(StuckRectsData)
            );
        }
        pcbData = sizeof(StuckRectsData);
        RegGetValueW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRects3",
            L"Settings",
            REG_BINARY,
            NULL,
            &srd,
            &pcbData);
        if (pcbData == sizeof(StuckRectsData) && srd.pvData[0] == sizeof(StuckRectsData) && srd.pvData[1] == -2)
        {
            srd.pvData[3] = *(DWORD*)lpData;
            if (srd.pvData[3] != 1 && srd.pvData[3] != 3) // Disallow left/right settings for Windows 11 taskbar, as this breaks it
            {
                srd.pvData[3] = 3;
            }
            RegSetKeyValueW(
                HKEY_CURRENT_USER,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRects3",
                L"Settings",
                REG_BINARY,
                &srd,
                sizeof(StuckRectsData)
            );
        }
        return ERROR_SUCCESS;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_MMTaskbarPosition"))
    {
        HKEY hKey = NULL;
        RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MMStuckRectsLegacy",
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WRITE,
            &hKey
        );
        if (hKey)
        {
            DWORD cValues = 0;
            RegQueryInfoKeyW(
                hKey,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                &cValues,
                NULL,
                NULL,
                NULL,
                NULL
            );
            WCHAR name[60];
            DWORD szName = 60;
            StuckRectsData srd;
            DWORD pcbData = sizeof(StuckRectsData);
            for (int i = 0; i < cValues; ++i)
            {
                RegEnumValueW(
                    hKey,
                    i,
                    name,
                    &szName,
                    0,
                    NULL,
                    &srd,
                    &pcbData
                );
                szName = 60;
                srd.pvData[3] = *(DWORD*)lpData;
                pcbData = sizeof(StuckRectsData);
                RegSetKeyValueW(
                    HKEY_CURRENT_USER,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MMStuckRectsLegacy",
                    name,
                    REG_BINARY,
                    &srd,
                    sizeof(StuckRectsData)
                );
            }
            RegCloseKey(hKey);
            SendNotifyMessageW(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)L"TraySettings");
        }
        RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MMStuckRects3",
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WRITE,
            &hKey
        );
        if (hKey)
        {
            DWORD cValues = 0;
            RegQueryInfoKeyW(
                hKey,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                &cValues,
                NULL,
                NULL,
                NULL,
                NULL
            );
            WCHAR name[60];
            DWORD szName = 60;
            StuckRectsData srd;
            DWORD pcbData = sizeof(StuckRectsData);
            for (int i = 0; i < cValues; ++i)
            {
                RegEnumValueW(
                    hKey,
                    i,
                    name,
                    &szName,
                    0,
                    NULL,
                    &srd,
                    &pcbData
                );
                szName = 60;
                srd.pvData[3] = *(DWORD*)lpData;
                if (srd.pvData[3] != 1 && srd.pvData[3] != 3) // Disallow left/right settings for Windows 11 taskbar, as this breaks it
                {
                    srd.pvData[3] = 3;
                }
                pcbData = sizeof(StuckRectsData);
                RegSetKeyValueW(
                    HKEY_CURRENT_USER,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MMStuckRects3",
                    name,
                    REG_BINARY,
                    &srd,
                    sizeof(StuckRectsData)
                );
            }
            RegCloseKey(hKey);
        }
        return ERROR_SUCCESS;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_AutoHideTaskbar"))
    {
        APPBARDATA abd;
        abd.cbSize = sizeof(APPBARDATA);
        abd.lParam = *(DWORD*)lpData;
        SHAppBarMessage(ABM_SETSTATE, &abd);
        return ERROR_SUCCESS;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_PeopleBand"))
    {
        DWORD dwData = 0, dwSize = sizeof(DWORD);
        RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\\People", L"PeopleBand", RRF_RT_DWORD, NULL, &dwData, &dwSize);
        if ((dwData && *(DWORD*)lpData) || (!dwData && !*(DWORD*)lpData))
        {
            return ERROR_SUCCESS;
        }
        PostMessageW(FindWindowW(L"Shell_TrayWnd", NULL), WM_COMMAND, 435, 0);
        return ERROR_SUCCESS;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_Start_MaximumFrequentApps"))
    {
        RegSetKeyValueW(
            HKEY_CURRENT_USER,
            TEXT(REGPATH_OLD),
            L"Start_MaximumFrequentApps",
            dwType,
            lpData,
            cbData
        );
        return RegSetValueExW(hKey, L"Start_MaximumFrequentApps", 0, dwType, lpData, cbData);
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_DisableRoundedCorners"))
    {
        return RegisterDWMService(*(DWORD*)lpData, 0);
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_RegisterAsShellExtension"))
    {
        HKEY hKey2 = NULL;
        RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"Software\\Classes\\CLSID\\" _T(EP_CLSID) L"\\InprocServer32",
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WOW64_64KEY,
            &hKey2
        );
        WCHAR wszArgs[MAX_PATH];
        if (((hKey2 == NULL || hKey2 == INVALID_HANDLE_VALUE) && !*(DWORD*)lpData) || !(hKey2 == NULL || hKey2 == INVALID_HANDLE_VALUE) && (*(DWORD*)lpData))
        {
            RegCloseKey(hKey2);
            return ERROR_SUCCESS;
        }
        if (!(hKey2 == NULL || hKey2 == INVALID_HANDLE_VALUE))
        {
            RegCloseKey(hKey2);
        }
        if (*(DWORD*)lpData)
        {
            wszArgs[0] = L'\"';
            SHGetFolderPathW(NULL, SPECIAL_FOLDER, NULL, SHGFP_TYPE_CURRENT, wszArgs + 1);
            wcscat_s(wszArgs, MAX_PATH, _T(APP_RELATIVE_PATH) L"\\" _T(PRODUCT_NAME) L".amd64.dll\"");
        }
        else
        {
            wszArgs[0] = L'/';
            wszArgs[1] = L'u';
            wszArgs[2] = L' ';
            wszArgs[3] = L'"';
            SHGetFolderPathW(NULL, SPECIAL_FOLDER, NULL, SHGFP_TYPE_CURRENT, wszArgs + 4);
            wcscat_s(wszArgs, MAX_PATH, _T(APP_RELATIVE_PATH) L"\\" _T(PRODUCT_NAME) L".amd64.dll\"");
        }
        wprintf(L"%s\n", wszArgs);
        WCHAR wszApp[MAX_PATH * 2];
        GetSystemDirectoryW(wszApp, MAX_PATH * 2);
        wcscat_s(wszApp, MAX_PATH * 2, L"\\regsvr32.exe");
        wprintf(L"%s\n", wszApp);
        SHELLEXECUTEINFOW sei;
        ZeroMemory(&sei, sizeof(SHELLEXECUTEINFOW));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = NULL;
        sei.hInstApp = NULL;
        sei.lpVerb = L"runas";
        sei.lpFile = wszApp;
        sei.lpParameters = wszArgs;
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;
        if (ShellExecuteExW(&sei) && sei.hProcess)
        {
            WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD dwExitCode = 0;
            if (GetExitCodeProcess(sei.hProcess, &dwExitCode) && !dwExitCode)
            {

            }
            else
            {

            }
            CloseHandle(sei.hProcess);
        }
        else
        {
            DWORD dwError = GetLastError();
            if (dwError == ERROR_CANCELLED)
            {
            }
        }
        return ERROR_SUCCESS;
     }
}

LSTATUS GUI_RegSetValueExW(
    HKEY       hKey,
    LPCWSTR    lpValueName,
    DWORD      Reserved,
    DWORD      dwType,
    const BYTE* lpData,
    DWORD      cbData
)
{
    LSTATUS lRes = GUI_Internal_RegSetValueExW(hKey, lpValueName, Reserved, dwType, lpData, cbData);
    return lRes;
}

LSTATUS GUI_Internal_RegQueryValueExW(
    HKEY    hKey,
    LPCWSTR lpValueName,
    LPDWORD lpReserved,
    LPDWORD lpType,
    LPBYTE  lpData,
    LPDWORD lpcbData
)
{
    if (!lpValueName || wcsncmp(lpValueName, L"Virtualized_" _T(EP_CLSID), 50))
    {
        return RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    }
    if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_TaskbarPosition"))
    {
        StuckRectsData srd;
        DWORD pcbData = sizeof(StuckRectsData);
        RegGetValueW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRectsLegacy",
            L"Settings",
            REG_BINARY,
            NULL,
            &srd,
            &pcbData);
        if (pcbData == sizeof(StuckRectsData) && srd.pvData[0] == sizeof(StuckRectsData) && srd.pvData[1] == -2)
        {
            dwTaskbarPosition = srd.pvData[3];
            if (!gui_bOldTaskbar)
            {
                if (srd.pvData[3] != 1 && srd.pvData[3] != 3) // Disallow left/right settings for Windows 11 taskbar, as this breaks it
                {
                    srd.pvData[3] = 3;
                }
            }
            *(DWORD*)lpData = srd.pvData[3];
            return ERROR_SUCCESS;
        }
        return ERROR_ACCESS_DENIED;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_MMTaskbarPosition"))
    {
        HKEY hKey = NULL;
        RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MMStuckRectsLegacy",
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WRITE,
            &hKey
        );
        if (hKey)
        {
            WCHAR name[60];
            DWORD szName = 60;
            StuckRectsData srd;
            DWORD pcbData = sizeof(StuckRectsData);
            RegEnumValueW(
                hKey,
                0,
                name,
                &szName,
                0,
                NULL,
                &srd,
                &pcbData
            );
            if (pcbData == sizeof(StuckRectsData) && srd.pvData[0] == sizeof(StuckRectsData) && srd.pvData[1] == -2)
            {
                if (!gui_bOldTaskbar)
                {
                    if (srd.pvData[3] != 1 && srd.pvData[3] != 3) // Disallow left/right settings for Windows 11 taskbar, as this breaks it
                    {
                        srd.pvData[3] = 3;
                    }
                }
                *(DWORD*)lpData = srd.pvData[3];
                RegCloseKey(hKey);
                return ERROR_SUCCESS;
            }
            RegCloseKey(hKey);
        }
        return ERROR_ACCESS_DENIED;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_AutoHideTaskbar"))
    {
        APPBARDATA abd;
        abd.cbSize = sizeof(APPBARDATA);
        *(DWORD*)lpData = (SHAppBarMessage(ABM_GETSTATE, &abd) == ABS_AUTOHIDE);
        return ERROR_SUCCESS;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_PeopleBand"))
    {
        return RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\\People", L"PeopleBand", RRF_RT_DWORD, NULL, lpData, lpcbData);
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_Start_MaximumFrequentApps"))
    {
        return RegQueryValueExW(hKey, L"Start_MaximumFrequentApps", lpReserved, lpType, lpData, lpcbData);
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_DisableRoundedCorners"))
    {
        HANDLE h_exists = CreateEventW(NULL, FALSE, FALSE, _T(EP_DWM_EVENTNAME));
        if (h_exists)
        {
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                *(DWORD*)lpData = 1;
            }
            else
            {
                *(DWORD*)lpData = 0;
            }
            CloseHandle(h_exists);
        }
        else
        {
            if (GetLastError() == ERROR_ACCESS_DENIED)
            {
                *(DWORD*)lpData = 1;
            }
            else
            {
                *(DWORD*)lpData = 0;
            }
        }
        return ERROR_SUCCESS;
    }
    else if (!wcscmp(lpValueName, L"Virtualized_" _T(EP_CLSID) L"_RegisterAsShellExtension"))
    {
        HKEY hKey2 = NULL;
        RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"Software\\Classes\\CLSID\\" _T(EP_CLSID) L"\\InprocServer32",
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WOW64_64KEY,
            &hKey2
        );
        if (hKey2 == NULL || hKey2 == INVALID_HANDLE_VALUE)
        {
            *(DWORD*)lpData = 0;
        }
        else
        {
            *(DWORD*)lpData = 1;
            RegCloseKey(hKey2);
        }
    }
}

LSTATUS GUI_RegCreateKeyExW(
    HKEY                        hKey,
    LPCWSTR                     lpSubKey,
    DWORD                       Reserved,
    LPWSTR                      lpClass,
    DWORD                       dwOptions,
    REGSAM                      samDesired,
    const LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    PHKEY                       phkResult,
    LPDWORD                     lpdwDisposition
)
{
    LSTATUS lRes = RegCreateKeyExW(hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
    if (AuditFile)
    {
        fwprintf(AuditFile, L"[%s\\%s]\n", hKey == HKEY_CURRENT_USER ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE", lpSubKey);
    }
    return lRes;
}

LSTATUS GUI_RegOpenKeyExW(
    HKEY    hKey,
    LPCWSTR lpSubKey,
    DWORD   ulOptions,
    REGSAM  samDesired,
    PHKEY   phkResult
)
{
    LSTATUS lRes = RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    if (AuditFile)
    {
        fwprintf(AuditFile, L"[%s%s\\%s]\n", (*phkResult == NULL || *phkResult == INVALID_HANDLE_VALUE) ? L"-" : L"", hKey == HKEY_CURRENT_USER ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE", lpSubKey);
        WCHAR wszDefVal[MAX_PATH];
        ZeroMemory(wszDefVal, MAX_PATH);
        DWORD dwLen = MAX_PATH;
        RegGetValueW(hKey, lpSubKey, NULL, RRF_RT_REG_SZ, NULL, wszDefVal, &dwLen);
        if (wszDefVal[0])
        {
            fwprintf(AuditFile, L"@=\"%s\"\n", wszDefVal);
        }
        else
        {
            fwprintf(AuditFile, L"@=\"\"\n");
        }
    }
    return lRes;
}

LSTATUS GUI_RegQueryValueExW(
    HKEY    hKey,
    LPCWSTR lpValueName,
    LPDWORD lpReserved,
    LPDWORD lpType,
    LPBYTE  lpData,
    LPDWORD lpcbData
)
{
    DWORD dwSize = lpcbData ? *(DWORD*)lpcbData : sizeof(DWORD);
    LSTATUS lRes = GUI_Internal_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    if (AuditFile)
    {
        if (dwSize != sizeof(DWORD))
        {
            fwprintf(AuditFile, L"%s\"%s\"=\"%s\"\n", (lpValueName && wcsncmp(lpValueName, L"Virtualized_" _T(EP_CLSID), 50)) ? L"" : L";", lpValueName, lpData);
        }
        else
        {
            fwprintf(AuditFile, L"%s\"%s\"=dword:%08x\n", (lpValueName && wcsncmp(lpValueName, L"Virtualized_" _T(EP_CLSID), 50)) ? L"" : L";", lpValueName, *(DWORD*)lpData);
        }
    }
    return lRes;
}


static HRESULT GUI_AboutProc(
    HWND hwnd,
    UINT uNotification,
    WPARAM wParam,
    LPARAM lParam,
    LONG_PTR lpRefData
)
{
    switch (uNotification)
    {
    case TDN_BUTTON_CLICKED:
    {
        if (wParam == IDOK || wParam == IDCANCEL)
        {
            return S_OK;
        }
        else if (wParam == IDS_VISITGITHUB)
        {
            ShellExecuteA(
                NULL,
                "open",
                "https://github.com/valinet/ExplorerPatcher",
                NULL,
                NULL,
                SW_SHOWNORMAL
            );
        }
        else if (wParam == IDS_VISITWEBSITE)
        {
            ShellExecuteA(
                NULL,
                "open",
                "https://www.valinet.ro",
                NULL,
                NULL,
                SW_SHOWNORMAL
            );
        }
        else if (wParam == IDS_LICENSEINFO)
        {
            ShellExecuteA(
                NULL,
                "open",
                "mailto:valentingabrielradu@gmail.com",
                NULL,
                NULL,
                SW_SHOWNORMAL
            );
        }
    }
    }
    return S_OK;
}

static void GUI_SetSection(GUI* _this, BOOL bCheckEnablement, int dwSection)
{
    _this->section = dwSection;

    HKEY hKey = NULL;
    DWORD dwSize = sizeof(DWORD);
    RegCreateKeyExW(
        HKEY_CURRENT_USER,
        TEXT(REGPATH),
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WOW64_64KEY | KEY_WRITE,
        NULL,
        &hKey,
        NULL
    );
    if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
    {
        return;
    }

    BOOL bEnabled = FALSE;
    if (bCheckEnablement)
    {
        dwSize = sizeof(DWORD);
        RegQueryValueExW(
            hKey,
            TEXT("LastSectionInProperties"),
            0,
            NULL,
            &bEnabled,
            &dwSize
        );
        dwSection++;
    }
    else
    {
        bEnabled = TRUE;
    }

    if (bEnabled)
    {
        RegSetValueExW(
            hKey,
            TEXT("LastSectionInProperties"),
            0,
            REG_DWORD,
            &dwSection,
            sizeof(DWORD)
        );
    }

    RegCloseKey(hKey);
}

static BOOL GUI_Build(HDC hDC, HWND hwnd, POINT pt)
{
    GUI* _this;
    LONG_PTR ptr = GetWindowLongPtr(hwnd, GWLP_USERDATA);
    _this = (int*)(ptr);
    double dx = _this->dpi.x / 96.0, dy = _this->dpi.y / 96.0;
    _this->padding.left = GUI_PADDING_LEFT * dx;
    _this->padding.right = GUI_PADDING_RIGHT * dx;
    _this->padding.top = GUI_PADDING_TOP * dy;
    _this->padding.bottom = GUI_PADDING_BOTTOM * dy;
    _this->sidebarWidth = GUI_SIDEBAR_WIDTH * dx;

    RECT rc;
    GetClientRect(hwnd, &rc);

    PVOID pRscr = NULL;
    DWORD cbRscr = 0;
    if (GUI_FileMapping && GUI_FileSize)
    {
        pRscr = GUI_FileMapping;
        cbRscr = GUI_FileSize;
    }
    else
    {
        HRSRC hRscr = FindResource(
            hModule,
            MAKEINTRESOURCE(IDR_REGISTRY1),
            RT_RCDATA
        );
        if (!hRscr)
        {
            return FALSE;
        }
        HGLOBAL hgRscr = LoadResource(
            hModule,
            hRscr
        );
        if (!hgRscr)
        {
            return FALSE;
        }
        pRscr = LockResource(hgRscr);
        cbRscr = SizeofResource(
            hModule,
            hRscr
        );
    }

    UINT dpiX = 0, dpiY = 0;
    HRESULT hr = GetDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), MDT_DEFAULT, &dpiX, &dpiY);
    LOGFONT logFont;
    memset(&logFont, 0, sizeof(logFont));
    NONCLIENTMETRICS ncm;
    ncm.cbSize = sizeof(NONCLIENTMETRICS);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0, dpiX);
    logFont = ncm.lfCaptionFont;
    //logFont.lfHeight = GUI_CAPTION_FONT_SIZE * dy;
    //logFont.lfWeight = FW_BOLD;
    HFONT hFontCaption = CreateFontIndirect(&logFont);
    logFont = ncm.lfMenuFont;
    //logFont.lfHeight = GUI_TITLE_FONT_SIZE * dy;
    HFONT hFontTitle = CreateFontIndirect(&logFont);
    logFont.lfWeight = FW_REGULAR;
    logFont.lfUnderline = 1;
    HFONT hFontUnderline = CreateFontIndirect(&logFont);
    logFont.lfWeight = FW_REGULAR;
    logFont.lfUnderline = 0;
    HFONT hFontRegular = CreateFontIndirect(&logFont);
    logFont.lfWeight = FW_DEMIBOLD;
    //logFont.lfHeight = GUI_SECTION_FONT_SIZE * dy;
    HFONT hFontSection = CreateFontIndirect(&logFont);
    logFont.lfUnderline = 1;
    HFONT hFontSectionSel = CreateFontIndirect(&logFont);
    HFONT hOldFont = NULL;

    DTTOPTS DttOpts;
    DttOpts.dwSize = sizeof(DTTOPTS);
    DttOpts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
    //DttOpts.crText = GetSysColor(COLOR_WINDOWTEXT);
    DttOpts.crText = g_darkModeEnabled ? GUI_TEXTCOLOR_DARK : GUI_TEXTCOLOR;
    DWORD dwTextFlags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    RECT rcText;
    DWORD dwMaxHeight = 0, dwMaxWidth = 0;
    BOOL bTabOrderHit = FALSE;
    DWORD dwLeftPad = _this->padding.left + _this->sidebarWidth + _this->padding.right;
    DWORD dwInitialLeftPad = dwLeftPad;

    HDC hdcPaint = NULL;
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HPAINTBUFFER hBufferedPaint = BeginBufferedPaint(hDC, &rc, BPBF_TOPDOWNDIB, &params, &hdcPaint);

    if (!hDC || (hDC && hdcPaint))
    {
        if (!hDC)
        {
            hdcPaint = GetDC(hwnd);
        }

        if ((!IsThemeActive() || IsHighContrast()) && hDC)
        {
            COLORREF oldcr = SetBkColor(hdcPaint, GetSysColor(COLOR_3DFACE));
            ExtTextOutW(hdcPaint, 0, 0, ETO_OPAQUE, &rc, L"", 0, 0);
            SetBkColor(hdcPaint, oldcr);
            SetTextColor(hdcPaint, GetSysColor(COLOR_WINDOWTEXT));
            SetBkMode(hdcPaint, TRANSPARENT);
        }

        BOOL bResetLastHeading = TRUE;
        BOOL bWasSpecifiedSectionValid = FALSE;
        FILE* f = fmemopen(pRscr, cbRscr, "r");
        char* line = malloc(MAX_LINE_LENGTH * sizeof(char));
        wchar_t* text = malloc((MAX_LINE_LENGTH + 3) * sizeof(wchar_t)); 
        wchar_t* name = malloc(MAX_LINE_LENGTH * sizeof(wchar_t));
        wchar_t* section = malloc(MAX_LINE_LENGTH * sizeof(wchar_t));
        size_t bufsiz = MAX_LINE_LENGTH, numChRd = 0, tabOrder = 1, currentSection = -1, topAdj = 0;
        wchar_t* lastHeading = calloc(MAX_LINE_LENGTH, sizeof(wchar_t));
        while ((numChRd = getline(&line, &bufsiz, f)) != -1)
        {
            if (currentSection == _this->section)
            {
                bWasSpecifiedSectionValid = TRUE;
            }
            if (!strncmp(line, ";q", 2))
            {
                bResetLastHeading = TRUE;
                lastHeading[0] = 0;
                continue;
            }
            if (strcmp(line, "Windows Registry Editor Version 5.00\r\n") && 
                strcmp(line, "\r\n") && 
                (currentSection == -1 || currentSection == _this->section || !strncmp(line, ";T ", 3) || !strncmp(line, ";f", 2) || AuditFile) &&
                !((!IsThemeActive() || IsHighContrast()) && !strncmp(line, ";M ", 3))
                )
            {
#ifndef USE_PRIVATE_INTERFACES
                if (!strncmp(line, ";p ", 3))
                {
                    int num = atoi(line + 3);
                    for (int i = 0; i < num; ++i)
                    {
                        getline(&line, &bufsiz, f);
                    }
                }
#endif
                if (!strncmp(line, ";f", 2))
                {
                    //if (topAdj + ((currentSection + 2) * GUI_SECTION_HEIGHT * dy) > dwMaxHeight)
                    //{
                    //    dwMaxHeight = topAdj + ((currentSection + 2) * GUI_SECTION_HEIGHT * dy);
                    //}
                    if (_this->dwStatusbarY == 0)
                    {
                        //dwMaxHeight += GUI_STATUS_PADDING * dy;
                        _this->dwStatusbarY = dwMaxHeight / dy;
                    }
                    else
                    {
                        dwMaxHeight = _this->dwStatusbarY * dy;
                    }
                    currentSection = -1;
                    dwLeftPad = 0;
                    continue;
                }

                if (!strncmp(line, "[", 1))
                {
                    ZeroMemory(section, MAX_LINE_LENGTH * sizeof(wchar_t));
                    MultiByteToWideChar(
                        CP_UTF8,
                        MB_PRECOMPOSED,
                        line[1] == '-' ? line + 2 : line + 1,
                        numChRd - (line[1] == '-' ? 5 : 4),
                        section,
                        MAX_LINE_LENGTH
                    );
                    //wprintf(L"%s\n", section);
                }

                DWORD dwLineHeight = !strncmp(line, ";M ", 3) ? _this->GUI_CAPTION_LINE_HEIGHT : GUI_LINE_HEIGHT;
                DWORD dwBottom = _this->padding.bottom;
                DWORD dwTop = _this->padding.top;
                if (!strncmp(line, ";a ", 3) || !strncmp(line, ";e ", 3))
                {
                    dwBottom = 0;
                    dwLineHeight -= 0.2 * dwLineHeight;
                }

                rcText.left = dwLeftPad + _this->padding.left;
                rcText.top = !strncmp(line, ";M ", 3) ? 0 : (dwTop + dwMaxHeight);
                rcText.right = (rc.right - rc.left) - _this->padding.right;
                rcText.bottom = !strncmp(line, ";M ", 3) ? _this->GUI_CAPTION_LINE_HEIGHT * dy : (dwMaxHeight + dwLineHeight * dy - dwBottom);

                if (!strncmp(line, ";T ", 3))
                {
                    if (currentSection + 1 == _this->section)
                    {
                        hOldFont = SelectObject(hdcPaint, hFontSectionSel);
                    }
                    else
                    {
                        hOldFont = SelectObject(hdcPaint, hFontSection);
                    }
                    rcText.left = _this->padding.left;
                    rcText.right = _this->padding.left + _this->sidebarWidth;
                    rcText.top = topAdj + ((currentSection + 1) * GUI_SECTION_HEIGHT * dy);
                    rcText.bottom = topAdj + ((currentSection + 2) * GUI_SECTION_HEIGHT * dy);
                    ZeroMemory(text, (MAX_LINE_LENGTH + 3) * sizeof(wchar_t));
                    MultiByteToWideChar(
                        CP_UTF8,
                        MB_PRECOMPOSED,
                        line + 3,
                        numChRd - 3,
                        text,
                        MAX_LINE_LENGTH
                    );
                    if (_this->sectionNames[currentSection + 1][0] == 0)
                    {
                        wcscpy_s(_this->sectionNames[currentSection + 1], 20, text);
                    }
                    if (hDC)
                    {
                        if (IsThemeActive() && !IsHighContrast())
                        {
                            DrawThemeTextEx(
                                _this->hTheme,
                                hdcPaint,
                                0,
                                0,
                                text,
                                -1,
                                dwTextFlags,
                                &rcText,
                                &DttOpts
                            );
                        }
                        else
                        {
                            DrawTextW(
                                hdcPaint,
                                text,
                                -1,
                                &rcText,
                                dwTextFlags
                            );
                        }
                    }
                    else
                    {
                        RECT rcTemp;
                        rcTemp = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            -1,
                            &rcTemp,
                            DT_CALCRECT
                        );
                        rcTemp.bottom = rcText.bottom;
                        if (PtInRect(&rcTemp, pt))
                        {
                            _this->bShouldAnnounceSelected = TRUE;
                            _this->bRebuildIfTabOrderIsEmpty = FALSE;
                            _this->tabOrder = 0;
                            GUI_SetSection(_this, TRUE, currentSection + 1);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    currentSection++;
                    continue;
                }
                else if (!strncmp(line, ";M ", 3))
                {
                    UINT diff = (((_this->GUI_CAPTION_LINE_HEIGHT - 16) * dx) / 2.0);
                    rcText.left = diff + (int)(16.0 * dx) + diff / 2;
                    topAdj = dwMaxHeight + _this->GUI_CAPTION_LINE_HEIGHT * dy;
                    hOldFont = SelectObject(hdcPaint, hFontCaption);
                }
                else if (!strncmp(line, ";u ", 3) || (!strncmp(line, ";y ", 3) && !strstr(line, "\xF0\x9F")))
                {
                    hOldFont = SelectObject(hdcPaint, hFontUnderline);
                }
                else
                {
                    hOldFont = SelectObject(hdcPaint, hFontRegular);
                }

                if (!strncmp(line, ";e ", 3) || !strncmp(line, ";a ", 3) || !strncmp(line, ";T ", 3) || !strncmp(line, ";t ", 3) || !strncmp(line, ";u ", 3) || !strncmp(line, ";M ", 3))
                {
                    if (!strncmp(line, ";t ", 3) || !strncmp(line, ";e ", 3) || !strncmp(line, ";a ", 3))
                    {
                        char* p = strstr(line, "%VERSIONINFORMATIONSTRING%");
                        if (p)
                        {
                            DWORD dwLeftMost = 0;
                            DWORD dwSecondLeft = 0;
                            DWORD dwSecondRight = 0;
                            DWORD dwRightMost = 0;

                            QueryVersionInfo(hModule, VS_VERSION_INFO, &dwLeftMost, &dwSecondLeft, &dwSecondRight, &dwRightMost);

                            sprintf_s(p, MAX_PATH, "%d.%d.%d.%d%s", dwLeftMost, dwSecondLeft, dwSecondRight, dwRightMost, 
#if defined(DEBUG) | defined(_DEBUG)
                                " (Debug)"
#else
                                ""
#endif
                                );
                        }
                    }
                    ZeroMemory(text, (MAX_LINE_LENGTH + 3) * sizeof(wchar_t));
                    MultiByteToWideChar(
                        CP_UTF8,
                        MB_PRECOMPOSED,
                        line + 3,
                        numChRd - 3,
                        text,
                        MAX_LINE_LENGTH
                    );
                    if (bResetLastHeading)
                    {
                        wcscpy_s(lastHeading, MAX_LINE_LENGTH, text);
                        bResetLastHeading = FALSE;
                    }
                    else
                    {
                        wcscat_s(lastHeading, MAX_LINE_LENGTH, L" ");
                        wcscat_s(lastHeading, MAX_LINE_LENGTH, text);
                    }
                    if (!strncmp(line, ";a ", 3))
                    {
                        RECT rcTemp;
                        rcTemp = rcText;
                        DrawTextW(
                            hdcPaint,
                            L"\u2795  ",
                            3,
                            &rcTemp,
                            DT_CALCRECT
                        );
                        rcText.left += rcTemp.right - rcTemp.left;
                        rcText.right += rcTemp.right - rcTemp.left;
                    }
                    if (!strncmp(line, ";M ", 3))
                    {
                        if (hDC)
                        {
                            UINT diff = (int)(((_this->GUI_CAPTION_LINE_HEIGHT - 16) * dx) / 2.0);
                            //printf("!!! %d %d\n", (int)(16.0 * dx), diff);
                            DrawIconEx(
                                hdcPaint,
                                diff,
                                diff,
                                _this->hIcon,
                                (int)(16.0 * dx),
                                (int)(16.0 * dy),
                                0,
                                NULL,
                                DI_NORMAL
                            );
                        }

                        TCHAR exeName[MAX_PATH + 1];
                        GetProcessImageFileNameW(
                            OpenProcess(
                                PROCESS_QUERY_INFORMATION,
                                FALSE,
                                GetCurrentProcessId()
                            ),
                            exeName,
                            MAX_PATH
                        );
                        PathStripPath(exeName);
                        //if (wcscmp(exeName, L"explorer.exe"))
                        //{
                        //    LoadStringW(hModule, IDS_PRODUCTNAME, text, MAX_LINE_LENGTH);
                        //}
                        //else
                        //{
                            //HMODULE hExplorerFrame = LoadLibraryExW(L"ExplorerFrame.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
                            LoadStringW(_this->hExplorerFrame, 50222, text, 260);
                            //FreeLibrary(hExplorerFrame);
                            wchar_t* p = wcschr(text, L'(');
                            if (p)
                            {
                                p--;
                                if (*p == L' ')
                                {
                                    *p = 0;
                                }
                                else
                                {
                                    p++;
                                    *p = 0;
                                }
                            }
                        //}
                        //rcText.bottom += _this->GUI_CAPTION_LINE_HEIGHT - dwLineHeight;
                        dwLineHeight = _this->GUI_CAPTION_LINE_HEIGHT;
                        _this->extent.cyTopHeight = rcText.bottom;
                    }
                    if (hDC)
                    {
                        COLORREF cr;
                        if (!strncmp(line, ";u ", 3) && tabOrder == _this->tabOrder)
                        {
                            bTabOrderHit = TRUE;
                            if (!IsThemeActive() || IsHighContrast())
                            {
                                cr = SetTextColor(hdcPaint, GetSysColor(COLOR_HIGHLIGHT));
                            }
                            else
                            {
                                DttOpts.crText = g_darkModeEnabled ? GUI_TEXTCOLOR_SELECTED_DARK : GUI_TEXTCOLOR_SELECTED;
                                //DttOpts.crText = GetSysColor(COLOR_HIGHLIGHT);
                            }
                            if (_this->bShouldAnnounceSelected)
                            {
                                WCHAR accText[1000];
                                swprintf_s(
                                    accText, 
                                    1000, 
                                    L"%s %s - Button.",
                                    (_this->dwPageLocation < 0 ?
                                    L"Reached end of the page." :
                                    (_this->dwPageLocation > 0 ?
                                    L"Reached beginning of the page." : L"")),
                                    text
                                );
                                _this->dwPageLocation = 0;
                                for (unsigned int i = 0; i < wcslen(accText) - 2; ++i)
                                {
                                    if (accText[i] == L'(' && accText[i + 1] == L'*' && accText[i + 2] == L')')
                                    {
                                        accText[i] = L' ';
                                        accText[i + 1] = L' ';
                                        accText[i + 2] = L' ';
                                    }
                                }
                                SetWindowTextW(_this->hAccLabel, accText);
                                NotifyWinEvent(
                                    EVENT_OBJECT_LIVEREGIONCHANGED,
                                    _this->hAccLabel,
                                    OBJID_CLIENT,
                                    CHILDID_SELF);
                                _this->bShouldAnnounceSelected = FALSE;
                            }
                        }
                        RECT rcNew = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            -1,
                            &rcNew,
                            DT_CALCRECT
                        );
                        if (rcNew.right - rcNew.left > dwMaxWidth)
                        {
                            dwMaxWidth = rcNew.right - rcNew.left + 50 * dx;
                        }
                        if (IsThemeActive() && !IsHighContrast())
                        {
                            DrawThemeTextEx(
                                _this->hTheme,
                                hdcPaint,
                                hOldFont ? 0 : 8,
                                0,
                                text,
                                -1,
                                dwTextFlags,
                                &rcText,
                                &DttOpts
                            );
                        }
                        else
                        {
                            DrawTextW(
                                hdcPaint,
                                text,
                                -1,
                                &rcText,
                                dwTextFlags
                            );
                        }
                        if (!strncmp(line, ";u ", 3) && tabOrder == _this->tabOrder)
                        {
                            if (!IsThemeActive() || IsHighContrast())
                            {
                                SetTextColor(hdcPaint, cr);
                            }
                            else
                            {
                                DttOpts.crText = g_darkModeEnabled ? GUI_TEXTCOLOR_DARK : GUI_TEXTCOLOR;
                                //DttOpts.crText = GetSysColor(COLOR_WINDOWTEXT);
                            }
                        }
                    }
                    else
                    {
                        RECT rcTemp;
                        rcTemp = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            -1,
                            &rcTemp,
                            DT_CALCRECT
                        );
                        rcTemp.bottom = rcText.bottom;
                        if (!strncmp(line, ";u ", 3) && (PtInRect(&rcTemp, pt) || (pt.x == 0 && pt.y == 0 && tabOrder == _this->tabOrder)))
                        {
                            numChRd = getline(&line, &bufsiz, f);
                            char* p = strchr(line, '\r');
                            if (p) *p = 0;
                            p = strchr(line, '\n');
                            if (p) *p = 0;
                            if (!strncmp(line + 1, "restart", 7))
                            {
                                HWND hShellTrayWnd = FindWindowW(L"Shell_TrayWnd", NULL);
                                if (hShellTrayWnd)
                                {
                                    HANDLE hEvent = NULL;
                                    if (GetAsyncKeyState(VK_SHIFT))
                                    {
                                        hEvent = CreateEventW(NULL, FALSE, FALSE, _T(EP_SETUP_EVENTNAME));
                                    }
                                    WCHAR wszPath[MAX_PATH];
                                    ZeroMemory(wszPath, MAX_PATH * sizeof(WCHAR));
                                    PDWORD_PTR res = -1;
                                    if (!SendMessageTimeoutW(hShellTrayWnd, 1460, 0, 0, SMTO_ABORTIFHUNG, 2000, &res) && res)
                                    {
                                        HANDLE hExplorerRestartThread = CreateThread(NULL, 0, BeginExplorerRestart, NULL, 0, NULL);
                                        if (hExplorerRestartThread)
                                        {
                                            WaitForSingleObject(hExplorerRestartThread, 2000);
                                            CloseHandle(hExplorerRestartThread);
                                            hExplorerRestartThread = NULL;
                                        }
                                        else
                                        {
                                            BeginExplorerRestart();
                                        }
                                    }
                                    Sleep(100);
                                    GetSystemDirectoryW(wszPath, MAX_PATH);
                                    wcscat_s(wszPath, MAX_PATH, L"\\taskkill.exe");
                                    SHELLEXECUTEINFOW sei;
                                    ZeroMemory(&sei, sizeof(SHELLEXECUTEINFOW));
                                    sei.cbSize = sizeof(sei);
                                    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                                    sei.hwnd = NULL;
                                    sei.hInstApp = NULL;
                                    sei.lpVerb = NULL;
                                    sei.lpFile = wszPath;
                                    sei.lpParameters = L"/f /im explorer.exe";
                                    sei.hwnd = NULL;
                                    sei.nShow = SW_SHOWMINIMIZED;
                                    if (ShellExecuteExW(&sei) && sei.hProcess)
                                    {
                                        WaitForSingleObject(sei.hProcess, INFINITE);
                                        CloseHandle(sei.hProcess);
                                    }
                                    GetWindowsDirectoryW(wszPath, MAX_PATH);
                                    wcscat_s(wszPath, MAX_PATH, L"\\explorer.exe");
                                    Sleep(1000);
                                    GUI_RegSetValueExW(NULL, L"Virtualized_" _T(EP_CLSID) L"_TaskbarPosition", NULL, NULL, &dwTaskbarPosition, NULL);
                                    if (hEvent)
                                    {
                                        CloseHandle(hEvent);
                                    }
                                    ShellExecuteW(
                                        NULL,
                                        L"open",
                                        wszPath,
                                        NULL,
                                        NULL,
                                        SW_SHOWNORMAL
                                    );
                                }
                                else
                                {
                                    StartExplorer();
                                }
                            }
                            else if (!strncmp(line + 1, "reset", 5))
                            {
                                wchar_t wszPath[MAX_PATH];
                                ZeroMemory(
                                    wszPath,
                                    MAX_PATH * sizeof(wchar_t)
                                );
                                SHGetFolderPathW(
                                    NULL,
                                    SPECIAL_FOLDER_LEGACY,
                                    NULL,
                                    SHGFP_TYPE_CURRENT,
                                    wszPath
                                );
                                wcscat_s(
                                    wszPath,
                                    MAX_PATH,
                                    TEXT(APP_RELATIVE_PATH)
                                );
                                CreateDirectoryW(wszPath, NULL);
                                wcscat_s(
                                    wszPath,
                                    MAX_PATH,
                                    L"\\settings.reg"
                                );
                                wprintf(L"%s\n", wszPath);
                                HANDLE hFile = CreateFileW(
                                    wszPath,
                                    GENERIC_WRITE,
                                    0,
                                    NULL,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    NULL
                                );
                                if (hFile)
                                {
                                    void* buffer = NULL;
                                    HKEY hKey = NULL;
                                    RegOpenKeyExW(
                                        HKEY_LOCAL_MACHINE,
                                        L"Software\\Classes\\CLSID\\" _T(EP_CLSID) L"\\InprocServer32",
                                        REG_OPTION_NON_VOLATILE,
                                        KEY_READ | KEY_WOW64_64KEY,
                                        &hKey
                                    );
                                    buffer = pRscr;
                                    DWORD dwNumberOfBytesWritten = 0;
                                    if (WriteFile(
                                        hFile,
                                        buffer,
                                        cbRscr,
                                        &dwNumberOfBytesWritten,
                                        NULL
                                    ))
                                    {
                                        CloseHandle(hFile);
                                        DWORD dwOldTaskbarOld = 0, dwOldTaskbar = 0, dwSize = sizeof(DWORD);
                                        RegGetValueW(HKEY_CURRENT_USER, _T(REGPATH), L"OldTaskbar", RRF_RT_DWORD, NULL, &dwOldTaskbarOld, &dwSize);
                                        RegSetKeyValueW(HKEY_CURRENT_USER, _T(REGPATH), L"OldTaskbar", REG_DWORD, &dwOldTaskbar, sizeof(DWORD));

                                        DWORD dwError = 0;
                                        // https://stackoverflow.com/questions/50298722/win32-launching-a-highestavailable-child-process-as-a-normal-user-process
                                        if (pvRtlQueryElevationFlags = GetProcAddress(GetModuleHandleW(L"ntdll"), "RtlQueryElevationFlags"))
                                        {
                                            PVOID pv;
                                            if (pv = AddVectoredExceptionHandler(TRUE, OnVex))
                                            {
                                                CONTEXT ctx;
                                                ZeroMemory(&ctx, sizeof(CONTEXT));
                                                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                                                ctx.Dr7 = 0x404;
                                                ctx.Dr1 = (ULONG_PTR)pvRtlQueryElevationFlags;

                                                if (SetThreadContext(GetCurrentThread(), &ctx))
                                                {
                                                    WCHAR wszExec[MAX_PATH * 2];
                                                    ZeroMemory(wszExec, MAX_PATH * 2 * sizeof(WCHAR));
                                                    wszExec[0] = L'"';
                                                    GetWindowsDirectoryW(wszExec + 1, MAX_PATH);
                                                    wcscat_s(wszExec, MAX_PATH * 2, L"\\regedit.exe\" \"");
                                                    wcscat_s(wszExec, MAX_PATH * 2, wszPath);
                                                    wcscat_s(wszExec, MAX_PATH * 2, L"\"");
                                                    STARTUPINFO si;
                                                    ZeroMemory(&si, sizeof(STARTUPINFO));
                                                    si.cb = sizeof(STARTUPINFO);
                                                    PROCESS_INFORMATION pi;
                                                    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
                                                    wprintf(L"%s\n", wszExec);
                                                    if (CreateProcessW(NULL, wszExec, 0, 0, 0, 0, 0, 0, &si, &pi))
                                                    {
                                                        CloseHandle(pi.hThread);
                                                        //CloseHandle(pi.hProcess);
                                                    }
                                                    else
                                                    {
                                                        dwError = GetLastError();
                                                    }

                                                    ctx.Dr7 = 0x400;
                                                    ctx.Dr1 = 0;
                                                    SetThreadContext(GetCurrentThread(), &ctx);

                                                    if (pi.hProcess)
                                                    {
                                                        WaitForSingleObject(pi.hProcess, INFINITE);
                                                        DWORD dwExitCode = 0;
                                                        GetExitCodeProcess(pi.hProcess, &dwExitCode);
                                                        CloseHandle(pi.hProcess);
                                                    }
                                                }
                                                else
                                                {
                                                    dwError = GetLastError();
                                                }
                                                RemoveVectoredExceptionHandler(pv);
                                            }
                                            else
                                            {
                                                dwError = GetLastError();
                                            }
                                        }
                                        else
                                        {
                                            dwError = GetLastError();
                                        }

                                        dwSize = sizeof(DWORD);
                                        RegGetValueW(HKEY_CURRENT_USER, _T(REGPATH), L"OldTaskbar", RRF_RT_DWORD, NULL, &dwOldTaskbar, &dwSize);
                                        if (dwOldTaskbar == 1)
                                        {
                                            FILE* vf = NULL;
                                            _wfopen_s(&vf, wszPath, L"r");
                                            if (vf)
                                            {
                                                char* line2 = malloc(MAX_LINE_LENGTH * sizeof(char));
                                                if (line2)
                                                {
                                                    int numChRd2 = 0;
                                                    size_t bufsiz2 = MAX_LINE_LENGTH;
                                                    while ((numChRd2 = getline(&line2, &bufsiz2, vf)) != -1)
                                                    {
                                                        if (!strncmp(line2, ";\"Virtualized_" EP_CLSID, 52))
                                                        {
                                                            DWORD dwVal = 0;
                                                            WCHAR wszName[MAX_PATH];
                                                            ZeroMemory(wszName, MAX_PATH * sizeof(wchar_t));
                                                            MultiByteToWideChar(
                                                                CP_UTF8,
                                                                MB_PRECOMPOSED,
                                                                line2 + 2,
                                                                numChRd2 - 2,
                                                                wszName,
                                                                MAX_PATH
                                                            );
                                                            wchar_t* ddd = wcschr(wszName, L'=');
                                                            if (ddd) *ddd = 0;
                                                            wchar_t* ppp = wcschr(wszName, L'"');
                                                            if (ppp) *ppp = 0;
                                                            if (!wcsncmp(ddd + 1, L"dword:", 6))
                                                            {
                                                                wchar_t* xxx = wcschr(ddd + 1, L':');
                                                                xxx++;
                                                                dwVal = wcstol(xxx, NULL, 16);
                                                                wprintf(L"%s %d\n", wszName, dwVal);
                                                                GUI_RegSetValueExW(NULL, wszName, 0, RRF_RT_DWORD, &dwVal, sizeof(DWORD));
                                                            }
                                                        }
                                                    }
                                                    free(line2);
                                                }
                                                fclose(vf);
                                            }
                                        }
                                        else
                                        {
                                            RegSetKeyValueW(HKEY_CURRENT_USER, _T(REGPATH), L"OldTaskbar", REG_DWORD, &dwOldTaskbarOld, sizeof(DWORD));
                                        }

                                        _this->tabOrder = 0;
                                        InvalidateRect(hwnd, NULL, FALSE);
                                        DeleteFileW(wszPath);
                                    }
                                }
                            }
                            else if (!strncmp(line + 1, "about", 5))
                            {
                                DWORD dwLeftMost = 0;
                                DWORD dwSecondLeft = 0;
                                DWORD dwSecondRight = 0;
                                DWORD dwRightMost = 0;

                                QueryVersionInfo(hModule, VS_VERSION_INFO, &dwLeftMost, &dwSecondLeft, &dwSecondRight, &dwRightMost);

                                TCHAR wszIDS_VISITGITHUB[100];
                                LoadString(hModule, IDS_VISITGITHUB, wszIDS_VISITGITHUB, 100);
                                TCHAR wszIDS_VISITWEBSITE[100];
                                LoadString(hModule, IDS_VISITWEBSITE, wszIDS_VISITWEBSITE, 100);
                                TCHAR wszIDS_LICENSEINFO[100];
                                LoadString(hModule, IDS_LICENSEINFO, wszIDS_LICENSEINFO, 100);
                                TCHAR wszIDS_PRODUCTNAME[100];
                                LoadString(hModule, IDS_PRODUCTNAME, wszIDS_PRODUCTNAME, 100);
                                TCHAR wszIDS_VERSION[100];
                                LoadString(hModule, IDS_VERSION, wszIDS_VERSION, 100);
                                TCHAR wszIDS_PRODUCTTAG[406];
                                wsprintf(wszIDS_PRODUCTTAG, wszIDS_VERSION, dwLeftMost, dwSecondLeft, dwSecondRight, dwRightMost);
                                wcscat_s(
                                    wszIDS_PRODUCTTAG,
                                    406,
                                    L"\r\n"
                                );
                                LoadString(hModule, IDS_COPYRIGHT, wszIDS_PRODUCTTAG + wcslen(wszIDS_PRODUCTTAG), 100);
                                wcscat_s(
                                    wszIDS_PRODUCTTAG,
                                    406,
                                    L"\r\n\r\n"
                                );
                                LoadString(hModule, IDS_PRODUCTTAG, wszIDS_PRODUCTTAG + wcslen(wszIDS_PRODUCTTAG), 200);

                                TASKDIALOG_BUTTON buttons[3];
                                buttons[0].nButtonID = IDS_VISITGITHUB;
                                buttons[0].pszButtonText = wszIDS_VISITGITHUB;
                                buttons[1].nButtonID = IDS_VISITWEBSITE;
                                buttons[1].pszButtonText = wszIDS_VISITWEBSITE;
                                buttons[2].nButtonID = IDS_LICENSEINFO;
                                buttons[2].pszButtonText = wszIDS_LICENSEINFO;

                                TASKDIALOGCONFIG td;
                                ZeroMemory(&td, sizeof(TASKDIALOGCONFIG));
                                td.cbSize = sizeof(TASKDIALOGCONFIG);
                                td.hwndParent = hwnd;
                                td.hInstance = hModule;
                                td.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_USE_COMMAND_LINKS;
                                td.dwCommonButtons = TDCBF_OK_BUTTON;
                                td.pszWindowTitle = L" ";
                                td.pszMainIcon = TD_INFORMATION_ICON;
                                td.pszMainInstruction = wszIDS_PRODUCTNAME;
                                td.pszContent = wszIDS_PRODUCTTAG;
                                td.cButtons = sizeof(buttons) / sizeof(buttons[0]);
                                td.pButtons = buttons;
                                td.nDefaultButton = IDOK;
                                td.cRadioButtons = 0;
                                td.pRadioButtons = NULL;
                                td.cxWidth = 0;
                                td.pszFooter = L"";
                                td.pfCallback = GUI_AboutProc;
                                td.lpCallbackData = 0;
                                int ret;

                                // If used directly, StartMenuExperienceHost.exe crashes badly and is unable to start; guess how I know...
                                (HRESULT(*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*))(GetProcAddress(GetModuleHandleA("Comctl32.dll"), "TaskDialogIndirect"))(
                                    &td,
                                    &ret,
                                    NULL,
                                    NULL
                                );
                            }
                            else if (!strncmp(line + 1, "export", 6))
                            {
                                WCHAR title[MAX_PATH];
                                WCHAR filter[MAX_PATH];
                                WCHAR wszRegedit[MAX_PATH];
                                GetWindowsDirectoryW(wszRegedit, MAX_PATH);
                                wcscat_s(wszRegedit, MAX_PATH, L"\\regedit.exe");
                                HMODULE hRegedit = LoadLibraryExW(wszRegedit, NULL, LOAD_LIBRARY_AS_DATAFILE);
                                if (hRegedit)
                                {
                                    LoadStringW(hRegedit, 301, title, MAX_PATH);
                                    LoadStringW(hRegedit, 302, filter, MAX_PATH);
                                    unsigned int j = 0;
                                    for (unsigned int i = 0; i < MAX_PATH; ++i)
                                    {
                                        if (filter[i] == L'#')
                                        {
                                            filter[i] = L'\0';
                                            j++;
                                            if (j == 2)
                                            {
                                                filter[i + 1] = L'\0';
                                                break;
                                            }
                                        }
                                    }
                                    FreeLibrary(hRegedit);
                                }
                                else
                                {
                                    wcscpy_s(title, MAX_PATH, L"Export settings");
                                    wcscpy_s(filter, MAX_PATH, L"Registration Files (*.reg)\0*.reg\0\0");
                                }
                                WCHAR wszPath[MAX_PATH];
                                ZeroMemory(wszPath, MAX_PATH * sizeof(WCHAR));
                                DWORD dwLeftMost = 0;
                                DWORD dwSecondLeft = 0;
                                DWORD dwSecondRight = 0;
                                DWORD dwRightMost = 0;
                                QueryVersionInfo(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), VS_VERSION_INFO, &dwLeftMost, &dwSecondLeft, &dwSecondRight, &dwRightMost);
                                swprintf_s(wszPath, MAX_PATH, _T(PRODUCT_NAME) L"_%d.%d.%d.%d.reg", dwLeftMost, dwSecondLeft, dwSecondRight, dwRightMost);
                                OPENFILENAMEW ofn;
                                ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
                                ofn.lStructSize = sizeof(OPENFILENAMEW);
                                ofn.hwndOwner = hwnd;
                                ofn.hInstance = GetModuleHandleW(NULL);
                                ofn.lpstrFilter = filter;
                                ofn.lpstrCustomFilter = NULL;
                                ofn.nMaxCustFilter = 0;
                                ofn.nFilterIndex = 1;
                                ofn.lpstrFile = wszPath;
                                ofn.nMaxFile = MAX_PATH;
                                ofn.lpstrFileTitle = NULL;
                                ofn.nMaxFileTitle = 0;
                                ofn.lpstrInitialDir = NULL;
                                ofn.lpstrTitle = title;
                                ofn.Flags = OFN_DONTADDTORECENT | OFN_CREATEPROMPT | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
                                ofn.nFileOffset = 0;
                                ofn.nFileExtension = 0;
                                ofn.lpstrDefExt = L"reg";
                                ofn.lCustData = NULL;
                                ofn.lpfnHook = NULL;
                                ofn.lpTemplateName = NULL;
                                if (GetSaveFileNameW(&ofn))
                                {
                                    _wfopen_s(&AuditFile, wszPath, L"w");
                                    if (AuditFile)
                                    {
                                        fwprintf(AuditFile, L"Windows Registry Editor Version 5.00\n\n[HKEY_CURRENT_USER\\Software\\ExplorerPatcher]\n\"ImportOK\"=dword:00000001\n");
                                        POINT pt;
                                        pt.x = 0;
                                        pt.y = 0;
                                        GUI_Build(0, hwnd, pt);
                                        fclose(AuditFile);
                                        AuditFile = NULL;
                                        MessageBoxW(hwnd, L"Settings have been exported successfully.", GUI_title, MB_ICONINFORMATION);
                                    }
                                }
                            }
                            else if (!strncmp(line + 1, "import", 6))
                            {
                                WCHAR title[MAX_PATH];
                                WCHAR filter[MAX_PATH];
                                WCHAR wszRegedit[MAX_PATH];
                                GetWindowsDirectoryW(wszRegedit, MAX_PATH);
                                wcscat_s(wszRegedit, MAX_PATH, L"\\regedit.exe");
                                HMODULE hRegedit = LoadLibraryExW(wszRegedit, NULL, LOAD_LIBRARY_AS_DATAFILE);
                                if (hRegedit)
                                {
                                    LoadStringW(hRegedit, 300, title, MAX_PATH);
                                    LoadStringW(hRegedit, 302, filter, MAX_PATH);
                                    unsigned j = 0;
                                    for (unsigned int i = 0; i < MAX_PATH; ++i)
                                    {
                                        if (filter[i] == L'#')
                                        {
                                            filter[i] = L'\0';
                                            j++;
                                            if (j == 2)
                                            {
                                                filter[i + 1] = L'\0';
                                                break;
                                            }
                                        }
                                    }
                                    FreeLibrary(hRegedit);
                                }
                                else
                                {
                                    wcscpy_s(title, MAX_PATH, L"Import settings");
                                    wcscpy_s(filter, MAX_PATH, L"Registration Files (*.reg)\0*.reg\0\0");
                                }
                                WCHAR wszPath[MAX_PATH];
                                ZeroMemory(wszPath, MAX_PATH * sizeof(WCHAR));
                                DWORD dwLeftMost = 0;
                                DWORD dwSecondLeft = 0;
                                DWORD dwSecondRight = 0;
                                DWORD dwRightMost = 0;
                                QueryVersionInfo(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), VS_VERSION_INFO, &dwLeftMost, &dwSecondLeft, &dwSecondRight, &dwRightMost);
                                swprintf_s(wszPath, MAX_PATH, _T(PRODUCT_NAME) L"_%d.%d.%d.%d.reg", dwLeftMost, dwSecondLeft, dwSecondRight, dwRightMost);
                                OPENFILENAMEW ofn;
                                ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
                                ofn.lStructSize = sizeof(OPENFILENAMEW);
                                ofn.hwndOwner = hwnd;
                                ofn.hInstance = GetModuleHandleW(NULL);
                                ofn.lpstrFilter = filter;
                                ofn.lpstrCustomFilter = NULL;
                                ofn.nMaxCustFilter = 0;
                                ofn.nFilterIndex = 1;
                                ofn.lpstrFile = wszPath;
                                ofn.nMaxFile = MAX_PATH;
                                ofn.lpstrFileTitle = NULL;
                                ofn.nMaxFileTitle = 0;
                                ofn.lpstrInitialDir = NULL;
                                ofn.lpstrTitle = title;
                                ofn.Flags = OFN_DONTADDTORECENT | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_FILEMUSTEXIST;
                                ofn.nFileOffset = 0;
                                ofn.nFileExtension = 0;
                                ofn.lpstrDefExt = L"reg";
                                ofn.lCustData = NULL;
                                ofn.lpfnHook = NULL;
                                ofn.lpTemplateName = NULL;
                                if (GetOpenFileNameW(&ofn))
                                {
                                    RegDeleteKeyValueW(HKEY_CURRENT_USER, _T(REGPATH), L"ImportOK");

                                    DWORD dwError = 0;
                                    // https://stackoverflow.com/questions/50298722/win32-launching-a-highestavailable-child-process-as-a-normal-user-process
                                    if (pvRtlQueryElevationFlags = GetProcAddress(GetModuleHandleW(L"ntdll"), "RtlQueryElevationFlags"))
                                    {
                                        PVOID pv;
                                        if (pv = AddVectoredExceptionHandler(TRUE, OnVex))
                                        {
                                            CONTEXT ctx;
                                            ZeroMemory(&ctx, sizeof(CONTEXT));
                                            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                                            ctx.Dr7 = 0x404;
                                            ctx.Dr1 = (ULONG_PTR)pvRtlQueryElevationFlags;

                                            if (SetThreadContext(GetCurrentThread(), &ctx))
                                            {
                                                WCHAR wszExec[MAX_PATH * 2];
                                                ZeroMemory(wszExec, MAX_PATH * 2 * sizeof(WCHAR));
                                                wszExec[0] = L'"';
                                                GetWindowsDirectoryW(wszExec + 1, MAX_PATH);
                                                wcscat_s(wszExec, MAX_PATH * 2, L"\\regedit.exe\" \"");
                                                wcscat_s(wszExec, MAX_PATH * 2, wszPath);
                                                wcscat_s(wszExec, MAX_PATH * 2, L"\"");
                                                STARTUPINFO si;
                                                ZeroMemory(&si, sizeof(STARTUPINFO));
                                                si.cb = sizeof(STARTUPINFO);
                                                PROCESS_INFORMATION pi;
                                                ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
                                                wprintf(L"%s\n", wszExec);
                                                if (CreateProcessW(NULL, wszExec, 0, 0, 0, 0, 0, 0, &si, &pi))
                                                {
                                                    CloseHandle(pi.hThread);
                                                    //CloseHandle(pi.hProcess);
                                                }
                                                else
                                                {
                                                    dwError = GetLastError();
                                                }

                                                ctx.Dr7 = 0x400;
                                                ctx.Dr1 = 0;
                                                SetThreadContext(GetCurrentThread(), &ctx);

                                                if (pi.hProcess)
                                                {
                                                    WaitForSingleObject(pi.hProcess, INFINITE);
                                                    DWORD dwExitCode = 0;
                                                    GetExitCodeProcess(pi.hProcess, &dwExitCode);
                                                    CloseHandle(pi.hProcess);
                                                }
                                            }
                                            else
                                            {
                                                dwError = GetLastError();
                                            }
                                            RemoveVectoredExceptionHandler(pv);
                                        }
                                        else
                                        {
                                            dwError = GetLastError();
                                        }
                                    }
                                    else
                                    {
                                        dwError = GetLastError();
                                    }

                                    DWORD dwData = 0, dwSize = sizeof(DWORD);
                                    RegGetValueW(HKEY_CURRENT_USER, _T(REGPATH), L"ImportOK", RRF_RT_DWORD, NULL, &dwData, &dwSize);
                                    if (dwData)
                                    {
                                        RegDeleteKeyValueW(HKEY_CURRENT_USER, _T(REGPATH), L"ImportOK");

                                        FILE* vf = NULL;
                                        _wfopen_s(&vf, wszPath, L"r");
                                        if (vf)
                                        {
                                            char* line2 = malloc(MAX_LINE_LENGTH * sizeof(char));
                                            if (line2)
                                            {
                                                int numChRd2 = 0;
                                                size_t bufsiz2 = MAX_LINE_LENGTH;
                                                while ((numChRd2 = getline(&line2, &bufsiz2, vf)) != -1)
                                                {
                                                    if (!strncmp(line2, ";\"Virtualized_" EP_CLSID, 52))
                                                    {
                                                        DWORD dwVal = 0;
                                                        WCHAR wszName[MAX_PATH];
                                                        ZeroMemory(wszName, MAX_PATH * sizeof(wchar_t));
                                                        MultiByteToWideChar(
                                                            CP_UTF8,
                                                            MB_PRECOMPOSED,
                                                            line2 + 2,
                                                            numChRd2 - 2,
                                                            wszName,
                                                            MAX_PATH
                                                        );
                                                        wchar_t* ddd = wcschr(wszName, L'=');
                                                        if (ddd) *ddd = 0;
                                                        wchar_t* ppp = wcschr(wszName, L'"');
                                                        if (ppp) *ppp = 0;
                                                        if (!wcsncmp(ddd + 1, L"dword:", 6))
                                                        {
                                                            wchar_t* xxx = wcschr(ddd + 1, L':');
                                                            xxx++;
                                                            dwVal = wcstol(xxx, NULL, 16);
                                                            wprintf(L"%s %d\n", wszName, dwVal);
                                                            GUI_RegSetValueExW(NULL, wszName, 0, RRF_RT_DWORD, &dwVal, sizeof(DWORD));
                                                        }
                                                        else
                                                        {
                                                            //WCHAR* wszTitle = malloc(MAX_LINE_LENGTH * sizeof(WCHAR));
                                                            //wchar_t* x = wcschr(ddd + 2, L'"');
                                                            //x[0] = 0;
                                                            //wprintf(L">>> %s\n", ddd + 2);
                                                        }
                                                    }
                                                }
                                                free(line2);
                                            }
                                            fclose(vf);
                                        }
                                    }
                                }
                            }
                            else if (!strncmp(line + 1, "update_weather", 14))
                            {
                                PostMessageW(FindWindowW(_T(EPW_WEATHER_CLASSNAME), NULL), EP_WEATHER_WM_FETCH_DATA, 0, 0);
                            }
                            else if (!strncmp(line + 1, "clear_data_weather", 18))
                            {
                                if (MessageBoxW(
                                    hwnd,
                                    L"Are you sure you want to permanently clear the weather widget's local data?\n\n"
                                    L"This will reset the internal components to their default state, but will preserve "
                                    L"your preferences. This may fix the widget not loading the data properly, or "
                                    L"having layout issues etc.",
                                    _T(PRODUCT_NAME),
                                    MB_ICONQUESTION | MB_YESNO) == IDYES)
                                {
                                    DWORD dwData = 0, dwVal = 0, dwSize = sizeof(DWORD);
                                    GUI_Internal_RegQueryValueExW(NULL, L"Virtualized_" _T(EP_CLSID) L"_PeopleBand", NULL, NULL, &dwData, &dwSize);
                                    int res = 0;
                                    if (dwData)
                                    {
                                        GUI_Internal_RegSetValueExW(NULL, L"Virtualized_" _T(EP_CLSID) L"_PeopleBand", 0, 0, &dwVal, sizeof(DWORD));
                                        PleaseWaitTimeout = 100;
                                        PleaseWaitCallbackData = &res;
                                        PleaseWaitCallbackFunc = GUI_Internal_DeleteWeatherFolder;
                                        PleaseWaitHook = SetWindowsHookExW(WH_CALLWNDPROC, PleaseWait_HookProc, NULL, GetCurrentThreadId());
                                        MessageBoxW(hwnd, L"Please wait...", _T(PRODUCT_NAME), 0);
                                    }
                                    else
                                    {
                                        res = GUI_DeleteWeatherFolder();
                                    }
                                    if (res == IDOK)
                                    {
                                        MessageBoxW(hwnd, L"Weather widget data cleared successfully.", _T(PRODUCT_NAME), MB_ICONINFORMATION);
                                    }
                                    else
                                    {
                                        if (res == IDABORT)
                                        {
                                            MessageBoxW(hwnd, L"An error has occured while clearing the data.", _T(PRODUCT_NAME), MB_ICONERROR);
                                        }
                                    }
                                    if (dwData)
                                    {
                                        dwVal = 1;
                                        GUI_Internal_RegSetValueExW(NULL, L"Virtualized_" _T(EP_CLSID) L"_PeopleBand", 0, 0, &dwVal, sizeof(DWORD));
                                    }
                                }
                            }
                        }
                    }
                    dwMaxHeight += dwLineHeight * dy;
                    if (!strncmp(line, ";u ", 3))
                    {
                        tabOrder++;
                    }
                }
                else if (!strncmp(line, ";l ", 3) || !strncmp(line, ";y ", 3) || !strncmp(line, ";c ", 3) || !strncmp(line, ";w ", 3) || !strncmp(line, ";z ", 3) || !strncmp(line, ";b ", 3) || !strncmp(line, ";i ", 3) || !strncmp(line, ";d ", 3) || !strncmp(line, ";v ", 3))
                {
                    ZeroMemory(text, (MAX_LINE_LENGTH + 3) * sizeof(wchar_t));
                    text[0] = L'\u2795';
                    text[1] = L' ';
                    text[2] = L' ';
                    MultiByteToWideChar(
                        CP_UTF8,
                        MB_PRECOMPOSED,
                        !strncmp(line, ";c ", 3) || !strncmp(line, ";z ", 3) ? strchr(line + 3, ' ') + 1 : line + 3,
                        numChRd - 3,
                        text + 3,
                        MAX_LINE_LENGTH
                    );

                    wchar_t* x = wcschr(text, L'\n');
                    if (x) *x = 0;
                    x = wcschr(text, L'\r');
                    if (x) *x = 0;
                    if (!strncmp(line, ";w ", 3) || !strncmp(line, ";c ", 3) || !strncmp(line, ";z ", 3) || !strncmp(line, ";b ", 3) || !strncmp(line, ";i ", 3) || !strncmp(line, ";d ", 3) || !strncmp(line, ";v ", 3))
                    {
                        WCHAR* wszTitle = NULL;
                        WCHAR* wszPrompt = NULL;
                        WCHAR* wszDefault = NULL;
                        WCHAR* wszFallbackDefault = NULL;
                        HMENU hMenu = NULL;
                        BOOL bInput = !strncmp(line, ";w ", 3);
                        BOOL bChoice = !strncmp(line, ";c ", 3);
                        BOOL bChoiceLefted = !strncmp(line, ";z ", 3);
                        BOOL bInvert = !strncmp(line, ";i ", 3);
                        BOOL bJustCheck = !strncmp(line, ";d ", 3);
                        BOOL bBool = !strncmp(line, ";b ", 3);
                        BOOL bValue = !strncmp(line, ";v ", 3);
                        DWORD numChoices = 0;
                        if (bChoice || bChoiceLefted)
                        {
                            char* p = strchr(line + 3, ' ');
                            if (p) *p = 0;
                            numChoices = atoi(line + 3);
                            hMenu = CreatePopupMenu();
                            for (unsigned int i = 0; i < numChoices; ++i)
                            {
                                char* l = malloc(MAX_LINE_LENGTH * sizeof(char));
                                numChRd = getline(&l, &bufsiz, f);
                                if (strncmp(l, ";x ", 3))
                                {
                                    free(l);
                                    i--;
                                    continue;
                                }
                                char* p = strchr(l + 3, ' ');
                                if (p) *p = 0;
                                char* ln = p + 1;
                                p = strchr(p + 1, '\r');
                                if (p) *p = 0;
                                p = strchr(p + 1, '\n');
                                if (p) *p = 0;

                                wchar_t* miText = malloc((strlen(ln) + 1) * sizeof(wchar_t));
                                MultiByteToWideChar(
                                    CP_UTF8,
                                    MB_PRECOMPOSED,
                                    ln,
                                    strlen(ln) + 1,
                                    miText,
                                    strlen(ln) + 1
                                );

                                MENUITEMINFOW menuInfo;
                                ZeroMemory(&menuInfo, sizeof(MENUITEMINFOW));
                                menuInfo.cbSize = sizeof(MENUITEMINFOW);
                                menuInfo.fMask = MIIM_ID | MIIM_STRING | MIIM_DATA | MIIM_STATE;
                                menuInfo.wID = atoi(l + 3) + 1;
                                menuInfo.dwItemData = l;
                                menuInfo.fType = MFT_STRING;
                                menuInfo.dwTypeData = miText;
                                menuInfo.cch = strlen(ln);
                                InsertMenuItemW(
                                    hMenu,
                                    i,
                                    TRUE,
                                    &menuInfo
                                );

                                free(miText);
                            }
                        }
                        else if (bInput)
                        {
                            wszTitle = malloc(MAX_LINE_LENGTH * sizeof(WCHAR));
                            wszPrompt = malloc(MAX_LINE_LENGTH * sizeof(WCHAR));
                            wszDefault = malloc(MAX_LINE_LENGTH * sizeof(WCHAR));
                            wszFallbackDefault = malloc(MAX_LINE_LENGTH * sizeof(WCHAR));
                            char* l = malloc(MAX_LINE_LENGTH * sizeof(char));
                            numChRd = getline(&l, &bufsiz, f);
                            char* p = l;
                            p = strchr(p + 1, '\r');
                            if (p) *p = 0;
                            p = strchr(p + 1, '\n');
                            if (p) *p = 0;
                            MultiByteToWideChar(
                                CP_UTF8,
                                MB_PRECOMPOSED,
                                l + 1,
                                numChRd - 1,
                                wszPrompt,
                                MAX_LINE_LENGTH
                            );
                            numChRd = getline(&l, &bufsiz, f);
                            p = l;
                            p = strchr(p + 1, '\r');
                            if (p) *p = 0;
                            p = strchr(p + 1, '\n');
                            if (p) *p = 0;
                            MultiByteToWideChar(
                                CP_UTF8,
                                MB_PRECOMPOSED,
                                l + 1,
                                numChRd - 1,
                                wszFallbackDefault,
                                MAX_LINE_LENGTH
                            );
                            free(l);
                        }
                        numChRd = getline(&line, &bufsiz, f);
                        if (!strncmp(line, ";\"Virtualized_" EP_CLSID, 52))
                        {
                            for (unsigned int kkkk = 1; kkkk < MAX_LINE_LENGTH; ++kkkk)
                            {
                                if (line[kkkk])
                                {
                                    line[kkkk - 1] = line[kkkk];
                                }
                                else
                                {
                                    line[kkkk - 1] = 0;
                                    break;
                                }
                            }
                            ////////printf("%s\n", line);
                        }
                        ZeroMemory(name, MAX_LINE_LENGTH * sizeof(wchar_t));
                        MultiByteToWideChar(
                            CP_UTF8,
                            MB_PRECOMPOSED,
                            line[0] == '"' ? line + 1 : line,
                            numChRd,
                            name,
                            MAX_LINE_LENGTH
                        );
                        wchar_t* d = wcschr(name, L'=');
                        if (d) *d = 0;
                        wchar_t* p = wcschr(name, L'"');
                        if (p) *p = 0;
                        BOOL bShouldAlterTaskbarDa = FALSE;
                        if (!wcscmp(name, L"TaskbarDa"))
                        {
                            if (!gui_bOldTaskbar)
                            {
                                MENUITEMINFOA menuInfo;
                                ZeroMemory(&menuInfo, sizeof(MENUITEMINFOA));
                                menuInfo.cbSize = sizeof(MENUITEMINFOA);
                                menuInfo.fMask = MIIM_DATA;
                                GetMenuItemInfoA(hMenu, 3, FALSE, &menuInfo);
                                if (menuInfo.dwItemData)
                                {
                                    free(menuInfo.dwItemData);
                                }
                                RemoveMenu(hMenu, 3, MF_BYCOMMAND);
                                bShouldAlterTaskbarDa = TRUE;
                            }
                        }
                        if (!wcscmp(name, L"Virtualized_" _T(EP_CLSID) L"_TaskbarPosition") || !wcscmp(name, L"Virtualized_" _T(EP_CLSID) L"_MMTaskbarPosition"))
                        {
                            if (!gui_bOldTaskbar)
                            {
                                MENUITEMINFOA menuInfo;
                                ZeroMemory(&menuInfo, sizeof(MENUITEMINFOA));
                                menuInfo.cbSize = sizeof(MENUITEMINFOA);
                                menuInfo.fMask = MIIM_DATA;
                                GetMenuItemInfoA(hMenu, 1, FALSE, &menuInfo);
                                if (menuInfo.dwItemData)
                                {
                                    free(menuInfo.dwItemData);
                                }
                                RemoveMenu(hMenu, 1, MF_BYCOMMAND);
                                ZeroMemory(&menuInfo, sizeof(MENUITEMINFOA));
                                menuInfo.cbSize = sizeof(MENUITEMINFOA);
                                menuInfo.fMask = MIIM_DATA;
                                GetMenuItemInfoA(hMenu, 3, FALSE, &menuInfo);
                                if (menuInfo.dwItemData)
                                {
                                    free(menuInfo.dwItemData);
                                }
                                RemoveMenu(hMenu, 3, MF_BYCOMMAND);
                            }
                        }
                        HKEY hKey = NULL;
                        wchar_t* bIsHKLM = wcsstr(section, L"HKEY_LOCAL_MACHINE");
                        bIsHKLM = !bIsHKLM ? NULL : ((bIsHKLM - section) < 3);
                        DWORD dwDisposition;
                        DWORD dwSize = sizeof(DWORD);
                        DWORD value = FALSE;

                        //wprintf(L"%s %s %s\n", section, name, d + 1);
                        if (!bInput && !wcsncmp(d + 1, L"dword:", 6))
                        {
                            wchar_t* x = wcschr(d + 1, L':');
                            x++;
                            value = wcstol(x, NULL, 16);
                        }
                        if (bInput)
                        {
                            wchar_t* x = wcschr(d + 2, L'"');
                            x[0] = 0;
                            wcscpy_s(wszDefault, MAX_LINE_LENGTH, d + 2);
                        }

                        if (bInput)
                        {
                            dwSize = MAX_LINE_LENGTH;
                            GUI_RegCreateKeyExW(
                                bIsHKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                wcschr(section, L'\\') + 1,
                                0,
                                NULL,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | (hDC ? 0 : (!bIsHKLM ? KEY_WRITE : 0)),
                                NULL,
                                & hKey,
                                & dwDisposition
                            );
                            if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
                            {
                                hKey = NULL;
                            }
                            GUI_RegQueryValueExW(
                                hKey,
                                name,
                                0,
                                NULL,
                                wszDefault,
                                &dwSize
                            );
                        }
                        else if (!bJustCheck)
                        {
                            GUI_RegCreateKeyExW(
                                bIsHKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                wcschr(section, L'\\') + 1,
                                0,
                                NULL,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | (hDC ? 0 : (!bIsHKLM ? KEY_WRITE : 0)),
                                NULL,
                                &hKey,
                                &dwDisposition
                            );
                            if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
                            {
                                hKey = NULL;
                            }
                            GUI_RegQueryValueExW(
                                hKey,
                                name,
                                0,
                                NULL,
                                &value,
                                &dwSize
                            );
                            if (!wcscmp(name, L"OldTaskbar"))
                            {
                                gui_bOldTaskbar = value;
                            }
                            if (hDC && bInvert)
                            {
                                value = !value;
                            }
                        }
                        else
                        {
                            GUI_RegOpenKeyExW(
                                bIsHKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                wcschr(section, L'\\') + 1,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | (hDC ? 0 : (!bIsHKLM ? KEY_WRITE : 0)),
                                &hKey
                            );
                            if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
                            {
                                hKey = NULL;
                            }
                            value = hKey;
                        }
                        if (bInput)
                        {
                            wcscpy_s(wszTitle, MAX_LINE_LENGTH, text + 3);
                            wcscat_s(
                                text,
                                MAX_LINE_LENGTH,
                                L" : "
                            );
                            if (wszDefault[0] == 0)
                            {
                                wcscat_s(text, MAX_LINE_LENGTH, wszFallbackDefault);
                            }
                            else
                            {
                                wcscat_s(
                                    text,
                                    MAX_LINE_LENGTH,
                                    wszDefault
                                );
                            }
                        }
                        else if (bInvert || bBool || bJustCheck)
                        {
                            if (value)
                            {
                                text[0] = L'\u2714';
                            }
                            else
                            {
                                text[0] = L'\u274C';
                            }
                            text[1] = L' ';
                            text[2] = L' ';
                        }
                        else if (bValue)
                        {
                            wcscat_s(
                                text,
                                MAX_LINE_LENGTH,
                                L" : "
                            );
                            wchar_t buf[100];
                            _itow_s(value, buf, 100, 10);
                            wcscat_s(
                                text,
                                MAX_LINE_LENGTH,
                                buf
                            );
                        }
                        else if (bChoice || bChoiceLefted)
                        {
                            wcscat_s(
                                text,
                                MAX_LINE_LENGTH,
                                L" : "
                            );
                            MENUITEMINFOW menuInfo;
                            ZeroMemory(&menuInfo, sizeof(MENUITEMINFOW));
                            menuInfo.cbSize = sizeof(MENUITEMINFOW);
                            menuInfo.fMask = MIIM_STRING;
                            int vvv = value + 1;
                            if (bShouldAlterTaskbarDa && vvv == 3) vvv = 2;
                            GetMenuItemInfoW(hMenu, vvv, FALSE, &menuInfo);
                            menuInfo.cch += 1;
                            menuInfo.dwTypeData = text + wcslen(text);
                            GetMenuItemInfoW(hMenu, vvv, FALSE, &menuInfo);
                            ZeroMemory(&menuInfo, sizeof(MENUITEMINFOW));
                            menuInfo.cbSize = sizeof(MENUITEMINFOW);
                            menuInfo.fMask = MIIM_STATE;
                            menuInfo.fState = MFS_CHECKED;
                            SetMenuItemInfo(hMenu, vvv, FALSE, &menuInfo);
                        }
                        if (hDC && !bInvert && !bBool && !bJustCheck)
                        {
                            RECT rcTemp;
                            rcTemp = rcText;
                            DrawTextW(
                                hdcPaint,
                                text,
                                3,
                                &rcTemp,
                                DT_CALCRECT
                            );
                            rcText.left += (!bChoiceLefted ? (rcTemp.right - rcTemp.left) : 0);
                            for (unsigned int i = 0; i < wcslen(text); ++i)
                            {
                                text[i] = text[i + 3];
                            }
                        }
                        RECT rcTemp;
                        rcTemp = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            -1,
                            &rcTemp,
                            DT_CALCRECT
                        );
                        rcTemp.bottom = rcText.bottom;
                        if (!hDC && (PtInRect(&rcTemp, pt) || (pt.x == 0 && pt.y == 0 && tabOrder == _this->tabOrder)))
                        {
                            if (bJustCheck)
                            {
                                if (bIsHKLM && wcsstr(section, L"Software\\Classes\\CLSID\\" _T(EP_CLSID) L"\\InprocServer32"))
                                {
                                    WCHAR wszArgs[MAX_PATH];
                                    if (!hKey)
                                    {
                                        wszArgs[0] = L'\"';
                                        SHGetFolderPathW(NULL, SPECIAL_FOLDER, NULL, SHGFP_TYPE_CURRENT, wszArgs + 1);
                                        wcscat_s(wszArgs, MAX_PATH, _T(APP_RELATIVE_PATH) L"\\" _T(PRODUCT_NAME) L".amd64.dll\"");
                                    }
                                    else
                                    {
                                        wszArgs[0] = L'/';
                                        wszArgs[1] = L'u';
                                        wszArgs[2] = L' ';
                                        wszArgs[3] = L'"';
                                        SHGetFolderPathW(NULL, SPECIAL_FOLDER, NULL, SHGFP_TYPE_CURRENT, wszArgs + 4);
                                        wcscat_s(wszArgs, MAX_PATH, _T(APP_RELATIVE_PATH) L"\\" _T(PRODUCT_NAME) L".amd64.dll\"");
                                    }
                                    wprintf(L"%s\n", wszArgs);
                                    WCHAR wszApp[MAX_PATH * 2];
                                    GetSystemDirectoryW(wszApp, MAX_PATH * 2);
                                    wcscat_s(wszApp, MAX_PATH * 2, L"\\regsvr32.exe");
                                    wprintf(L"%s\n", wszApp);
                                    SHELLEXECUTEINFOW sei;
                                    ZeroMemory(&sei, sizeof(SHELLEXECUTEINFOW));
                                    sei.cbSize = sizeof(sei);
                                    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                                    sei.hwnd = NULL;
                                    sei.hInstApp = NULL;
                                    sei.lpVerb = L"runas";
                                    sei.lpFile = wszApp;
                                    sei.lpParameters = wszArgs;
                                    sei.hwnd = NULL;
                                    sei.nShow = SW_NORMAL;
                                    if (ShellExecuteExW(&sei) && sei.hProcess)
                                    {
                                        WaitForSingleObject(sei.hProcess, INFINITE);
                                        DWORD dwExitCode = 0;
                                        if (GetExitCodeProcess(sei.hProcess, &dwExitCode) && !dwExitCode)
                                        {

                                        }
                                        else
                                        {

                                        }
                                        CloseHandle(sei.hProcess);
                                    }
                                    else
                                    {
                                        DWORD dwError = GetLastError();
                                        if (dwError == ERROR_CANCELLED)
                                        {
                                        }
                                    }
                                }
                                else
                                {
                                    if (hKey)
                                    {
                                        RegCloseKey(hKey);
                                        hKey = NULL;
                                        RegDeleteKeyExW(
                                            bIsHKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                            wcschr(section, L'\\') + 1,
                                            REG_OPTION_NON_VOLATILE,
                                            0
                                        );
                                    }
                                    else
                                    {
                                        GUI_RegCreateKeyExW(
                                            bIsHKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                            wcschr(section, L'\\') + 1,
                                            0,
                                            NULL,
                                            REG_OPTION_NON_VOLATILE,
                                            KEY_WRITE,
                                            NULL,
                                            &hKey,
                                            &dwDisposition
                                        );
                                        if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
                                        {
                                            hKey = NULL;
                                        }
                                        if (d[1] == '"')
                                        {
                                            wchar_t* p = wcschr(d + 2, L'"');
                                            if (p) *p = 0;
                                            GUI_RegSetValueExW(
                                                hKey,
                                                !wcsncmp(name, L"@", 1) ? NULL : name,
                                                0,
                                                REG_SZ,
                                                d + 2,
                                                wcslen(d + 2) * sizeof(wchar_t)
                                            );
                                        }
                                    }
                                }
                            }
                            else
                            {
                                DWORD val = 0;
                                if (bInput)
                                {
                                    WCHAR* wszAnswer = calloc(MAX_LINE_LENGTH, sizeof(WCHAR));
                                    BOOL bWasCancelled = FALSE;
                                    if (SUCCEEDED(InputBox(FALSE, hwnd, wszPrompt, wszTitle, wszDefault, wszAnswer, MAX_LINE_LENGTH, &bWasCancelled)) && !bWasCancelled)
                                    {
                                        if (wszAnswer[0])
                                        {
                                            GUI_RegSetValueExW(
                                                hKey,
                                                name,
                                                0,
                                                REG_SZ,
                                                wszAnswer,
                                                (wcslen(wszAnswer) + 1) * sizeof(WCHAR)
                                            );
                                        }
                                        else
                                        {
                                            RegDeleteValueW(hKey, name);
                                        }
                                        Sleep(100);
                                        PostMessageW(FindWindowW(_T(EPW_WEATHER_CLASSNAME), NULL), EP_WEATHER_WM_FETCH_DATA, 0, 0);
                                    }
                                    free(wszAnswer);
                                }
                                else if (bChoice || bChoiceLefted)
                                {
                                    RECT rcTemp;
                                    rcTemp = rcText;
                                    DrawTextW(
                                        hdcPaint,
                                        text,
                                        3,
                                        &rcTemp,
                                        DT_CALCRECT
                                    );
                                    POINT p;
                                    p.x = rcText.left + (bChoiceLefted ? 0 : (rcTemp.right - rcTemp.left));
                                    p.y = rcText.bottom;
                                    ClientToScreen(
                                        hwnd,
                                        &p
                                    );
                                    val = TrackPopupMenu(
                                        hMenu, 
                                        TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        p.x,
                                        p.y,
                                        0,
                                        hwnd,
                                        0
                                    );
                                    if (val > 0) value = val - 1;
                                    KillTimer(hwnd, GUI_TIMER_READ_REPEAT_SELECTION);
                                    SetTimer(hwnd, GUI_TIMER_READ_REPEAT_SELECTION, GUI_TIMER_READ_REPEAT_SELECTION_TIMEOUT, NULL);

                                }
                                else if (bValue)
                                {

                                }
                                else
                                {
                                    value = !value;
                                }
                                if (!wcscmp(name, L"LastSectionInProperties") && wcsstr(section, _T(REGPATH)) && value)
                                {
                                    value = _this->section + 1;
                                }
                                if (!bInput && (!(bChoice || bChoiceLefted) || ((bChoice || bChoiceLefted) && val)))
                                {
                                    GUI_RegSetValueExW(
                                        hKey,
                                        name,
                                        0,
                                        REG_DWORD,
                                        &value,
                                        sizeof(DWORD)
                                    );
                                }
                            }
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        if (hKey)
                        {
                            RegCloseKey(hKey);
                        }
                        if (bChoice || bChoiceLefted)
                        {
                            for (unsigned int i = 0; i < numChoices; ++i)
                            {
                                MENUITEMINFOA menuInfo;
                                ZeroMemory(&menuInfo, sizeof(MENUITEMINFOA));
                                menuInfo.cbSize = sizeof(MENUITEMINFOA);
                                menuInfo.fMask = MIIM_DATA;
                                GetMenuItemInfoA(hMenu, i, TRUE, &menuInfo);
                                if (menuInfo.dwItemData)
                                {
                                    free(menuInfo.dwItemData);
                                }
                            }
                            DestroyMenu(hMenu);
                        }
                        if (wszTitle)
                        {
                            free(wszTitle);
                        }
                        if (wszPrompt)
                        {
                            free(wszPrompt);
                        }
                        if (wszDefault)
                        {
                            free(wszDefault);
                        }
                        if (wszFallbackDefault)
                        {
                            free(wszFallbackDefault);
                        }
                    }
                    if (hDC && (!strncmp(line, ";l ", 3) || !strncmp(line, ";y ", 3)))
                    {
                        RECT rcTemp;
                        rcTemp = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            3,
                            &rcTemp,
                            DT_CALCRECT
                        );
                        rcText.left += (!strncmp(line, ";l ", 3) ? (rcTemp.right - rcTemp.left) : 0);
                        for (unsigned int i = 0; i < wcslen(text); ++i)
                        {
                            text[i] = text[i + 3];
                        }
                    }
                    if (!hDC && (!strncmp(line, ";l ", 3) || !strncmp(line, ";y ", 3)))
                    {
                        RECT rcTemp;
                        rcTemp = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            -1,
                            &rcTemp,
                            DT_CALCRECT
                        );
                        rcTemp.bottom = rcText.bottom;
                        //printf("%d %d %d %d %d %d %d %d\n", rcText.left, rcText.top, rcText.right, rcText.bottom, rcTemp.left, rcTemp.top, rcTemp.right, rcTemp.bottom);
                        if (PtInRect(&rcTemp, pt) || (pt.x == 0 && pt.y == 0 && tabOrder == _this->tabOrder))
                        {
                            numChRd = getline(&line, &bufsiz, f);
                            char* p = strchr(line, '\r');
                            if (p) *p = 0;
                            p = strchr(line, '\n');
                            if (p) *p = 0;
                            if (line[1] != 0)
                            {
                                if (line[1] == ';')
                                {
                                    if (!strcmp(line + 2, ";EP_CHECK_FOR_UPDATES"))
                                    {
                                        HANDLE hEvent = CreateEventW(NULL, FALSE, FALSE, L"EP_Ev_CheckForUpdates_" _T(EP_CLSID));
                                        if (hEvent)
                                        {
                                            if (GetLastError() != ERROR_ALREADY_EXISTS)
                                            {
                                                CloseHandle(hEvent);
                                            }
                                            else
                                            {
                                                SetEvent(hEvent);
                                                CloseHandle(hEvent);
                                            }
                                        }
                                    }
                                    else if(!strcmp(line + 2, ";EP_INSTALL_UPDATES"))
                                    {
                                        HANDLE hEvent = CreateEventW(NULL, FALSE, FALSE, L"EP_Ev_InstallUpdates_" _T(EP_CLSID));
                                        if (hEvent)
                                        {
                                            if (GetLastError() != ERROR_ALREADY_EXISTS)
                                            {
                                                CloseHandle(hEvent);
                                            }
                                            else
                                            {
                                                SetEvent(hEvent);
                                                CloseHandle(hEvent);
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    ShellExecuteA(
                                        NULL,
                                        "open",
                                        line + 1,
                                        NULL,
                                        NULL,
                                        SW_SHOWNORMAL
                                    );
                                }
                            }
                        }
                    }
                    if (hDC)
                    {
                        COLORREF cr;
                        if (tabOrder == _this->tabOrder)
                        {
                            bTabOrderHit = TRUE;
                            if (!IsThemeActive() || IsHighContrast())
                            {
                                cr = SetTextColor(hdcPaint, GetSysColor(COLOR_HIGHLIGHT));
                            }
                            else
                            {
                                DttOpts.crText = g_darkModeEnabled ? GUI_TEXTCOLOR_SELECTED_DARK : GUI_TEXTCOLOR_SELECTED;
                                //DttOpts.crText = GetSysColor(COLOR_HIGHLIGHT);
                            }
                        }
                        RECT rcNew = rcText;
                        DrawTextW(
                            hdcPaint,
                            text,
                            -1,
                            &rcNew,
                            DT_CALCRECT
                        );
                        if (rcNew.right - rcNew.left > dwMaxWidth)
                        {
                            dwMaxWidth = rcNew.right - rcNew.left + 50 * dx;
                        }
                        if (!wcsncmp(text + 3, L"%PLACEHOLDER_0001%", 18))
                        {
                            WCHAR key = 0;
                            BYTE kb[256];
                            ZeroMemory(kb, 256);
                            ToUnicode(
                                MapVirtualKeyW(0x29, MAPVK_VSC_TO_VK_EX),
                                0x29,
                                kb,
                                &key,
                                1,
                                0
                            );
                            swprintf(text + 3, MAX_LINE_LENGTH, L"Disable per-application window list ( Alt + %c )", key);
                        }
                        if (tabOrder == _this->tabOrder)
                        {
                            if (_this->bShouldAnnounceSelected)
                            {
                                unsigned int accLen = wcslen(text);
                                DWORD dwType = 0;
                                if (!strncmp(line, ";y ", 3))
                                {
                                    dwType = 4;
                                }
                                if (text[0] == L'\u2714') dwType = 1;
                                else if (text[0] == L'\u274C') dwType = 2;
                                else if (text[accLen - 1] == 56405) dwType = 3;
                                else if (!strstr(line, "dword")) dwType = 5;
                                WCHAR accText[1000], accText2[1000];
                                ZeroMemory(accText, 1000 * sizeof(wchar_t));
                                ZeroMemory(accText2, 1000 * sizeof(wchar_t));
                                swprintf_s(
                                    accText,
                                    1000,
                                    L"%s %s %s: %s",
                                    (_this->dwPageLocation < 0 ?
                                        L"Reached end of the page." :
                                        (_this->dwPageLocation > 0 ?
                                            L"Reached beginning of the page." : L"")),
                                    (lastHeading[0] == 0) ? L"" : lastHeading,
                                    (dwType == 1 || dwType == 2) ? text + 1 : text,
                                    dwType == 1 ? L"Enabled" :
                                    (dwType == 2 ? L"Disabled" :
                                        (dwType == 3 ? L"Link" :
                                            (dwType == 4 ? L"Button" :
                                                (dwType == 5 ? L"Input" : 
                                                    L"List"))))
                                );
                                accLen = wcslen(accText);
                                unsigned int j = 0;
                                for (unsigned int i = 0; i < accLen; ++i)
                                {
                                    if (accText[i] == L'%')
                                    {
                                        accText2[j] = L'%';
                                        accText2[j + 1] = L'%';
                                        j++;
                                    }
                                    else
                                    {
                                        accText2[j] = accText[i];
                                    }
                                    ++j;
                                }
                                _this->dwPageLocation = 0;
                                BOOL dwTypeRepl = 0;
                                accLen = wcslen(accText2);
                                for (unsigned int i = 0; i < accLen; ++i)
                                {
                                    if (accText2[i] == L'*')
                                    {
                                        if (accText2[i + 1] == L'*')
                                        {
                                            dwTypeRepl = 1;
                                        }
                                        accText2[i] = L'%';
                                        if (i + 1 >= accLen)
                                        {
                                            accText2[i + 2] = 0;
                                        }
                                        accText2[i + 1] = L's';
                                    }
                                }
                                if (dwTypeRepl == 1)
                                {
                                    swprintf_s(accText, 1000, accText2, L" - Requires registration as shell extension to work in Open or Save file dialogs - ");
                                }
                                else
                                {
                                    swprintf_s(accText, 1000, accText2, L" - Requires File Explorer restart to apply - ");
                                }
                                //wprintf(L">>> %s\n", accText);
                                SetWindowTextW(_this->hAccLabel, accText);
                                NotifyWinEvent(
                                    EVENT_OBJECT_LIVEREGIONCHANGED,
                                    _this->hAccLabel,
                                    OBJID_CLIENT,
                                    CHILDID_SELF);
                                _this->bShouldAnnounceSelected = FALSE;
                            }
                        }
                        if (IsThemeActive() && !IsHighContrast())
                        {
                            DrawThemeTextEx(
                                _this->hTheme,
                                hdcPaint,
                                0,
                                0,
                                text,
                                -1,
                                dwTextFlags,
                                &rcText,
                                &DttOpts
                            );
                        }
                        else
                        {
                            DrawTextW(
                                hdcPaint,
                                text,
                                -1,
                                &rcText,
                                dwTextFlags
                            );
                        }
                        if (tabOrder == _this->tabOrder)
                        {
                            if (!IsThemeActive() || IsHighContrast())
                            {
                                SetTextColor(hdcPaint, cr);
                            }
                            else
                            {
                                DttOpts.crText = g_darkModeEnabled ? GUI_TEXTCOLOR_DARK : GUI_TEXTCOLOR;
                                //DttOpts.crText = GetSysColor(COLOR_WINDOWTEXT);
                            }
                        }
                    }
                    dwMaxHeight += dwLineHeight * dy;
                    tabOrder++;
                }
            }
        }
        fclose(f);
        free(section);
        free(name);
        free(text);
        free(line);
        free(lastHeading);
        if (!bWasSpecifiedSectionValid)
        {
            _this->bRebuildIfTabOrderIsEmpty = FALSE;
            _this->tabOrder = 0;
            GUI_SetSection(_this, FALSE, 0);
            InvalidateRect(hwnd, NULL, FALSE);
        }

        SelectObject(hdcPaint, hOldFont);
        if (!hDC)
        {
            ReleaseDC(hwnd, hdcPaint);
        }
        DeleteObject(hFontSectionSel);
        DeleteObject(hFontSection);
        DeleteObject(hFontRegular);
        DeleteObject(hFontTitle);
        DeleteObject(hFontUnderline);
        DeleteObject(hFontCaption);

        if (_this->bShouldAnnounceSelected)
        {
            int max_section = 100;
            for (unsigned int i = 0; i < 100; ++i)
            {
                if (_this->sectionNames[i][0] == 0)
                {
                    max_section = i - 1;
                    break;
                }
            }
            WCHAR wszAccText[100];
            swprintf_s(
                wszAccText,
                100,
                L"Selected page: %s: %d of %d.",
                _this->sectionNames[_this->section],
                _this->section + 1,
                max_section + 1
            );
            SetWindowTextW(_this->hAccLabel, wszAccText);
            if (!_this->bRebuildIfTabOrderIsEmpty)
            {
                NotifyWinEvent(
                    EVENT_OBJECT_LIVEREGIONCHANGED,
                    _this->hAccLabel,
                    OBJID_CLIENT,
                    CHILDID_SELF
                );
            }
        }

        if (hDC)
        {
            if (_this->tabOrder == GUI_MAX_TABORDER)
            {
                _this->tabOrder = tabOrder - 1;
                _this->dwPageLocation = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (!bTabOrderHit)
            {
                if (_this->bRebuildIfTabOrderIsEmpty)
                {
                    _this->dwPageLocation = 1;
                    _this->bRebuildIfTabOrderIsEmpty = FALSE;
                    _this->tabOrder = 1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                else
                {
                    _this->tabOrder = 0;
                }
            }
        }

        if (_this->bRebuildIfTabOrderIsEmpty)
        {
            _this->bRebuildIfTabOrderIsEmpty = FALSE;
        }
    }
    if (_this->bCalcExtent)
    {
        RECT rcWin;
        GetWindowRect(hwnd, &rcWin);
        printf("%d %d - %d %d\n", rcWin.right - rcWin.left, rcWin.bottom - rcWin.top, dwMaxWidth, dwMaxHeight);

        dwMaxWidth += dwInitialLeftPad + _this->padding.left + _this->padding.right;
        if (!IsThemeActive() || IsHighContrast())
        {
            dwMaxHeight += GUI_LINE_HEIGHT * dy + 20 * dy;
        }
        else
        {
            dwMaxHeight += GUI_PADDING * 2 * dy;
        }

        HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hMonitor, &mi);
        SetWindowPos(
            hwnd,
            hwnd,
            mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) / 2 - (dwMaxWidth) / 2),
            mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) / 2 - (dwMaxHeight) / 2),
            dwMaxWidth,
            dwMaxHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | (_this->bCalcExtent == 2 ? SWP_NOMOVE : 0)
        );

        DWORD dwReadSection = 0;

        HKEY hKey = NULL;
        DWORD dwSize = sizeof(DWORD);
        RegCreateKeyExW(
            HKEY_CURRENT_USER,
            TEXT(REGPATH),
            0,
            NULL,
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WOW64_64KEY | KEY_WRITE,
            NULL,
            &hKey,
            NULL
        );
        if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
        {
            hKey = NULL;
        }
        if (hKey)
        {
            dwReadSection = 0;
            dwSize = sizeof(DWORD);
            RegQueryValueExW(
                hKey,
                TEXT("LastSectionInProperties"),
                0,
                NULL,
                &dwReadSection,
                &dwSize
            );
            if (dwReadSection)
            {
                _this->section = dwReadSection - 1;
            }
            dwReadSection = 0;
            dwSize = sizeof(DWORD);
            RegQueryValueExW(
                hKey,
                TEXT("OpenPropertiesAtNextStart"),
                0,
                NULL,
                &dwReadSection,
                &dwSize
            );
            if (dwReadSection)
            {
                _this->section = dwReadSection - 1;
                dwReadSection = 0;
                RegSetValueExW(
                    hKey,
                    TEXT("OpenPropertiesAtNextStart"),
                    0,
                    REG_DWORD,
                    &dwReadSection,
                    sizeof(DWORD)
                );
            }
            RegCloseKey(hKey);
        }

        _this->bCalcExtent = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
    }

    EndBufferedPaint(hBufferedPaint, TRUE);
    return TRUE;
}

static LRESULT CALLBACK GUI_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    GUI* _this;
    if (uMsg == WM_CREATE)
    {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)(lParam);
        _this = (int*)(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)_this);
        UINT dpiX, dpiY, dpiXP, dpiYP;
        POINT ptCursor, ptZero;
        ptZero.x = 0;
        ptZero.y = 0;
        GetCursorPos(&ptCursor);
        HMONITOR hMonitor = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTOPRIMARY);
        HMONITOR hPrimaryMonitor = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
        HRESULT hr = GetDpiForMonitor(
            hMonitor,
            MDT_DEFAULT,
            &dpiX,
            &dpiY
        );
        hr = GetDpiForMonitor(
            hPrimaryMonitor,
            MDT_DEFAULT,
            &dpiXP,
            &dpiYP
        );
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hMonitor, &mi);
        double dx = dpiX / 96.0, dy = dpiY / 96.0, dxp = dpiXP / 96.0, dyp = dpiYP / 96.0;
        _this->dpi.x = dpiX;
        _this->dpi.y = dpiY;
        SetRect(&_this->border_thickness, 2, 2, 2, 2);
        if (IsThemeActive())
        {
            BOOL bIsCompositionEnabled = TRUE;
            DwmIsCompositionEnabled(&bIsCompositionEnabled);
            if (bIsCompositionEnabled)
            {
                MARGINS marGlassInset;
                if (!IsHighContrast())
                {
                    marGlassInset.cxLeftWidth = -1; // -1 means the whole window
                    marGlassInset.cxRightWidth = -1;
                    marGlassInset.cyBottomHeight = -1;
                    marGlassInset.cyTopHeight = -1;
                }
                else
                {
                    marGlassInset.cxLeftWidth = 0;
                    marGlassInset.cxRightWidth = 0;
                    marGlassInset.cyBottomHeight = 0;
                    marGlassInset.cyTopHeight = 0;
                }
                DwmExtendFrameIntoClientArea(hWnd, &marGlassInset);
            }
        }
        SetWindowPos(
            hWnd, 
            hWnd, 
            mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) / 2 - (_this->size.cx * dx) / 2),
            mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) / 2 - (_this->size.cy * dy) / 2),
            _this->size.cx * dxp, 
            _this->size.cy * dyp,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
        );
        SetTimer(hWnd, GUI_TIMER_READ_HELP, GUI_TIMER_READ_HELP_TIMEOUT, NULL);
        if (IsThemeActive() && !IsHighContrast())
        {
            RECT rcTitle;
            DwmGetWindowAttribute(hWnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rcTitle, sizeof(RECT));
            _this->GUI_CAPTION_LINE_HEIGHT = rcTitle.bottom - rcTitle.top;
        }
        else
        {
            _this->GUI_CAPTION_LINE_HEIGHT = GUI_CAPTION_LINE_HEIGHT_DEFAULT;
        }
        if (IsThemeActive() && ShouldAppsUseDarkMode && !IsHighContrast())
        {
            AllowDarkModeForWindow(hWnd, g_darkModeEnabled);
            BOOL value = g_darkModeEnabled;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(BOOL));
        }
        if (!IsThemeActive() || IsHighContrast())
        {
            int extendedStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
            SetWindowLong(hWnd, GWL_EXSTYLE, extendedStyle | WS_EX_DLGMODALFRAME);
        }
    }
    else
    {
        LONG_PTR ptr = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        _this = (int*)(ptr);
    }
    if (uMsg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    else if (uMsg == WM_GETICON)
    {
        return _this->hIcon;
    }
    else if (uMsg == WM_SETTINGCHANGE)
    {
        if (IsColorSchemeChangeMessage(lParam))
        {
            if (IsThemeActive())
            {
                BOOL bIsCompositionEnabled = TRUE;
                DwmIsCompositionEnabled(&bIsCompositionEnabled);
                if (bIsCompositionEnabled)
                {
                    MARGINS marGlassInset;
                    if (!IsHighContrast())
                    {
                        marGlassInset.cxLeftWidth = -1; // -1 means the whole window
                        marGlassInset.cxRightWidth = -1;
                        marGlassInset.cyBottomHeight = -1;
                        marGlassInset.cyTopHeight = -1;
                    }
                    else
                    {
                        marGlassInset.cxLeftWidth = 0;
                        marGlassInset.cxRightWidth = 0;
                        marGlassInset.cyBottomHeight = 0;
                        marGlassInset.cyTopHeight = 0;
                    }
                    DwmExtendFrameIntoClientArea(hWnd, &marGlassInset);
                }
            }
            _this->bCalcExtent = 2;
            BOOL bIsCompositionEnabled = TRUE;
            DwmIsCompositionEnabled(&bIsCompositionEnabled);
            if (bIsCompositionEnabled)
            {
                BOOL value = (IsThemeActive() && !IsHighContrast()) ? 1 : 0;
                DwmSetWindowAttribute(hWnd, DWMWA_MICA_EFFFECT, &value, sizeof(BOOL));
            }
            if (IsThemeActive() && ShouldAppsUseDarkMode && !IsHighContrast())
            {
                RefreshImmersiveColorPolicyState();
                BOOL bDarkModeEnabled = IsThemeActive() && bIsCompositionEnabled && ShouldAppsUseDarkMode() && !IsHighContrast();
                if (bDarkModeEnabled != g_darkModeEnabled)
                {
                    g_darkModeEnabled = bDarkModeEnabled;
                    AllowDarkModeForWindow(hWnd, g_darkModeEnabled);
                    BOOL value = g_darkModeEnabled;
                    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(BOOL));
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            else
            {
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    else if (uMsg == WM_KEYDOWN)
    {
        _this->bRebuildIfTabOrderIsEmpty = FALSE;
        if (wParam == VK_ESCAPE)
        {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        else if (wParam == VK_TAB || wParam == VK_DOWN || wParam == VK_UP)
        {
            if ((GetKeyState(VK_SHIFT) & 0x8000) || wParam == VK_UP)
            {
                if (_this->tabOrder == 0)
                {
                    _this->tabOrder = GUI_MAX_TABORDER;
                }
                else
                {
                    _this->tabOrder--;
                    if (_this->tabOrder == 0)
                    {
                        _this->tabOrder = GUI_MAX_TABORDER;
                    }
                }
            }
            else
            {
                _this->tabOrder++;
            }
            _this->bRebuildIfTabOrderIsEmpty = TRUE;
            _this->bShouldAnnounceSelected = TRUE;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        else if (wParam == VK_SPACE || wParam == VK_RETURN)
        {
            POINT pt;
            pt.x = 0;
            pt.y = 0;
            _this->bShouldAnnounceSelected = TRUE;
            GUI_Build(0, hWnd, pt);
            return 0;
        }
        // this should be determined from the file, but for now it works
        else if (wParam >= '1' && wParam <= '9' || wParam == '0')
        {
            _this->tabOrder = 0;
            GUI_SetSection(_this, TRUE, wParam == '0' ? 9 : wParam - '1');
            _this->bShouldAnnounceSelected = TRUE;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        else if (wParam == VK_LEFT || wParam == VK_RIGHT)
        {
            int min_section = 0;
            int max_section = 100;
            int new_section = _this->section;
            for (unsigned int i = 0; i < 100; ++i)
            {
                if (_this->sectionNames[i][0] == 0)
                {
                    max_section = i - 1;
                    break;
                }
            }
            if (wParam == VK_LEFT)
            {
                new_section--;
            }
            else
            {
                new_section++;
            }
            if (new_section < min_section)
            {
                new_section = max_section;
            }
            if (new_section > max_section)
            {
                new_section = min_section;
            }
            _this->tabOrder = 0;
            GUI_SetSection(_this, TRUE, new_section);
            _this->bShouldAnnounceSelected = TRUE;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0; 
        }
        else if (wParam == 'H' || wParam == VK_F1)
        {
            SetTimer(hWnd, GUI_TIMER_READ_HELP, 200, NULL);
            return 0;
        }
        else if (wParam == VK_F5)
        {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        else if (wParam == 'Z')
        {
            return 0;
        }
        else if (wParam == 'X')
        {
            return 0;
        }
    }
    else if (uMsg == WM_NCMOUSELEAVE && IsThemeActive() && !IsHighContrast())
    {
        LRESULT lRes = 0;
        if (DwmDefWindowProc(hWnd, uMsg, wParam, lParam, &lRes))
        {
            return lRes;
        }
    }
    else if (uMsg == WM_NCRBUTTONUP && IsThemeActive() && !IsHighContrast())
    {
        HMENU pSysMenu = GetSystemMenu(hWnd, FALSE);
        if (pSysMenu != NULL)
        {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            EnableMenuItem(pSysMenu, SC_RESTORE, MF_GRAYED);
            EnableMenuItem(pSysMenu, SC_SIZE, MF_GRAYED);
            EnableMenuItem(pSysMenu, SC_MAXIMIZE, MF_GRAYED);
            BOOL cmd = TrackPopupMenu(pSysMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD, xPos, yPos, NULL, hWnd, 0);
            if (cmd)
            {
                PostMessageW(hWnd, WM_SYSCOMMAND, cmd, 0);
            }
        }
        return 0;
    }
    else if ((uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONUP) && IsThemeActive() && !IsHighContrast())
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        double dx = _this->dpi.x / 96.0, dy = _this->dpi.y / 96.0;
        UINT diff = (int)(((_this->GUI_CAPTION_LINE_HEIGHT - 16) * dx) / 2.0);
        RECT rc;
        SetRect(&rc, diff, diff, diff + (int)(16.0 * dx), diff + (int)(16.0 * dy));
        if (PtInRect(&rc, pt))
        {
            if (uMsg == WM_LBUTTONUP && _this->LeftClickTime != 0)
            {
                _this->LeftClickTime = milliseconds_now() - _this->LeftClickTime;
            }
            if (uMsg == WM_LBUTTONUP && _this->LeftClickTime != 0 && _this->LeftClickTime < GetDoubleClickTime())
            {
                _this->LeftClickTime = 0;
                PostQuitMessage(0);
            }
            else
            {
                if (uMsg == WM_LBUTTONUP)
                {
                    _this->LeftClickTime = milliseconds_now();
                }
                if (uMsg == WM_RBUTTONUP || !_this->LastClickTime || milliseconds_now() - _this->LastClickTime > 500)
                {
                    HMENU pSysMenu = GetSystemMenu(hWnd, FALSE);
                    if (pSysMenu != NULL)
                    {
                        if (uMsg == WM_LBUTTONUP)
                        {
                            pt.x = 0;
                            pt.y = _this->GUI_CAPTION_LINE_HEIGHT * dy;
                        }
                        ClientToScreen(hWnd, &pt);
                        EnableMenuItem(pSysMenu, SC_RESTORE, MF_GRAYED);
                        EnableMenuItem(pSysMenu, SC_SIZE, MF_GRAYED);
                        EnableMenuItem(pSysMenu, SC_MAXIMIZE, MF_GRAYED);
                        BOOL cmd = TrackPopupMenu(pSysMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, NULL, hWnd, 0);
                        if (cmd)
                        {
                            PostMessageW(hWnd, WM_SYSCOMMAND, cmd, 0);
                        }
                        if (uMsg == WM_LBUTTONUP)
                        {
                            _this->LastClickTime = milliseconds_now();
                        }
                    }
                }
            }
            return 0;
        }
    }
    else if (uMsg == WM_NCHITTEST && IsThemeActive() && !IsHighContrast())
    {
        LRESULT lRes = 0;
        if (DwmDefWindowProc(hWnd, uMsg, wParam, lParam, &lRes))
        {
            return lRes;
        }

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hWnd, &pt);

        double dx = _this->dpi.x / 96.0, dy = _this->dpi.y / 96.0;
        UINT diff = (int)(((_this->GUI_CAPTION_LINE_HEIGHT - 16) * dx) / 2.0);
        RECT rc;
        SetRect(&rc, diff, diff, diff + (int)(16.0 * dx), diff + (int)(16.0 * dy));
        if (PtInRect(&rc, pt))
        {
            return HTCLIENT;
        }

        if (pt.y < _this->extent.cyTopHeight)
        {
            return HTCAPTION;
        }
    }
    else if (uMsg == WM_NCCALCSIZE && wParam == TRUE && IsThemeActive() && !IsHighContrast())
    {
        NCCALCSIZE_PARAMS* sz = (NCCALCSIZE_PARAMS*)(lParam);
        sz->rgrc[0].left += _this->border_thickness.left;
        sz->rgrc[0].right -= _this->border_thickness.right;
        sz->rgrc[0].bottom -= _this->border_thickness.bottom;
        return 0;
    }
    else if (uMsg == WM_LBUTTONDOWN)
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        GUI_Build(0, hWnd, pt);
        //InvalidateRect(hWnd, NULL, FALSE);
    }
    else if (uMsg == WM_DPICHANGED)
    {
        _this->dpi.x = LOWORD(wParam);
        _this->dpi.y = HIWORD(wParam);
        RECT* rc = lParam;
        SetWindowPos(
            hWnd,
            hWnd,
            rc->left,
            rc->top,
            rc->right - rc->left,
            rc->bottom - rc->top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS
        );
        RECT rcTitle;
        DwmGetWindowAttribute(hWnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rcTitle, sizeof(RECT));
        _this->GUI_CAPTION_LINE_HEIGHT = (rcTitle.bottom - rcTitle.top) * (96.0 / _this->dpi.y);
        return 0;
    }
    else if (uMsg == WM_PAINT)
    {    
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);

        POINT pt;
        pt.x = 0;
        pt.y = 0;
        GUI_Build(hDC, hWnd, pt);

        EndPaint(hWnd, &ps);
        return 0;
    }
    else if (uMsg == WM_INPUTLANGCHANGE)
    {
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    else if (uMsg == WM_MSG_GUI_SECTION && wParam == WM_MSG_GUI_SECTION_GET)
    {
        return _this->section + 1;
    }
    else if (uMsg == WM_TIMER && wParam == GUI_TIMER_READ_HELP)
    {
        PlayHelpMessage(_this);
        KillTimer(hWnd, GUI_TIMER_READ_HELP);
    }
    else if (uMsg == WM_TIMER && wParam == GUI_TIMER_READ_REPEAT_SELECTION)
    {
        _this->bShouldAnnounceSelected = TRUE;
        InvalidateRect(hWnd, NULL, FALSE);
        KillTimer(hWnd, GUI_TIMER_READ_REPEAT_SELECTION);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

__declspec(dllexport) int ZZGUI(HWND hWnd, HINSTANCE hInstance, LPSTR lpszCmdLine, int nCmdShow)
{
    HWND hOther = NULL;
    if (hOther = FindWindowW(L"ExplorerPatcher_GUI_" _T(EP_CLSID), NULL))
    {
        SwitchToThisWindow(hOther, TRUE);
        return 0;
    }

    HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);

    HKEY hKey = NULL;
    DWORD dwSize = sizeof(DWORD);
    RegCreateKeyExW(
        HKEY_CURRENT_USER,
        TEXT(REGPATH),
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WOW64_64KEY,
        NULL,
        &hKey,
        NULL
    );
    if (hKey == NULL || hKey == INVALID_HANDLE_VALUE)
    {
        hKey = NULL;
    }
    DWORD bAllocConsole = FALSE;
    if (hKey)
    {
        dwSize = sizeof(DWORD);
        RegQueryValueExW(
            hKey,
            TEXT("AllocConsole"),
            0,
            NULL,
            &bAllocConsole,
            &dwSize
        );
        if (bAllocConsole)
        {
            FILE* conout;
            AllocConsole();
            freopen_s(
                &conout,
                "CONOUT$",
                "w",
                stdout
            );
        }
    }
    locale = GetUserDefaultUILanguage();
    dwSize = LOCALE_NAME_MAX_LENGTH;
    if (hKey)
    {
        RegQueryValueExW(
            hKey,
            TEXT("Language"),
            0,
            NULL,
            &locale,
            &dwSize
        );
    }

    wchar_t wszPath[MAX_PATH];
    ZeroMemory(
        wszPath,
        (MAX_PATH) * sizeof(char)
    );
    GetModuleFileNameW(hModule, wszPath, MAX_PATH);
    PathRemoveFileSpecW(wszPath);
    wcscat_s(
        wszPath,
        MAX_PATH,
        L"\\settings.reg"
    );
    wprintf(L"%s\n", wszPath);
    if (FileExistsW(wszPath))
    {
        HANDLE hFile = CreateFileW(
            wszPath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0
        );
        if (hFile)
        {
            HANDLE hFileMapping = CreateFileMappingW(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
            if (hFileMapping)
            {
                GUI_FileMapping = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
                GUI_FileSize = GetFileSize(hFile, NULL);
            }
        }
    }

    printf("Started \"GUI\" thread.\n");

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GUI _this;
    ZeroMemory(&_this, sizeof(GUI));
    _this.hBackgroundBrush = (HBRUSH)(CreateSolidBrush(RGB(255, 255, 255)));// (HBRUSH)GetStockObject(BLACK_BRUSH);
    _this.location.x = GUI_POSITION_X;
    _this.location.y = GUI_POSITION_Y;
    _this.size.cx = GUI_POSITION_WIDTH;
    _this.size.cy = GUI_POSITION_HEIGHT;
    _this.padding.left = GUI_PADDING_LEFT;
    _this.padding.right = GUI_PADDING_RIGHT;
    _this.padding.top = GUI_PADDING_TOP;
    _this.padding.bottom = GUI_PADDING_BOTTOM;
    _this.sidebarWidth = GUI_SIDEBAR_WIDTH;
    _this.hTheme = OpenThemeData(NULL, TEXT(GUI_WINDOWSWITCHER_THEME_CLASS));
    _this.tabOrder = 0;
    _this.bCalcExtent = 1;
    _this.section = 0;
    _this.dwStatusbarY = 0;
    _this.hIcon = NULL;
    _this.hExplorerFrame = NULL;

    ZeroMemory(
        wszPath,
        (MAX_PATH) * sizeof(wchar_t)
    );
    GetSystemDirectoryW(
        wszPath,
        MAX_PATH
    );
    wcscat_s(
        wszPath,
        MAX_PATH,
        L"\\shell32.dll"
    );

    WNDCLASS wc = { 0 };
    ZeroMemory(&wc, sizeof(WNDCLASSW));
    wc.style = 0;// CS_DBLCLKS;
    wc.lpfnWndProc = GUI_WindowProc;
    wc.hbrBackground = _this.hBackgroundBrush;
    wc.hInstance = hModule;
    wc.lpszClassName = L"ExplorerPatcher_GUI_" _T(EP_CLSID);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    HMODULE hShell32 = LoadLibraryExW(wszPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (hShell32)
    {
        _this.hIcon = LoadIconW(hShell32, MAKEINTRESOURCEW(40)); //40
        wc.hIcon = _this.hIcon;
    }
    RegisterClassW(&wc);

    _this.hExplorerFrame = LoadLibraryExW(L"ExplorerFrame.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (_this.hExplorerFrame)
    {
        LoadStringW(_this.hExplorerFrame, 50222, GUI_title, 260); // 726 = File Explorer
        wchar_t* p = wcschr(GUI_title, L'(');
        if (p)
        {
            p--;
            if (*p == L' ')
            {
                *p = 0;
            }
            else
            {
                p++;
                *p = 0;
            }
        }
        if (GUI_title[0] == 0)
        {
            LoadStringW(hModule, IDS_PRODUCTNAME, GUI_title, 260);
        }
    }
    else
    {
        LoadStringW(hModule, IDS_PRODUCTNAME, GUI_title, 260);
    }
    BOOL bIsCompositionEnabled = TRUE;
    DwmIsCompositionEnabled(&bIsCompositionEnabled);
    HANDLE hUxtheme = NULL;
    BOOL bHasLoadedUxtheme = FALSE;
    bHasLoadedUxtheme = TRUE;
    hUxtheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxtheme)
    {
        RefreshImmersiveColorPolicyState = GetProcAddress(hUxtheme, (LPCSTR)104);
        SetPreferredAppMode = GetProcAddress(hUxtheme, (LPCSTR)135);
        AllowDarkModeForWindow = GetProcAddress(hUxtheme, (LPCSTR)133);
        ShouldAppsUseDarkMode = GetProcAddress(hUxtheme, (LPCSTR)132);
        if (ShouldAppsUseDarkMode &&
            SetPreferredAppMode &&
            AllowDarkModeForWindow &&
            RefreshImmersiveColorPolicyState
            )
        {
            SetPreferredAppMode(TRUE);
            RefreshImmersiveColorPolicyState();
            g_darkModeEnabled = IsThemeActive() && bIsCompositionEnabled && ShouldAppsUseDarkMode() && !IsHighContrast();
        }
    }
    GUI_RegQueryValueExW(NULL, L"Virtualized_" _T(EP_CLSID) L"_TaskbarPosition", NULL, NULL, &dwTaskbarPosition, NULL);
    HWND hwnd = CreateWindowEx(
        NULL,
        L"ExplorerPatcher_GUI_" _T(EP_CLSID),
        GUI_title,
        WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX,
        0,
        0,
        0,
        0,
        NULL, NULL, hModule, &_this
    );
    if (!hwnd)
    {
        return 1;
    }

    _this.hAccLabel = CreateWindowExW(
        0,
        L"Static",
        L"",
        WS_CHILD,
        10,   
        10,   
        100, 
        100, 
        hwnd,
        NULL,
        (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
        NULL
    );

    hr = CoCreateInstance(
        &CLSID_AccPropServices,
        NULL,
        CLSCTX_INPROC,
        &IID_IAccPropServices,
        &_this.pAccPropServices);
    if (SUCCEEDED(hr))
    {
        VARIANT var;
        var.vt = VT_I4;
        var.lVal = 2; // Assertive;

        hr = ((IAccPropServices*)(_this.pAccPropServices))->lpVtbl->SetHwndProp(
            _this.pAccPropServices,
            _this.hAccLabel,
            OBJID_CLIENT,
            CHILDID_SELF,
            LiveSetting_Property_GUID,
            var
        );
    }

    if (IsThemeActive() && !IsHighContrast())
    {
        if (bIsCompositionEnabled)
        {
            BOOL value = 1;
            DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFFECT, &value, sizeof(BOOL));
            /*WTA_OPTIONS ops;
            ops.dwFlags = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
            ops.dwMask = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
            SetWindowThemeAttribute(
                hwnd,
                WTA_NONCLIENT,
                &ops,
                sizeof(WTA_OPTIONS)
            );*/
        }
    }
    ShowWindow(hwnd, SW_SHOW);
    if (hKey)
    {
        RegCloseKey(hKey);
    }

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (_this.pAccPropServices != NULL)
    {
        MSAAPROPID props[] = { LiveSetting_Property_GUID };
        ((IAccPropServices*)(_this.pAccPropServices))->lpVtbl->ClearHwndProps(
            _this.pAccPropServices,
            _this.hAccLabel,
            OBJID_CLIENT,
            CHILDID_SELF,
            props,
            ARRAYSIZE(props));

        ((IAccPropServices*)(_this.pAccPropServices))->lpVtbl->Release(_this.pAccPropServices);
        _this.pAccPropServices = NULL;
    }

    DestroyWindow(_this.hAccLabel);

    if (_this.hExplorerFrame)
    {
        FreeLibrary(_this.hExplorerFrame);
    }

    if (hShell32)
    {
        CloseHandle(_this.hIcon);
        FreeLibrary(hShell32);
    }

    if (bHasLoadedUxtheme && hUxtheme)
    {
        FreeLibrary(hUxtheme);
    }

    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
    _CrtDumpMemoryLeaks();
#ifdef _DEBUG
    _getch();
#endif

    printf("Ended \"GUI\" thread.\n");
}
