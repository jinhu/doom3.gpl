/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "sys/platform.h"

#include "sys/win32/win_local.h"

#include <lmerr.h>
#include <lmcons.h>
#include <lmwksta.h>
#include <errno.h>
#include <fcntl.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

#if !defined(ID_DEDICATED) && !defined(__MINGW32__)
#include <comdef.h>
#include <comutil.h>
#include <wbemidl.h>

#pragma comment (lib, "wbemuuid.lib")
#endif

/*
================
Sys_GetSystemRam

	returns amount of physical memory in MB
================
*/
int Sys_GetSystemRam( void ) {
	MEMORYSTATUSEX statex;
	statex.dwLength = sizeof ( statex );
	GlobalMemoryStatusEx (&statex);
	int physRam = statex.ullTotalPhys / ( 1024 * 1024 );
	// HACK: For some reason, ullTotalPhys is sometimes off by a meg or two, so we round up to the nearest 16 megs
	physRam = ( physRam + 8 ) & ~15;
	return physRam;
}


/*
================
Sys_GetDriveFreeSpace
returns in megabytes
================
*/
int Sys_GetDriveFreeSpace( const char *path ) {
	DWORDLONG lpFreeBytesAvailable;
	DWORDLONG lpTotalNumberOfBytes;
	DWORDLONG lpTotalNumberOfFreeBytes;
	int ret = 26;
	//FIXME: see why this is failing on some machines
	if ( ::GetDiskFreeSpaceEx( path, (PULARGE_INTEGER)&lpFreeBytesAvailable, (PULARGE_INTEGER)&lpTotalNumberOfBytes, (PULARGE_INTEGER)&lpTotalNumberOfFreeBytes ) ) {
		ret = ( double )( lpFreeBytesAvailable ) / ( 1024.0 * 1024.0 );
	}
	return ret;
}


/*
================
Sys_GetVideoRam
returns in megabytes
================
*/
int Sys_GetVideoRam( void ) {
#if defined(ID_DEDICATED)
	return 0;
#elif !defined(_MFC_VER)
	// no <atlbase.h> (MinGW, VS Express etc.), so assume the min. req. 64Mb
	return 64;
#else
	unsigned int retSize = 64;

	CComPtr<IWbemLocator> spLoc = NULL;
	HRESULT hr = CoCreateInstance( CLSID_WbemLocator, 0, CLSCTX_SERVER, IID_IWbemLocator, ( LPVOID * ) &spLoc );
	if ( hr != S_OK || spLoc == NULL ) {
		return retSize;
	}

	CComBSTR bstrNamespace( _T( "\\\\.\\root\\CIMV2" ) );
	CComPtr<IWbemServices> spServices;

	// Connect to CIM
	hr = spLoc->ConnectServer( bstrNamespace, NULL, NULL, 0, NULL, 0, 0, &spServices );
	if ( hr != WBEM_S_NO_ERROR ) {
		return retSize;
	}

	// Switch the security level to IMPERSONATE so that provider will grant access to system-level objects.
	hr = CoSetProxyBlanket( spServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE );
	if ( hr != S_OK ) {
		return retSize;
	}

	// Get the vid controller
	CComPtr<IEnumWbemClassObject> spEnumInst = NULL;
	hr = spServices->CreateInstanceEnum( CComBSTR( "Win32_VideoController" ), WBEM_FLAG_SHALLOW, NULL, &spEnumInst );
	if ( hr != WBEM_S_NO_ERROR || spEnumInst == NULL ) {
		return retSize;
	}

	ULONG uNumOfInstances = 0;
	CComPtr<IWbemClassObject> spInstance = NULL;
	hr = spEnumInst->Next( 10000, 1, &spInstance, &uNumOfInstances );

	if ( hr == S_OK && spInstance ) {
		// Get properties from the object
		CComVariant varSize;
		hr = spInstance->Get( CComBSTR( _T( "AdapterRAM" ) ), 0, &varSize, 0, 0 );
		if ( hr == S_OK ) {
			retSize = varSize.intVal / ( 1024 * 1024 );
			if ( retSize == 0 ) {
				retSize = 64;
			}
		}
	}
	return retSize;
#endif
}

/*
================
Sys_GetCurrentMemoryStatus

	returns OS mem info
	all values are in kB except the memoryload
================
*/
void Sys_GetCurrentMemoryStatus( sysMemoryStats_t &stats ) {
	MEMORYSTATUSEX statex;
	unsigned __int64 work;

	memset( &statex, 0, sizeof( statex ) );
	statex.dwLength = sizeof( statex );
	GlobalMemoryStatusEx( &statex );

	memset( &stats, 0, sizeof( stats ) );

	stats.memoryLoad = statex.dwMemoryLoad;

	work = statex.ullTotalPhys >> 20;
	stats.totalPhysical = *(int*)&work;

	work = statex.ullAvailPhys >> 20;
	stats.availPhysical = *(int*)&work;

	work = statex.ullAvailPageFile >> 20;
	stats.availPageFile = *(int*)&work;

	work = statex.ullTotalPageFile >> 20;
	stats.totalPageFile = *(int*)&work;

	work = statex.ullTotalVirtual >> 20;
	stats.totalVirtual = *(int*)&work;

	work = statex.ullAvailVirtual >> 20;
	stats.availVirtual = *(int*)&work;

	work = statex.ullAvailExtendedVirtual >> 20;
	stats.availExtendedVirtual = *(int*)&work;
}

/*
================
Sys_LockMemory
================
*/
bool Sys_LockMemory( void *ptr, int bytes ) {
	return ( VirtualLock( ptr, (SIZE_T)bytes ) != FALSE );
}

/*
================
Sys_UnlockMemory
================
*/
bool Sys_UnlockMemory( void *ptr, int bytes ) {
	return ( VirtualUnlock( ptr, (SIZE_T)bytes ) != FALSE );
}

/*
================
Sys_SetPhysicalWorkMemory
================
*/
void Sys_SetPhysicalWorkMemory( int minBytes, int maxBytes ) {
	::SetProcessWorkingSetSize( GetCurrentProcess(), minBytes, maxBytes );
}

/*
================
Sys_GetCurrentUser
================
*/
char *Sys_GetCurrentUser( void ) {
	static char s_userName[1024];
	unsigned long size = sizeof( s_userName );


	if ( !GetUserName( s_userName, &size ) ) {
		strcpy( s_userName, "player" );
	}

	if ( !s_userName[0] ) {
		strcpy( s_userName, "player" );
	}

	return s_userName;
}
