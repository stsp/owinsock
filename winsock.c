/*
 *  Open Winsock - winsock-1.1/win16 (winsock.dll) for OpenWatcom
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
_WCRTLINK void _set_blocking_hook(int (far * hook) (void *));
_WCRTLINK void _set_debug_hook(void (far *hook)(const char *));
_WCRTLINK extern void freehostent(struct hostent *he);
_WCRTLINK extern struct hostent *gethostbyname_ex( const char *__name, void *arg );

struct per_task {
    HTASK task;
    FARPROC BlockingHook;
    int cancel;
    int blocking;
    int wsa_err;
};
#define MAX_TASKS 10
struct per_task tasks[MAX_TASKS];
static int num_tasks;

static HINSTANCE hinst;
static const char *WSAClassName = "OpenWinsock WSA Window";

struct GHBN {
    HWND hWnd;
    u_int wMsg;
    const char FAR *name;
    char FAR *buf;
    int buflen;
    HANDLE id;
};

struct per_async {
    void (*handler)(struct per_async *arg);
    struct GHBN ghbn;
    int cancel;
};
#define MAX_ASYNC 256
static struct per_async asyncs[MAX_ASYNC];
static HANDLE wsa_id;
#define MAX_ASYNC_M1 (MAX_ASYNC - 1)

static void debug_out(const char *msg)
{
    if (idComm > 0)
	WriteComm(idComm, msg, strlen(msg));
}

#define _ENT() debug_out("enter: " __FUNCTION__ "\r\n")
#define _LVE() debug_out("leave: " __FUNCTION__ "\r\n")
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

static int blk_async(struct per_async *async)
{
    Yield();
    if (async->cancel)
        return 0;
    return 1;
}

static int blk_func(void *arg)
{
    struct per_task *task;

    if (arg)
        return blk_async(arg);

    task = task_find(GetCurrentTask());
    assert(task);
    if (task->blocking)
        return 0;  // avoid recursive blocking
    task->blocking++;
    if (task->BlockingHook)
        while (task->BlockingHook());
    else
        Yield();
    task->blocking--;
    /* check for WSACancelBlockingCall() */
    if (task->cancel) {
        task->cancel = 0;
        return 0;
    }
    return 1;
}

/* callback needs to be exported so that Windows can patch its prolog
 * with proper dataseg - same that it passes to LibMain() */
LRESULT CALLBACK _export WSAWindowProc(HWND hWnd, UINT wMsg,
        WPARAM wParam, LPARAM lParam)
{
    _ENT();
    if (wMsg == WM_USER) {
        switch (wParam) {
            case 0: {
                struct per_async *async = (struct per_async *)lParam;

                debug_out("\tWM_USER\r\n");
                assert(async && async->handler);
                async->handler(async);
                async->handler = NULL;
                DestroyWindow(hWnd);
                break;
            }
        }
        return 0;
    }
    DEBUG_STR("\twmsg 0x%x\r\n", wMsg);
    return DefWindowProc(hWnd, wMsg, wParam, lParam);
}

BOOL FAR PASCAL LibMain(HINSTANCE hInstance, WORD wDataSegment,
			WORD wHeapSize, LPSTR lpszCmdLine)
{
    WNDCLASS wc = {0};

    idComm = OpenComm("COM4", 16384, 16384);
    _ENT();
    DEBUG_STR("hInstance=%x dataseg=%x heapsize=%x cmdline=%s\r\n",
            hInstance, wDataSegment, wHeapSize, lpszCmdLine);
    _set_blocking_hook(blk_func);
    _set_debug_hook(debug_out);
    hinst = hInstance;

    wc.style = 0;
    wc.lpfnWndProc = WSAWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WSAClassName;
    RegisterClass(&wc);
    return 1;
}

#pragma off (unreferenced);
int FAR PASCAL WEP(int nParameter)
#pragma on (unreferenced);
{
    _ENT();
    _set_blocking_hook(NULL);
    _set_debug_hook(NULL);
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
    assert(task);
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
    assert(task);
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
    assert(task);
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
    assert(task);
    /* Not supported */
    task->wsa_err = WSAEOPNOTSUPP;
    return 0;
}

#define GHBN_RET(g, l) \
        PostMessage(g->hWnd, g->wMsg, g->id, WSAMAKEASYNCREPLY(l, 0));
#define GHBN_ERR(g, e) \
        PostMessage(g->hWnd, g->wMsg, g->id, WSAMAKEASYNCREPLY(0, e));

static void AsyncGetHostByName(struct per_async *arg)
{
    struct GHBN *ghbn = &arg->ghbn;
    struct hostent *he;
    int len, i;
    char FAR **aliases;
    char FAR **h;
    struct hostent FAR *dst = (struct hostent FAR *)ghbn->buf;
    char FAR *dstart = ghbn->buf + sizeof(struct hostent);
    char FAR *data = dstart;
    int buflen = ghbn->buflen;
    int done_len = 0;

    he = gethostbyname_ex(ghbn->name, arg);
    if (!he) {
        GHBN_ERR(ghbn, WSAHOST_NOT_FOUND);
        return;
    }
    len = sizeof(struct hostent);
    memcpy(dst, he, len);
    buflen -= len;
    done_len += len;

    len = strlen(he->h_name) + 1;
    assert(len <= buflen);
    memcpy(data, he->h_name, len);
    dst->h_name = data;
    data += len;
    buflen -= len;
    done_len += len;

#define MAXALIASES      8
    aliases = (char FAR **)data;
    dst->h_aliases = aliases;
    len = sizeof(char FAR *) * (MAXALIASES + 1);
    assert(len <= buflen);
    memset(data, 0, len);
    data += len;
    buflen -= len;
    done_len += len;

    for (h = he->h_aliases; *h && (h < he->h_aliases + MAXALIASES); h++, aliases++) {
        len = strlen(*h) + 1;
        assert(len <= buflen);
        memcpy(data, *h, len);
        *aliases = data;
        data += len;
        buflen -= len;
        done_len += len;
    }
    *aliases = NULL;

#define MAXADDRS        8
    aliases = (char FAR **)data;
    dst->h_addr_list = aliases;
    len = sizeof(char FAR *) * (MAXADDRS + 1);
    assert(len <= buflen);
    memset(data, 0, len);
    data += len;
    buflen -= len;
    done_len += len;

    len = he->h_length;
    for (h = he->h_addr_list; *h && (h < he->h_addr_list + MAXADDRS); h++, aliases++) {
        assert(len <= buflen);
        memcpy(data, *h, len);
        *aliases = data;
        data += len;
        buflen -= len;
        done_len += len;
    }
    *aliases = NULL;

    freehostent(he);
    GHBN_RET(ghbn, done_len);
}

HANDLE pascal far WSAAsyncGetHostByName(HWND hWnd, u_int wMsg,
					const char FAR *name,
					char FAR *buf, int buflen)
{
    struct per_task *task = task_find(GetCurrentTask());
    HANDLE id = wsa_id;
    struct per_async *async;
    HWND wnd;

    _ENT();
    if (!name || buflen < MAXGETHOSTSTRUCT) {
        task->wsa_err = WSAEINVAL;
        return 0;
    }

    async = &asyncs[id];
    assert(!async->handler);
    async->handler = AsyncGetHostByName;
    async->cancel = 0;
    async->ghbn.id = id + 1;
    async->ghbn.hWnd = hWnd;
    async->ghbn.wMsg = wMsg;
    async->ghbn.name = name;
    async->ghbn.buf = buf;
    async->ghbn.buflen = buflen;

    wnd = CreateWindow(WSAClassName, __FUNCTION__,
                        WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        NULL, NULL,
                        hinst,
                        NULL);
    if (!wnd) {
        async->handler = NULL;
        task->wsa_err = WSANO_RECOVERY;
        return 0;
    }
    PostMessage(wnd, WM_USER, 0, async);

    wsa_id++;
    wsa_id &= MAX_ASYNC_M1;
    _LVE();
    return id + 1;
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
    struct per_async *async;

    _ENT();
    assert(hAsyncTaskHandle > 0 && hAsyncTaskHandle <= MAX_ASYNC);
    async = &asyncs[hAsyncTaskHandle - 1];
    async->cancel++;
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
    const char desc[] =
		"Open Winsock - winsock-1.1 for OpenWatcom. "
		"Copyright 2025 @stsp. "
		"Open Winsock is free software, GPLv3+.";
    _ENT();
    lpWSAData->wVersion = 0x0101;
    lpWSAData->wHighVersion = 0x0101;
    assert(sizeof(desc) <= 256);
    strcpy(lpWSAData->szDescription, desc);
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
    assert(task);
    task->wsa_err = iError;
}

int pascal far WSAGetLastError(void)
{
    int ret;
    struct per_task *task = task_find(GetCurrentTask());

    _ENT();
    assert(task);
    ret = task->wsa_err;
    task->wsa_err = 0;
    return ret;
}

BOOL pascal far WSAIsBlocking(void)
{
    struct per_task *task = task_find(GetCurrentTask());

    _ENT();
    assert(task);
    return task->blocking;
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
    if (task->blocking)
        task->cancel++;
    return 0;
}
