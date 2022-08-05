#include <windows.h>
#include <wchar.h>
#include <stdio.h>

HHOOK g_hkMouse;
HHOOK g_hkKeyboard;
RECT g_rcMonitors[32];
RECT g_rcTaskbars[32];
HWND g_hwTaskbars[32];
INT g_iMonitorCount;

HWINEVENTHOOK g_hWinEventHook;

void DebugOut(WCHAR* format, ...)
{
	va_list argp;
	WCHAR dbg_out[4096];

	va_start(argp, format);
	vswprintf_s(dbg_out, ARRAYSIZE(dbg_out), format, argp);
	va_end(argp);
	OutputDebugString(dbg_out);
}

VOID CleanupHooksExit(UINT uiExitCode)
{
	if (g_hkMouse)
		UnhookWindowsHookEx(g_hkMouse);
	if (g_hkKeyboard)
		UnhookWindowsHookEx(g_hkKeyboard);
	if (g_hWinEventHook)
		UnhookWinEvent(g_hWinEventHook);

	ExitProcess(uiExitCode);
}


DWORD WINAPI ThreadAskUserExit(LPVOID lpThreadParameter)
{
	INT iRet = 0;

	iRet = MessageBox(NULL, L"Do you want to close Taskbar Helper?", L"Closing Taskbar Helper", MB_YESNO);
	if (iRet == IDYES) {
		CleanupHooksExit(EXIT_SUCCESS);
	}

	return 0;
}

BOOL GetProcessName(DWORD dwProcessID, PWSTR pwProcessName, PDWORD dwProcessNameLength)
{
	BOOL blRet;
	HANDLE hProcess;

	hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessID);
	if (!hProcess)
		return FALSE;

	blRet = QueryFullProcessImageName(hProcess, 0, pwProcessName, dwProcessNameLength);
	CloseHandle(hProcess);

	return blRet;
}

BOOL IsShellWndVisible(HWND hwShellWnd)
{
	HWND hw;
	GUITHREADINFO gti = { .cbSize = sizeof(GUITHREADINFO) };

	if (!GetGUIThreadInfo(0, &gti))
		return FALSE;

	if (gti.hwndActive) {
		DWORD dwProcessID;
		WCHAR wcProcessName[1024] = { L'\0' };
		DWORD dwProcessNameLength = ARRAYSIZE(wcProcessName);

		GetWindowThreadProcessId(gti.hwndActive, &dwProcessID);
		if (GetProcessName(dwProcessID, wcProcessName, &dwProcessNameLength) &&
			wcsstr(wcProcessName, L"ShellExperienceHost.exe") != NULL)
			return TRUE;
	}

	for (hw = gti.hwndActive; hw != NULL; hw = GetParent(hw)) {
		if (hw == hwShellWnd) {
			return TRUE;
		}
	}

	return FALSE;
}

HWND lastHandle;
LRESULT CALLBACK HookMouseCallback(INT nCode, WPARAM wParam, LPARAM lParam)
{
	int i;

	if (nCode < 0 ||
		wParam != WM_MOUSEMOVE)
		return CallNextHookEx(g_hkMouse, nCode, wParam, lParam);

	for (i = 0; i < ARRAYSIZE(g_rcTaskbars); ++i) {
		if (g_rcTaskbars[i].left == 0 &&
			g_rcTaskbars[i].right == 0 &&
			g_rcTaskbars[i].top == 0 &&
			g_rcTaskbars[i].bottom == 0) {
			break;
		}

		QUERY_USER_NOTIFICATION_STATE pquns;
		SHQueryUserNotificationState(&pquns);

		if (PtInRect(&g_rcTaskbars[i], ((PMSLLHOOKSTRUCT)lParam)->pt) &&
			!IsShellWndVisible(g_hwTaskbars[i])
			&& (pquns == 2 || pquns == 3 || pquns == 4 || pquns == 3)) {
			GUITHREADINFO gti = { .cbSize = sizeof(GUITHREADINFO) };

			lastHandle = GetForegroundWindow();
			SetForegroundWindow(g_hwTaskbars[i]);
		}

		if (!PtInRect(&g_rcTaskbars[i], ((PMSLLHOOKSTRUCT)lParam)->pt) &&
			lastHandle != NULL) {
			SetForegroundWindow(lastHandle);
			lastHandle = NULL;
		}
	}

	return CallNextHookEx(g_hkMouse, nCode, wParam, lParam);
}

LRESULT CALLBACK HookKeyboardCallback(INT nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode < 0)
		return CallNextHookEx(g_hkKeyboard, nCode, wParam, lParam);

	if (wParam == WM_KEYDOWN &&
		((PKBDLLHOOKSTRUCT)lParam)->vkCode == VK_INSERT) {
		HANDLE hThread = NULL;

		hThread = CreateThread(NULL, 0, ThreadAskUserExit, NULL, 0, NULL);
		if (hThread)
			CloseHandle(hThread);
		return (-1);
	}

	return CallNextHookEx(g_hkKeyboard, nCode, wParam, lParam);
}

VOID CALLBACK HookWinEventCallback(HWINEVENTHOOK hWinEventHook, DWORD dwEvent, HWND hWnd, LONG lObject, LONG lChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	DebugOut(L"dwEvent: %x hWnd: %p lObject: %x lChild: %x dwmsEventTime: %x\n", dwEvent, hWnd, lObject, lChild, dwmsEventTime);
}

BOOL CALLBACK EnumMonitorCallback(HMONITOR hMonitor, HDC hDC, LPRECT lpRect, LPARAM lParam)
{
	g_rcMonitors[g_iMonitorCount++] = *lpRect;
	return TRUE;
}

BOOL CALLBACK EnumWindowCallback(HWND hWnd, LPARAM lParam)
{
	INT i;
	RECT rc;

	WCHAR wcClassName[ARRAYSIZE(L"Shell_TrayWnd")] = { L'\0' };

	if (RealGetWindowClass(hWnd, wcClassName, ARRAYSIZE(L"Shell_TrayWnd")) != (ARRAYSIZE(L"Shell_TrayWnd") - 1) ||
		wcscmp(wcClassName, L"Shell_TrayWnd") != 0)
		return TRUE;

	GetWindowRect(hWnd, &rc);
	for (i = 0; i < ARRAYSIZE(g_rcMonitors); ++i) {
		if (PtInRect(&g_rcMonitors[i], (*((POINT*)&rc)))) {
			g_hwTaskbars[i] = hWnd;
			g_rcTaskbars[i] = rc;
			break;
		}
	}

	return TRUE;
}

INT WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ INT nCmdShow)
{
	MSG msg;

	EnumDisplayMonitors((HDC)NULL, (LPRECT)NULL, EnumMonitorCallback, (LPARAM)NULL);
	EnumWindows(EnumWindowCallback, (LPARAM)NULL);

	g_hkKeyboard = SetWindowsHookEx(WH_KEYBOARD_LL, HookKeyboardCallback, hInstance, 0);
	if (!g_hkKeyboard) {
		CleanupHooksExit(EXIT_FAILURE);
	}

	g_hkMouse = SetWindowsHookEx(WH_MOUSE_LL, HookMouseCallback, hInstance, 0);
	if (!g_hkMouse) {
		CleanupHooksExit(EXIT_FAILURE);
	}

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return EXIT_SUCCESS;
}