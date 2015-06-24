#include <phdk.h>
#include "runas.h"
#include "phsvccl.h"

static WCHAR RunAsOldServiceName[32] = L"";
static PH_QUEUED_LOCK RunAsOldServiceLock = PH_QUEUED_LOCK_INIT;

static ULONG PhSvcReferenceCount = 0;
static PH_QUEUED_LOCK PhSvcStartLock = PH_QUEUED_LOCK_INIT;

/**
* Starts a program as another user.
*
* \param hWnd A handle to the parent window.
* \param Program The command line of the program to start.
* \param UserName The user to start the program as. The user
* name should be specified as: domain\\name. This parameter
* can be NULL if \a ProcessIdWithToken is specified.
* \param Password The password for the specified user. If there
* is no password, specify an empty string. This parameter
* can be NULL if \a ProcessIdWithToken is specified.
* \param LogonType The logon type for the specified user. This
* parameter can be 0 if \a ProcessIdWithToken is specified.
* \param ProcessIdWithToken The ID of a process from which
* to duplicate the token.
* \param SessionId The ID of the session to run the program
* under.
* \param DesktopName The window station and desktop to run the
* program under.
* \param UseLinkedToken Uses the linked token if possible.
*
* \retval STATUS_CANCELLED The user cancelled the operation.
*
* \remarks This function will cause another instance of
* Process Hacker to be executed if the current security context
* does not have sufficient system access. This is done
* through a UAC elevation prompt.
*/
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
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PH_RUNAS_SERVICE_PARAMETERS parameters;
	WCHAR serviceName[32];
	PPH_STRING portName;
	UNICODE_STRING portNameUs;

	memset(&parameters, 0, sizeof(PH_RUNAS_SERVICE_PARAMETERS));
	parameters.ProcessId = (ULONG)ProcessIdWithToken;
	parameters.UserName = UserName;
	parameters.Password = Password;
	parameters.LogonType = LogonType;
	parameters.SessionId = SessionId;
	parameters.CommandLine = CommandLine;
	parameters.DesktopName = DesktopName;
	parameters.UseLinkedToken = UseLinkedToken;
	parameters.FileName = FileName;

	// Try to use an existing instance of the service if possible.
	if (RunAsOldServiceName[0] != 0)
	{
		PhAcquireQueuedLockExclusive(&RunAsOldServiceLock);

		portName = PhConcatStrings2(L"\\BaseNamedObjects\\", RunAsOldServiceName);
		PhStringRefToUnicodeString(&portName->sr, &portNameUs);

		if (NT_SUCCESS(PhSvcConnectToServer(&portNameUs, 0)))
		{
			parameters.ServiceName = RunAsOldServiceName;
			status = PhSvcCallInvokeRunAsService(&parameters);
			PhSvcDisconnectFromServer();

			PhDereferenceObject(portName);
			PhReleaseQueuedLockExclusive(&RunAsOldServiceLock);

			return status;
		}

		PhDereferenceObject(portName);
		PhReleaseQueuedLockExclusive(&RunAsOldServiceLock);
	}

	// An existing instance was not available. Proceed normally.

	memcpy(serviceName, L"ProcessHacker", 13 * sizeof(WCHAR));
	PhGenerateRandomAlphaString(&serviceName[13], 16);
	PhAcquireQueuedLockExclusive(&RunAsOldServiceLock);
	memcpy(RunAsOldServiceName, serviceName, sizeof(serviceName));
	PhReleaseQueuedLockExclusive(&RunAsOldServiceLock);

	parameters.ServiceName = serviceName;

	if (PhElevated)
	{
		status = PhExecuteRunAsCommand(&parameters);
	}
	else
	{
		if (PhUiConnectToPhSvc(hWnd, FALSE))
		{
			status = PhSvcCallExecuteRunAsCommand(&parameters);
			PhUiDisconnectFromPhSvc();
		}
		else
		{
			status = STATUS_CANCELLED;
		}
	}

	return status;
}

/**
* Connects to phsvc.
*
* \param hWnd The window to display user interface components on.
* \param ConnectOnly TRUE to only try to connect to phsvc, otherwise
* FALSE to try to elevate and start phsvc if the initial connection
* attempt failed.
*/
BOOLEAN PhUiConnectToPhSvc(
	_In_ HWND hWnd,
	_In_ BOOLEAN ConnectOnly
	)
{
	NTSTATUS status;
	BOOLEAN started;
	UNICODE_STRING portName;

	if (_InterlockedIncrementNoZero(&PhSvcReferenceCount))
	{
		started = TRUE;
	}
	else
	{
		PhAcquireQueuedLockExclusive(&PhSvcStartLock);

		if (PhSvcReferenceCount == 0)
		{
			started = FALSE;
			RtlInitUnicodeString(&portName, PHSVC_PORT_NAME);

			// Try to connect first, then start the server if we failed.
			status = PhSvcConnectToServer(&portName, 0);

			if (NT_SUCCESS(status))
			{
				started = TRUE;
				_InterlockedIncrement(&PhSvcReferenceCount);
			}
			else if (!ConnectOnly)
			{
				// Prompt for elevation, and then try to connect to the server.

				if (PhShellProcessHacker(
					hWnd,
					L"-phsvc",
					SW_HIDE,
					PH_SHELL_EXECUTE_ADMIN,
					PH_SHELL_APP_PROPAGATE_PARAMETERS,
					0,
					NULL
					))
				{
					started = TRUE;
				}

				if (started)
				{
					ULONG attempts = 10;
					LARGE_INTEGER interval;

					// Try to connect several times because the server may take
					// a while to initialize.
					do
					{
						status = PhSvcConnectToServer(&portName, 0);

						if (NT_SUCCESS(status))
							break;

						interval.QuadPart = -50 * PH_TIMEOUT_MS;
						NtDelayExecution(FALSE, &interval);
					} while (--attempts != 0);

					// Increment the reference count even if we failed.
					// We don't want to prompt the user again.

					_InterlockedIncrement(&PhSvcReferenceCount);
				}
			}
		}
		else
		{
			started = TRUE;
			_InterlockedIncrement(&PhSvcReferenceCount);
		}

		PhReleaseQueuedLockExclusive(&PhSvcStartLock);
	}

	return started;
}

/**
* Disconnects from phsvc.
*/
VOID PhUiDisconnectFromPhSvc(
	VOID
	)
{
	PhAcquireQueuedLockExclusive(&PhSvcStartLock);

	if (_InterlockedDecrement(&PhSvcReferenceCount) == 0)
	{
		PhSvcDisconnectFromServer();
	}

	PhReleaseQueuedLockExclusive(&PhSvcStartLock);
}

/**
* Executes the run-as service.
*
* \param Parameters The run-as parameters.
*
* \remarks This function requires administrator-level access.
*/
NTSTATUS PhExecuteRunAsCommand(
	_In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
	)
{
	NTSTATUS status;
	ULONG win32Result;
	PPH_STRING commandLine;
	SC_HANDLE scManagerHandle;
	SC_HANDLE serviceHandle;
	PPH_STRING portName;
	UNICODE_STRING portNameUs;
	ULONG attempts;
	LARGE_INTEGER interval;
	WCHAR fullPath[MAX_PATH];

	if (!(scManagerHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE)))
		return PhGetLastWin32ErrorAsNtStatus();

	GetModuleFileName(NULL, fullPath, MAX_PATH);

	commandLine = PhFormatString(L"\"%s\" -ras \"%s\"", fullPath, Parameters->ServiceName);

	serviceHandle = CreateService(
		scManagerHandle,
		Parameters->ServiceName,
		Parameters->ServiceName,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_IGNORE,
		commandLine->Buffer,
		NULL,
		NULL,
		NULL,
		L"LocalSystem",
		L""
		);
	win32Result = GetLastError();

	PhDereferenceObject(commandLine);

	CloseServiceHandle(scManagerHandle);

	if (!serviceHandle)
	{
		return NTSTATUS_FROM_WIN32(win32Result);
	}

	PhSetDesktopWinStaAccess();

	StartService(serviceHandle, 0, NULL);
	DeleteService(serviceHandle);

	portName = PhConcatStrings2(L"\\BaseNamedObjects\\", Parameters->ServiceName);
	PhStringRefToUnicodeString(&portName->sr, &portNameUs);
	attempts = 10;

	// Try to connect several times because the server may take
	// a while to initialize.
	do
	{
		status = PhSvcConnectToServer(&portNameUs, 0);

		if (NT_SUCCESS(status))
			break;

		interval.QuadPart = -50 * PH_TIMEOUT_MS;
		NtDelayExecution(FALSE, &interval);
	} while (--attempts != 0);

	PhDereferenceObject(portName);

	if (NT_SUCCESS(status))
	{
		status = PhSvcCallInvokeRunAsService(Parameters);
		PhSvcDisconnectFromServer();
	}

	if (serviceHandle)
		CloseServiceHandle(serviceHandle);

	return status;
}

/**
* Sets the access control lists of the current window station
* and desktop to allow all access.
*/
VOID PhSetDesktopWinStaAccess(
	VOID
	)
{
	static SID_IDENTIFIER_AUTHORITY appPackageAuthority = SECURITY_APP_PACKAGE_AUTHORITY;

	HWINSTA wsHandle;
	HDESK desktopHandle;
	ULONG allocationLength;
	PSECURITY_DESCRIPTOR securityDescriptor;
	PACL dacl;
	CHAR allAppPackagesSidBuffer[FIELD_OFFSET(SID, SubAuthority) + sizeof(ULONG)* 2];
	PSID allAppPackagesSid;

	// TODO: Set security on the correct window station and desktop.

	allAppPackagesSid = (PISID)allAppPackagesSidBuffer;
	RtlInitializeSid(allAppPackagesSid, &appPackageAuthority, SECURITY_BUILTIN_APP_PACKAGE_RID_COUNT);
	*RtlSubAuthoritySid(allAppPackagesSid, 0) = SECURITY_APP_PACKAGE_BASE_RID;
	*RtlSubAuthoritySid(allAppPackagesSid, 1) = SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE;

	// We create a DACL that allows everyone to access everything.

	SID everyone = { SID_REVISION, 1, SECURITY_WORLD_SID_AUTHORITY, { SECURITY_WORLD_RID } };

	allocationLength = SECURITY_DESCRIPTOR_MIN_LENGTH +
		(ULONG)sizeof(ACL)+
		(ULONG)sizeof(ACCESS_ALLOWED_ACE)+
		RtlLengthSid(&everyone) +
		(ULONG)sizeof(ACCESS_ALLOWED_ACE)+
		RtlLengthSid(allAppPackagesSid);
	securityDescriptor = PhAllocate(allocationLength);
	dacl = (PACL)((PCHAR)securityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);

	RtlCreateSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION);

	RtlCreateAcl(dacl, allocationLength - SECURITY_DESCRIPTOR_MIN_LENGTH, ACL_REVISION);
	RtlAddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_ALL, &everyone);

	if (WindowsVersion >= WINDOWS_8)
	{
		RtlAddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_ALL, allAppPackagesSid);
	}

	RtlSetDaclSecurityDescriptor(securityDescriptor, TRUE, dacl, FALSE);

	if (wsHandle = OpenWindowStation(
		L"WinSta0",
		FALSE,
		WRITE_DAC
		))
	{
		PhSetObjectSecurity(wsHandle, DACL_SECURITY_INFORMATION, securityDescriptor);
		CloseWindowStation(wsHandle);
	}

	if (desktopHandle = OpenDesktop(
		L"Default",
		0,
		FALSE,
		WRITE_DAC | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS
		))
	{
		PhSetObjectSecurity(desktopHandle, DACL_SECURITY_INFORMATION, securityDescriptor);
		CloseDesktop(desktopHandle);
	}

	PhFree(securityDescriptor);
}

static VOID PhpSplitUserName(
	_In_ PWSTR UserName,
	_Out_ PPH_STRING *DomainPart,
	_Out_ PPH_STRING *UserPart
	)
{
	PH_STRINGREF userName;
	PH_STRINGREF domainPart;
	PH_STRINGREF userPart;

	PhInitializeStringRef(&userName, UserName);

	if (PhSplitStringRefAtChar(&userName, '\\', &domainPart, &userPart))
	{
		*DomainPart = PhCreateStringEx(domainPart.Buffer, domainPart.Length);
		*UserPart = PhCreateStringEx(userPart.Buffer, userPart.Length);
	}
	else
	{
		*DomainPart = NULL;
		*UserPart = PhCreateStringEx(userName.Buffer, userName.Length);
	}
}

NTSTATUS PhInvokeRunAsService(
	_In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
	)
{
	NTSTATUS status;
	PPH_STRING domainName;
	PPH_STRING userName;
	PH_CREATE_PROCESS_AS_USER_INFO createInfo;
	ULONG flags;

	if (Parameters->UserName)
	{
		PhpSplitUserName(Parameters->UserName, &domainName, &userName);
	}
	else
	{
		domainName = NULL;
		userName = NULL;
	}

	memset(&createInfo, 0, sizeof(PH_CREATE_PROCESS_AS_USER_INFO));
	createInfo.ApplicationName = Parameters->FileName;
	createInfo.CommandLine = Parameters->CommandLine;
	createInfo.CurrentDirectory = Parameters->CurrentDirectory;
	createInfo.DomainName = PhGetString(domainName);
	createInfo.UserName = PhGetString(userName);
	createInfo.Password = Parameters->Password;
	createInfo.LogonType = Parameters->LogonType;
	createInfo.SessionId = Parameters->SessionId;
	createInfo.DesktopName = Parameters->DesktopName;

	flags = PH_CREATE_PROCESS_SET_SESSION_ID;

	if (Parameters->ProcessId)
	{
		createInfo.ProcessIdWithToken = UlongToHandle(Parameters->ProcessId);
		flags |= PH_CREATE_PROCESS_USE_PROCESS_TOKEN;
	}

	if (Parameters->UseLinkedToken)
		flags |= PH_CREATE_PROCESS_USE_LINKED_TOKEN;

	status = PhCreateProcessAsUser(
		&createInfo,
		flags,
		NULL,
		NULL,
		NULL
		);

	if (domainName) PhDereferenceObject(domainName);
	if (userName) PhDereferenceObject(userName);

	return status;
}