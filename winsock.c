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
#include <d2sock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

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

struct async_base {
    int aid;
    int (*handler)(struct async_base *arg);
    int cancel;
    int closed;
};

struct per_asel {
    struct async_base base;
    HWND hWnd;
    unsigned int wMsg;
    long lEvent;
    int s;
    int state;
};

struct per_async {
    struct async_base base;
    struct GHBN ghbn;
};
#define MAX_ASYNC 256
static struct per_async asyncs[MAX_ASYNC];
static HANDLE wsa_id;
#define MAX_ASYNC_M1 (MAX_ASYNC - 1)

enum { I_ASYNC, I_ASEL };

static void CancelAS(int s);

#ifdef DEBUG
static int idComm;

static void debug_out(const char *msg)
{
    if (idComm > 0)
	WriteComm(idComm, msg, strlen(msg));
}

#define DEBUG_STR(...) { \
	char _buf[128]; \
	snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
	debug_out(_buf); \
}
#else
#define debug_out(x)
#define DEBUG_STR(...)
#endif

#define _ENT() debug_out("enter: " __FUNCTION__ "\n")
#define _LVE() debug_out("leave: " __FUNCTION__ "\n")

#define _WSAE(x) errno = 0, (x)

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

/*
 * We can't use Yield() as it never switches between window procs of
 * the same task. Needs to use this synchronous event dispatching.
 *
 */
static BOOL DefaultBlockingHook(void)
{
    MSG msg;
    BOOL ret;

    ret = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
    if (ret) {
       TranslateMessage(&msg);
       DispatchMessage(&msg);
    }
    return ret;
}

static int blk_async(struct async_base *async)
{
    /* cancel must be processed synchronously, as the canceler is
     * waiting for a "done" flag. So check before anything else,
     * rather than call back (recursively) to canceler. */
    if (async->cancel || async->closed)
        return 0;
#if 0
    Yield();
#else
    while (DefaultBlockingHook());
#endif
    return 1;
}

static int blk_func(void *arg)
{
    struct per_task *task;

    if (arg)
        return blk_async(arg);

    task = task_find(GetCurrentTask());
    assert(task);
    if (task->blocking) {
        _WSAE(task->wsa_err) = WSAEINPROGRESS;
        return 0;  // avoid recursive blocking
    }
    task->blocking++;
    if (task->BlockingHook)
        while (task->BlockingHook());
    else
        DefaultBlockingHook();
    task->blocking--;
    /* check for WSACancelBlockingCall() */
    if (task->cancel) {
        task->cancel = 0;
        return 0;
    }
    return 1;
}

static int close_func(int s, void *arg)
{
    struct per_asel *asel = arg;

    _ENT();
    assert(asel);
    asel->base.closed++;
    return 0;  // no close
}

/* callback needs to be exported so that Windows can patch its prolog
 * with proper dataseg - same that it passes to LibMain() */
LRESULT CALLBACK _export WSAWindowProc(HWND hWnd, UINT wMsg,
        WPARAM wParam, LPARAM lParam)
{
    _ENT();
    switch (wMsg) {
    case WM_USER:
        switch (wParam) {
            case 0: {
                struct async_base *async = (struct async_base *)lParam;
                int rc;

                DEBUG_STR("\tASYNC event %i\n", async->aid);
                assert(async && async->handler);
                rc = async->handler(async);
                if (rc) {
                    DestroyWindow(hWnd);
                } else {
#define USE_TIMER 1
#if USE_TIMER
                    SetWindowLong(hWnd, 0, lParam);
                    SetTimer(hWnd, 1, 500, NULL);
                    debug_out("setting timer\n");
#else
                    while (DefaultBlockingHook());
                    PostMessage(hWnd, wMsg, wParam, lParam);
#endif
                }
                break;
            }
        }
        break;

    case WM_TIMER:
        DEBUG_STR("fired timer %i\n", wParam);
        KillTimer(hWnd, wParam);
        PostMessage(hWnd, WM_USER, 0, GetWindowLong(hWnd, 0));
        break;

    default:
        DEBUG_STR("\twmsg 0x%x\n", wMsg);
        return DefWindowProc(hWnd, wMsg, wParam, lParam);
    }

    return 0;
}

BOOL FAR PASCAL LibMain(HINSTANCE hInstance, WORD wDataSegment,
			WORD wHeapSize, LPSTR lpszCmdLine)
{
    WNDCLASS wc = {0};

#ifdef DEBUG
    idComm = OpenComm("COM4", 16384, 16384);
#endif
    _ENT();
    DEBUG_STR("hInstance=%x dataseg=%x heapsize=%x cmdline=%s\n",
            hInstance, wDataSegment, wHeapSize, lpszCmdLine);
    d2s_set_blocking_hook(blk_func);
#ifdef DEBUG
    d2s_set_debug_hook(debug_out);
#endif
    d2s_set_close_hook(close_func);
    hinst = hInstance;

    wc.style = 0;
    wc.lpfnWndProc = WSAWindowProc;
    wc.cbWndExtra = sizeof(long);
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
    d2s_set_blocking_hook(NULL);
    d2s_set_debug_hook(NULL);
#ifdef DEBUG
    if (idComm > 0)
	CloseComm(idComm);
#endif
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
    _WSAE(task->wsa_err) = WSAEOPNOTSUPP;
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
    _WSAE(task->wsa_err) = WSAEOPNOTSUPP;
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
    _WSAE(task->wsa_err) = WSAEOPNOTSUPP;
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
    _WSAE(task->wsa_err) = WSAEOPNOTSUPP;
    return 0;
}

#define GHBN_RET(g, l) \
        PostMessage(g->hWnd, g->wMsg, g->id, WSAMAKEASYNCREPLY(l, 0));
#define GHBN_ERR(g, e) \
        PostMessage(g->hWnd, g->wMsg, g->id, WSAMAKEASYNCREPLY(0, e));

static void _AsyncGetHostByName(struct async_base *base)
{
    struct per_async *arg = (struct per_async *)base;
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

static int AsyncGetHostByName(struct async_base *base)
{
    _AsyncGetHostByName(base);
    base->handler = NULL;
    return 1;
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
        _WSAE(task->wsa_err) = WSAEINVAL;
        return 0;
    }

    async = &asyncs[id];
    assert(!async->base.handler);
    memset(async, 0, sizeof(struct per_async));
    async->base.aid = I_ASYNC;
    async->base.handler = AsyncGetHostByName;
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
        async->base.handler = NULL;
        _WSAE(task->wsa_err) = WSANO_RECOVERY;
        return 0;
    }
    PostMessage(wnd, WM_USER, 0, (long)async);

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

/* Note: WSAAsyncGetXByY() (above) return 0 as failure.
 *       Other WSA funcs (below) return 0 as success. */

int pascal far WSACancelAsyncRequest(HANDLE hAsyncTaskHandle)
{
    struct per_async *async;

    _ENT();
    assert(hAsyncTaskHandle > 0 && hAsyncTaskHandle <= MAX_ASYNC);
    async = &asyncs[hAsyncTaskHandle - 1];
    async->base.cancel++;
    return 0;
}

#define _FREAD(lEvent) (!!((lEvent) & FD_READ))
#define _FWRITE(lEvent) (!!((lEvent) & FD_WRITE))
#define _FOOB(lEvent) (!!((lEvent) & FD_OOB))
#define _FACCEPT(lEvent) (!!((lEvent) & FD_ACCEPT))
#define _FCONNECT(lEvent) (!!((lEvent) & FD_CONNECT))
#define _FCLOSE(lEvent) (!!((lEvent) & FD_CLOSE))

static int AsyncSelect(struct async_base *base)
{
    struct per_asel *arg = (struct per_asel *)base;
    int fread = _FREAD(arg->lEvent);
    int fwrite = _FWRITE(arg->lEvent);
    int foob = _FOOB(arg->lEvent);
    int faccept = _FACCEPT(arg->lEvent);
    int fconnect = _FCONNECT(arg->lEvent);
    int fclose = _FCLOSE(arg->lEvent);
    int err;

    _ENT();

    DEBUG_STR("\tfd:%i event:0x%lx (fread:%i fwrite:%i foob:%i faccept:%i fconnect:%i fclose:%i)\n",
            arg->s, arg->lEvent, fread, fwrite, foob, faccept, fconnect, fclose);
    DEBUG_STR("\tcancel:%i closed:%i\n", base->cancel, base->closed);
    if (!base->cancel && !base->closed) {
        if (arg->state = 0) {
            arg->state++;
            /* do initialization here */
            d2s_set_blocking_arg(arg->s, base);
            d2s_set_close_arg(arg->s, arg);
        }

        if (fconnect) {
            err = aconnect(arg->s);
            if (err) {
                switch (errno) {
                    case EAGAIN:
                        debug_out("\tkeeps waiting\n");
                        return 0;
                    case EIO:
                        PostMessage(arg->hWnd, arg->wMsg, arg->s,
                                WSAMAKESELECTREPLY(FD_CONNECT, WSAECONNREFUSED));
                        arg->lEvent &= ~FD_CONNECT;
                        debug_out("\tconnect failed\n");
                        return 0;
                    /* other errors: ignore fconnect */
                }
            } else {
                PostMessage(arg->hWnd, arg->wMsg, arg->s,
                        WSAMAKESELECTREPLY(FD_CONNECT, 0));
                arg->lEvent &= ~FD_CONNECT;
                debug_out("\tconnected\n");
                return 0;
            }
        }

        if (fread || fwrite || foob) {
            fd_set r, w, b;
            struct timeval tv = {0};
            int res;

            FD_ZERO(&r);
            FD_ZERO(&w);
            FD_ZERO(&b);
            if (fread)
                FD_SET(arg->s, &r);
            if (fwrite)
                FD_SET(arg->s, &w);
            if (foob)
                FD_SET(arg->s, &b);
            res = select(arg->s + 1,
                         fread ? &r : NULL,
                         fwrite ? &w : NULL,
                         foob ? &b : NULL,
                         &tv);
            if (res <= 0)
                return 0;
            if (FD_ISSET(arg->s, &r)) {
                PostMessage(arg->hWnd, arg->wMsg, arg->s,
                        WSAMAKESELECTREPLY(FD_READ, 0));
                arg->lEvent &= ~FD_READ;
                debug_out("\tread\n");
            }
            if (FD_ISSET(arg->s, &w)) {
                PostMessage(arg->hWnd, arg->wMsg, arg->s,
                        WSAMAKESELECTREPLY(FD_WRITE, 0));
                arg->lEvent &= ~FD_WRITE;
                debug_out("\twrite\n");
            }
            if (FD_ISSET(arg->s, &b)) {
                PostMessage(arg->hWnd, arg->wMsg, arg->s,
                        WSAMAKESELECTREPLY(FD_OOB, 0));
                arg->lEvent &= ~FD_OOB;
                debug_out("\toob\n");
            }
        }

        if (arg->lEvent)
            return 0;
    }

    if (fclose && base->closed && !base->cancel) {
        PostMessage(arg->hWnd, arg->wMsg, arg->s,
                WSAMAKESELECTREPLY(FD_CLOSE, 0));
        arg->lEvent &= ~FD_CLOSE;
        debug_out("\tclosed\n");
    }

    /* on cancel the arg already re-used, so then don't touch */
    if (!base->cancel) {
        assert(arg == d2s_get_close_arg(arg->s));
        d2s_set_close_arg(arg->s, NULL);
    } else {
        assert(arg != d2s_get_close_arg(arg->s));
    }
    if (base->closed)
        closesocket(arg->s);
    free(arg);
    DEBUG_STR("async select finished, fd=%i\n", arg->s);
    return 1;
}

static void CancelAS(int s)
{
    struct per_asel *asel = d2s_get_close_arg(s);

    _ENT();
    if (!asel)
        return;
    d2s_set_close_arg(s, NULL);
    asel->base.cancel++;
}

int pascal far WSAAsyncSelect(SOCKET s, HWND hWnd, u_int wMsg, long lEvent)
{
    struct per_task *task = task_find(GetCurrentTask());
    int fread = _FREAD(lEvent);
    int fwrite = _FWRITE(lEvent);
    int foob = _FOOB(lEvent);
    int faccept = _FACCEPT(lEvent);
    int fconnect = _FCONNECT(lEvent);
    int fclose = _FCLOSE(lEvent);
    struct per_asel *asel;
    HWND wnd;

    _ENT();
    DEBUG_STR("\tfd:%i event:0x%lx (fread:%i fwrite:%i foob:%i faccept:%i fconnect:%i fclose:%i)\n",
            s, lEvent, fread, fwrite, foob, faccept, fconnect, fclose);
    CancelAS(s);
    if (!lEvent)
        return 0;

    wnd = CreateWindow(WSAClassName, __FUNCTION__,
                        WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        NULL, NULL,
                        hinst,
                        NULL);
    if (!wnd) {
        _WSAE(task->wsa_err) = WSANO_RECOVERY;
        return SOCKET_ERROR;
    }

    asel = malloc(sizeof(struct per_asel));
    memset(asel, 0, sizeof(struct per_asel));
    asel->base.aid = I_ASEL;
    asel->base.handler = AsyncSelect;
    asel->hWnd = hWnd;
    asel->wMsg = wMsg;
    asel->lEvent = lEvent;
    asel->s = s;
    d2s_set_close_arg(s, asel);
    PostMessage(wnd, WM_USER, 0, (long)asel);
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
    _WSAE(task->wsa_err) = iError;
}

static int from_errno(int e)
{
    switch (e) {
        case EAGAIN:
            return WSAEWOULDBLOCK;
        case EINVAL:
            return WSAENOTCONN;  // oops
    }
    DEBUG_STR("\tunsupported errno %i\n", e);
    return 0;
}

int pascal far WSAGetLastError(void)
{
    int ret;
    struct per_task *task = task_find(GetCurrentTask());

    _ENT();
    assert(task);
    if (errno)
        ret = from_errno(errno);
    else
        ret = task->wsa_err;
    _WSAE(task->wsa_err) = 0;
    DEBUG_STR("\treturning %i\n", ret);
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
