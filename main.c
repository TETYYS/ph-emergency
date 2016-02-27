#include "main.h"
#include <phdk.h>
#include "resource.h"

LOGICAL DllMain(
	__in HINSTANCE Instance,
	__in ULONG Reason,
	__reserved PVOID Reserved
	)
{
	switch (Reason)
	{
	case DLL_PROCESS_ATTACH:
	{
		PPH_PLUGIN_INFORMATION info;

		PluginInstance = PhRegisterPlugin(L"TT.Emergency", Instance, &info);

		if (!PluginInstance)
			return FALSE;

		info->DisplayName = L"Emergency";
		info->Author = L"TETYYS";
		info->Description = L"Brings up ProcessHacker in emergency situations";
		info->Url = L"http://wj32.org/processhacker/forums/viewtopic.php?f=18&t=1954";
		info->HasOptions = TRUE;

		ULONG major, minor;
		PhGetPhVersionNumbers(&major, &minor, NULL, NULL);
		if (major < 2 || minor < 38) {
			PhShowMessage(NULL, MB_ICONERROR, L"%s%d%d%d%s", L"Your Process Hacker version is not supported by Emergency plugin, please update Process Hacker or plugin will stay disabled. (Requires version 2.38)");
			info->HasOptions = FALSE;
			return FALSE;
		}

		Switching = FALSE;

		{
			static PH_SETTING_CREATE settings[] =
			{
				{ StringSettingType, DESKTOP_SETTING, L"WinSTA0\\Emergency" }
			};

			PhAddSettings(settings, sizeof(settings) / sizeof(PH_SETTING_CREATE));
		}

		PPH_STRING CommandLine = PhCreateString(L"");
		PhGetProcessPebString(GetCurrentProcess(), PhpoCommandLine, &CommandLine);

		if (PhFindStringInString(CommandLine, 0, L"--EmergencySwitch") != -1) {
			InEmergency = TRUE;
		}

		PhRegisterCallback(
			PhGetPluginCallback(PluginInstance, PluginCallbackUnload),
			UnloadCallback,
			NULL,
			&PluginUnloadCallbackRegistration
			);

		if (InEmergency) {
			info->HasOptions = FALSE;
			ULONG_PTR index = PhFindStringInString(CommandLine, 0, L"--EmergencySwitch") + 17 + 1 /* space */;

			HDESK hDesktop = CreateDesktop(PhSubstring(CommandLine, index, CommandLine->Length - index)->Buffer, NULL, NULL, 0, DESKTOP_ALL_ACCESS, NULL);
			SwitchDesktop(hDesktop);
			CloseDesktop(hDesktop);
		} else {
			PhRegisterCallback(
				PhGetPluginCallback(PluginInstance, PluginCallbackShowOptions),
				ShowOptionsCallback,
				NULL,
				&ShowOptionsCallbackRegistration
				);

			if (PhGetOwnTokenAttributes().Elevated) {
				hHookMutex = CreateMutex(NULL, FALSE, L"Emergency_MtxWaitForHook");
				if (hHookMutex == NULL)
					return GetLastError();

				CreateThread(NULL, 1024 * 1024, (LPTHREAD_START_ROUTINE)MsgPumpForHook, (VOID*)Instance, 0, NULL);

				DWORD result = WaitForSingleObject(hHookMutex, 5000);

				if (result == WAIT_OBJECT_0)
					return TRUE;
				else {
					DWORD status = GetLastError();
					PhShowMessage(NULL, MB_ICONERROR | MB_OK, L"Emergency failed to load: Failed to SetWindowsHookEx, code %d", status);
					return status;
				}
			}
		}
	}
		break;
	}
	return TRUE;
}

VOID ShowOptionsCallback(
	__in_opt PVOID Parameter,
	__in_opt PVOID Context
	)
{
	if (!PhGetOwnTokenAttributes().Elevated)
		PhShowMessage(NULL, MB_ICONEXCLAMATION | MB_OK, L"Please note that Emergency plugin requires elevated privileges to work.");

	DialogBox(PluginInstance->DllBase,
		MAKEINTRESOURCE(IDD_OPTIONS),
		(HWND)Parameter,
		OptionsDlgProc);
}

static BOOL CALLBACK EnumDesktopsCallback(
	_In_ PWSTR DesktopName,
	_In_ LPARAM Context
	)
{
	PhAddItemList((PPH_LIST)Context, PhConcatStrings(
		2,
		L"WinSTA0\\",
		DesktopName
		));

	return TRUE;
}

INT_PTR CALLBACK OptionsDlgProc(
	_In_ HWND hwndDlg,
	_In_ UINT uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
	)
{
	switch (uMsg) {
	case WM_INITDIALOG:
	{
		PPH_STRING desktop = PhGetStringSetting(DESKTOP_SETTING);
		NOTHING;
		SetDlgItemText(hwndDlg, IDC_DESKTOP, desktop->Buffer);
		PhDereferenceObject(desktop);
		break;
	}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_SELECTDESKTOP:
		{
			PPH_EMENU desktopsMenu;
			ULONG i;
			RECT buttonRect;
			PPH_EMENU_ITEM selectedItem;
			PPH_LIST desktopList;
			BOOL emergencyExists = FALSE;

			desktopsMenu = PhCreateEMenu();

			desktopList = PhCreateList(10);

			EnumDesktops(GetProcessWindowStation(), EnumDesktopsCallback, (LPARAM)desktopList);

			for (i = 0; i < desktopList->Count; i++)
			{
				PhInsertEMenuItem(
					desktopsMenu,
					PhCreateEMenuItem(0, 0, ((PPH_STRING)desktopList->Items[i])->Buffer, NULL, NULL),
					-1
					);
				if (PhCompareString2((PPH_STRING)desktopList->Items[i], L"WinSTA0\\Emergency", TRUE))
					emergencyExists = TRUE;
			}

			if (!emergencyExists)
				PhInsertEMenuItem(
				desktopsMenu,
				PhCreateEMenuItem(0, 0, L"WinSTA0\\Emergency", NULL, NULL),
				-1
				);

			GetWindowRect(GetDlgItem(hwndDlg, IDC_SELECTDESKTOP), &buttonRect);

			selectedItem = PhShowEMenu(
				desktopsMenu,
				hwndDlg,
				PH_EMENU_SHOW_LEFTRIGHT,
				PH_ALIGN_LEFT | PH_ALIGN_TOP,
				buttonRect.right,
				buttonRect.top
				);

			if (selectedItem)
				SetDlgItemText(
				hwndDlg,
				IDC_DESKTOP,
				selectedItem->Text
				);

			for (i = 0; i < desktopList->Count; i++)
				PhDereferenceObject(desktopList->Items[i]);

			PhClearList(desktopList);
			PhDestroyEMenu(desktopsMenu);
		}
			break;
		case IDCANCEL:
			EndDialog(hwndDlg, IDCANCEL);
			break;
		case IDOK:
		{
			PPH_STRING setting = PhGetWindowText(GetDlgItem(hwndDlg, IDC_DESKTOP)); {
				PhSetStringSetting(DESKTOP_SETTING, setting->Buffer);
			} PhDereferenceObject(setting);
			EndDialog(hwndDlg, IDOK);
		}
			break;
		}
		break;
	}

	return FALSE;
}

VOID MsgPumpForHook(
	HINSTANCE *hLib
	)
{
	hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
	if (hHook == NULL)
		return;

	ReleaseMutex(hHookMutex);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0));
}

VOID NTAPI UnloadCallback(
	__in_opt PVOID Parameter,
	__in_opt PVOID Context
	)
{
	if (InEmergency)
		SwitchDesktop(OpenDesktop(L"Default", 0, FALSE, DESKTOP_SWITCHDESKTOP));
	else
		UnhookWindowsHookEx(hHook);
}

static BOOL DesktopExists(PPH_STRING Desktop) {
	HDESK hDesk = OpenDesktop(Desktop->Buffer, 0, FALSE, DESKTOP_CREATEWINDOW);
	if (GetLastError() == ERROR_FILE_NOT_FOUND)
		return FALSE;
	if (hDesk != NULL)
		CloseDesktop(hDesk);
	return TRUE;
}

LRESULT CALLBACK LowLevelKeyboardProc(
	_In_ int    nCode,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
	)
{
	WPARAM identifier = wParam;
	KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT*)lParam;
	BOOL enabled;

	if (nCode != HC_ACTION)
		goto nextHook;

	enabled = identifier == WM_KEYDOWN || identifier == WM_SYSKEYDOWN;

	switch (kbd->vkCode) {
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
		CtrlEnabled = enabled;
		break;
	case VK_MENU:
	case VK_RMENU:
	case VK_LMENU:
		AltEnabled = enabled;
		break;
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
		ShiftEnabled = enabled;
		break;
	}

	if (CtrlEnabled && AltEnabled && ShiftEnabled && (kbd->vkCode == 'P' || kbd->vkCode == 'p') && !Switching) {
		Switching = TRUE;
		// GO GO GO!

		ULONG sessId;
		PhGetProcessSessionId(GetCurrentProcess(), &sessId);

		PPH_STRING desktopFull = PhGetStringSetting(DESKTOP_SETTING);
		ULONG_PTR index = PhFindCharInString(desktopFull, 0, L'\\') + 1;
		PPH_STRING desktop = PhSubstring(desktopFull, index, desktopFull->Length - index);

		PPH_STRING cmd;
		PPH_STRING app = PhGetApplicationFileName(); {
			cmd = PhFormatString(L"\"%s\" -newinstance --EmergencySwitch %s", app->Buffer, desktop->Buffer);
		} PhDereferenceObject(app);

		if (!DesktopExists(desktop)) {
			if (CreateDesktop(desktop->Buffer, NULL, NULL, 0, DESKTOP_ALL_ACCESS, NULL) == NULL) {
				PhShowMessage(NULL, MB_ICONERROR, L"Failed to create desktop %s: 0x%08x", desktopFull->Buffer, GetLastError());

				PhDereferenceObject(desktop);
				PhDereferenceObject(desktopFull);
				PhDereferenceObject(cmd);
				goto nextHook;
			}
			// do NOT close the desktop!
		}
		PhDereferenceObject(desktop);

		PhExecuteRunAsCommand2(NULL, cmd->Buffer, L"NT AUTHORITY\\SYSTEM", PhGetStringOrEmpty(NULL), LOGON32_LOGON_SERVICE, NULL, sessId, desktopFull->Buffer, FALSE);

		PhDereferenceObject(desktopFull);
		PhDereferenceObject(cmd);
		Sleep(2000);
		Switching = FALSE;
	}

nextHook:
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}