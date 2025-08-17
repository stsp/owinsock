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

static int idComm;

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

int FAR PASCAL __WSAFDIsSet(SOCKET s, fd_set FAR *pfds)
{
	int i;

	_ENT();
	for (i = 0; i < pfds->fd_count; i++)
	{
		if (pfds->fd_array[i] == s)
			return TRUE;
	}
	return FALSE;
}

BOOL FAR PASCAL LibMain( HINSTANCE hInstance, WORD wDataSegment,
                         WORD wHeapSize, LPSTR lpszCmdLine )
{
	idComm = OpenComm("COM4", 16384, 16384);
	_ENT();
	return 1;
}

#pragma off (unreferenced);
int FAR PASCAL WEP( int nParameter )
#pragma on (unreferenced);
{
	_ENT();
	if (idComm > 0)
		CloseComm(idComm);
	return( 1 );
}
