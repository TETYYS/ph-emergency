#pragma once

#include <phdk.h>

#define EMERGENCY_DESKTOP L"Emergency"
#define DESKTOP_SETTING L"TT.Emergency.Desktop"

BOOL InEmergency;

LRESULT CALLBACK LowLevelKeyboardProc(
	_In_ int    nCode,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
	);

VOID NTAPI UnloadCallback(
	__in_opt PVOID Parameter,
	__in_opt PVOID Context
	);

VOID MsgPumpForHook(
	HINSTANCE *hLib
	);

INT_PTR CALLBACK OptionsDlgProc(
	_In_ HWND hwndDlg,
	_In_ UINT uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
	);

VOID ShowOptionsCallback(
	__in_opt PVOID Parameter,
	__in_opt PVOID Context
	);

PPH_PLUGIN PluginInstance;
PH_CALLBACK_REGISTRATION PluginUnloadCallbackRegistration;
PH_CALLBACK_REGISTRATION ShowOptionsCallbackRegistration;

HDESK Desktop;

HHOOK hHook;
HANDLE hHookMutex;
BOOL CtrlEnabled;
BOOL ShiftEnabled;
BOOL AltEnabled;
BOOL Switching;