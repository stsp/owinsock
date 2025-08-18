/*
 *  owinsock - winsock-1.1/win16 (winsock.dll) for OpenWatcom
 *  Copyright (C) 2025  @stsp
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int idComm;
_WCRTLINK void _set_blocking_hook(int (far * hook) (void));

struct per_task {
    HTASK task;
    FARPROC BlockingHook;
    int cancel;
    int wsa_err;
};
#define MAX_TASKS 10
struct per_task tasks[MAX_TASKS];
static int num_tasks;

static void debug_out(const char *msg)
{
    if (idComm > 0)
	WriteComm(idComm, msg, strlen(msg));
}

#define _ENT() debug_out("enter: " __FUNCTION__ "\r\n")
#define DEBUG_STR(...) { \
	char _buf[128]; \
	snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
	debug_out(_buf); \
}

static void task_alloc(HTASK task)
{
    struct per_task *ret;
    int i;

    assert(task);
    for (i = 0; i < num_tasks; i++) {
        if (!tasks[i].task)
            break;
    }
    if (i == num_tasks) {
        assert(num_tasks < MAX_TASKS);
        num_tasks++;
    }
    ret = &tasks[i];
    memset(ret, 0, sizeof(*ret));
    ret->task = task;
}

static void task_free(struct per_task *task)
{
    task->task = NULL;
    while (num_tasks && !tasks[num_tasks - 1].task)
        num_tasks--;
}

static struct per_task *task_find(HTASK task)
{
    int i;

    for (i = 0; i < num_tasks; i++) {
	if (tasks[i].task == task)
	    return &tasks[i];
    }
    return NULL;
}

int FAR PASCAL __WSAFDIsSet(SOCKET s, fd_set FAR *pfds)
{
    int i;

    _ENT();
    for (i = 0; i < pfds->fd_count; i++) {
	if (pfds->fd_array[i] == s)
	    return TRUE;
    }
    return FALSE;
}

static BOOL far pascal DefaultBlockingHook(void)
{
    MSG msg;
    BOOL ret;
    /* get the next message if any */
    ret = (BOOL) PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
    /* if we got one, process it */
    if (ret) {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }
    /* TRUE if we got a message */
    return ret;
}

static int blk_func(void)
{
    FARPROC BlockingHook;
    struct per_task *task = task_find(GetCurrentTask());

    assert(task);
    BlockingHook = task->BlockingHook ? task->BlockingHook : DefaultBlockingHook;
    /* flush messages for good user response */
    while (BlockingHook());
    /* check for WSACancelBlockingCall() */
    if (task->cancel) {
        task->cancel = 0;
        return 0;
    }
    return 1;
}

BOOL FAR PASCAL LibMain(HINSTANCE hInstance, WORD wDataSegment,
			WORD wHeapSize, LPSTR lpszCmdLine)
{
    _ENT();
    idComm = OpenComm("COM4", 16384, 16384);
    _set_blocking_hook(blk_func);
    return 1;
}

#pragma off (unreferenced);
int FAR PASCAL WEP(int nParameter)
#pragma on (unreferenced);
{
    _ENT();
    _set_blocking_hook(NULL);
    if (idComm > 0)
	CloseComm(idComm);
    return (1);
}

HANDLE pascal far WSAAsyncGetServByName(HWND hWnd, u_int wMsg,
					const char FAR *name,
					const char FAR *proto,
					char FAR *buf, int buflen)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    /* Not supported */
    task->wsa_err = WSAEOPNOTSUPP;
    return 0;
}

HANDLE pascal far WSAAsyncGetServByPort(HWND hWnd, u_int wMsg, int port,
					const char FAR *proto,
					char FAR *buf, int buflen)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    /* Not supported */
    task->wsa_err = WSAEOPNOTSUPP;
    return 0;
}

HANDLE pascal far WSAAsyncGetProtoByName(HWND hWnd, u_int wMsg,
					 const char FAR *name,
					 char FAR *buf, int buflen)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    /* Not supported */
    task->wsa_err = WSAEOPNOTSUPP;
    return 0;
}

HANDLE pascal far WSAAsyncGetProtoByNumber(HWND hWnd, u_int wMsg,
					   int number, char FAR *buf,
					   int buflen)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    /* Not supported */
    task->wsa_err = WSAEOPNOTSUPP;
    return 0;
}

HANDLE pascal far WSAAsyncGetHostByName(HWND hWnd, u_int wMsg,
					const char FAR *name,
					char FAR *buf, int buflen)
{
    _ENT();
    /* TODO! */
    return 0;
}

HANDLE pascal far WSAAsyncGetHostByAddr(HWND hWnd, u_int wMsg,
					const char FAR *addr, int len,
					int type, char FAR *buf,
					int buflen)
{
    _ENT();
    /* TODO! */
    return 0;
}

int pascal far WSACancelAsyncRequest(HANDLE hAsyncTaskHandle)
{
    _ENT();
    /* TODO! */
    return 0;
}

int pascal far WSAAsyncSelect(SOCKET s, HWND hWnd, u_int wMsg, long lEvent)
{
    _ENT();
    /* TODO! */
    return 0;
}

int pascal far WSAStartup(WORD wVersionRequired, LPWSADATA lpWSAData)
{
    _ENT();
    lpWSAData->wVersion = 0x0101;
    lpWSAData->wHighVersion = 0x0101;
    strcpy(lpWSAData->szDescription,
		"OWinSock - dosemu2 sockets. "
		"Copyright 2025 @stsp. "
		"OWinSock is free software, GPLv3+. ");
    strcpy(lpWSAData->szSystemStatus, "Ready.");
    lpWSAData->iMaxSockets = 256;
    lpWSAData->iMaxUdpDg = 512;
    lpWSAData->lpVendorInfo = 0;
    if (wVersionRequired == 0x0001)
	return WSAVERNOTSUPPORTED;
    task_alloc(GetCurrentTask());
    return 0;
}

int pascal far WSACleanup(void)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    assert(task);
    task_free(task);
    return 0;
}

void pascal far WSASetLastError(int iError)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    task->wsa_err = iError;
}

int pascal far WSAGetLastError(void)
{
    struct per_task *task = task_find(GetCurrentTask());
    _ENT();
    return task->wsa_err;
}

BOOL pascal far WSAIsBlocking(void)
{
    _ENT();
    /* TODO! */
    return 0;
}

int pascal far WSAUnhookBlockingHook(void)
{
    FARPROC BlockingHook;
    struct per_task *task = task_find(GetCurrentTask());

    _ENT();
    assert(task);
    task->BlockingHook = NULL;
    return 0;
}

FARPROC pascal far WSASetBlockingHook(FARPROC lpBlockFunc)
{
    FARPROC ret;
    struct per_task *task = task_find(GetCurrentTask());

    _ENT();
    assert(task);
    ret = task->BlockingHook;
    task->BlockingHook = lpBlockFunc;
    return ret;
}

int pascal far WSACancelBlockingCall(void)
{
    struct per_task *task = task_find(GetCurrentTask());

    _ENT();
    assert(task);
    task->cancel++;
    return 0;
}
