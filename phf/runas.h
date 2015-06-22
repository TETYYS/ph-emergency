#pragma once

#include <phdk.h>
#include "phsvcapi.h"

NTSTATUS PhExecuteRunAsCommand(
	_In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
	);

VOID PhSetDesktopWinStaAccess(
	VOID
	);

NTSTATUS PhSvcConnectToServer(
	_In_ PUNICODE_STRING PortName,
	_In_opt_ SIZE_T PortSectionSize
	);

NTSTATUS PhInvokeRunAsService(
	_In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
	);

BOOLEAN PhUiConnectToPhSvc(
	_In_ HWND hWnd,
	_In_ BOOLEAN ConnectOnly
	);

VOID PhUiDisconnectFromPhSvc(
	VOID
	);

NTSTATUS PhExecuteRunAsCommand2(
	_In_ HWND hWnd,
	_In_ PWSTR FileName,
	_In_ PWSTR CommandLine,
	_In_opt_ PWSTR UserName,
	_In_opt_ PWSTR Password,
	_In_opt_ ULONG LogonType,
	_In_opt_ HANDLE ProcessIdWithToken,
	_In_ ULONG SessionId,
	_In_ PWSTR DesktopName,
	_In_ BOOLEAN UseLinkedToken
	);

HANDLE PhSvcClPortHandle;
PVOID PhSvcClPortHeap;
HANDLE PhSvcClServerProcessId;
