/*
 * COPYRIGHT:            See COPYING in the top level directory
 * PROJECT:              ReactOS kernel
 * FILE:                 lib/opengl32/wgl.c
 * PURPOSE:              OpenGL32 lib, wglXXX functions
 * PROGRAMMER:           Anich Gregor (blight)
 * UPDATE HISTORY:
 *                       Feb 2, 2004: Created
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winreg.h>

#include "opengl32.h"


/* FUNCTION: Append OpenGL Rendering Context (GLRC) to list
 * ARGUMENTS: [IN] glrc: GLRC to append to list
 * TODO: protect from race conditions
 */
static void WGL_AppendContext( GLRC *glrc )
{
	if (OPENGL32_processdata.glrc_list == NULL)
		OPENGL32_processdata.glrc_list = glrc;
	else
	{
		GLRC *p = OPENGL32_processdata.glrc_list;
		while (p->next != NULL)
			p = p->next;
		p->next = glrc;
	}
}


/* FUNCTION: Remove OpenGL Rendering Context (GLRC) from list
 * ARGUMENTS: [IN] glrc: GLRC to remove from list
 * TODO: protect from race conditions
 */
static void WGL_RemoveContext( GLRC *glrc )
{
	if (glrc == OPENGL32_processdata.glrc_list)
		OPENGL32_processdata.glrc_list = glrc->next;
	else
	{
		GLRC *p = OPENGL32_processdata.glrc_list;
		while (p != NULL)
		{
			if (p->next == glrc)
			{
				p->next = glrc->next;
				return;
			}
			p = p->next;
		}
		DBGPRINT( "Error: GLRC 0x%08x not found in list!", glrc );
	}
}

/* FUNCTION: Check wether a GLRC is in the list
 * ARGUMENTS: [IN] glrc: GLRC to remove from list
 */
static BOOL WGL_ContainsContext( GLRC *glrc )
{
	GLRC *p = OPENGL32_processdata.glrc_list;

	while (p != NULL)
	{
		if (p == glrc)
			return TRUE;
		p = p->next;
	}

	return FALSE;
}




/* FUNCTION: Copy data specified by mask from one GLRC to another.
 * ARGUMENTS: [IN]  src  Source GLRC
 *            [OUT] dst  Destination GLRC
 *            [IN]  mask Bitfield like given to glPushAttrib()
 * RETURN: TRUE on success, FALSE on failure
 */
BOOL WINAPI wglCopyContext( HGLRC hsrc, HGLRC hdst, UINT mask )
{
	GLRC *src = (GLRC *)hsrc;
	GLRC *dst = (GLRC *)hdst;

	/* check glrcs */
	if (!WGL_ContainsContext( src ))
	{
		DBGPRINT( "Error: src GLRC not found!" );
		return FALSE; /* FIXME: SetLastError() */
	}
	if (!WGL_ContainsContext( dst ))
	{
		DBGPRINT( "Error: dst GLRC not found!" );
		return FALSE; /* FIXME: SetLastError() */
	}

	/* I think this is only possible within one ICD */
	if (src->icd != src->icd)
	{
		DBGPRINT( "Error: src and dst GLRC use different ICDs!" );
		return FALSE;
	}

	/* copy data (call ICD) */
	return src->icd->DrvCopyContext( src->hglrc, dst->hglrc, mask );
}


/* FUNCTION: Create a new GL Rendering Context for the given DC.
 * ARGUMENTS: [IN] hdc  Handle for DC for which to create context
 * RETURNS: NULL on failure, new GLRC on success
 */
HGLRC WINAPI wglCreateContext( HDC hdc )
{
	HKEY hKey;
	WCHAR subKey[1024] = L"SOFTWARE\\Microsoft\\Windows NT\\"
	                      "CurrentVersion\\OpenGLDrivers";
	LONG ret;
	WCHAR driver[256];
	DWORD size;
	DWORD dw;
	FILETIME time;

	GLDRIVERDATA *icd;
	GLRC *glrc;
	HGLRC drvHglrc = NULL;

	if (GetObjectType( hdc ) != OBJ_DC)
	{
		DBGPRINT( "Error: hdc is not a DC handle!" );
		return NULL;
	}

	/* open "OpenGLDrivers" key */
	ret = RegOpenKeyExW( HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey );
	if (ret != ERROR_SUCCESS)
	{
		DBGPRINT( "Error: Couldn't open registry key '%ws'", subKey );
		return NULL;
	}

	/* allocate our GLRC */
	glrc = (GLRC*)HeapAlloc( GetProcessHeap(),
	               HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS, sizeof (GLRC) );
	if (!glrc)
		return NULL;

	/* try to find an ICD */
	for (dw = 0; ; dw++)
	{
		size = 256;
		ret = RegEnumKeyExW( hKey, dw, driver, &size, NULL, NULL, NULL, &time );
		if (ret != ERROR_SUCCESS )
			break;

		icd = OPENGL32_LoadICD( driver );
		if (icd == NULL) /* try next ICD */
			continue;

		drvHglrc = icd->DrvCreateContext( hdc );
		if (drvHglrc == NULL) /* try next ICD */
		{
			DBGPRINT( "Info: DrvCreateContext (driver = %ws) failed: %d",
			          icd->driver_name, GetLastError() );
			OPENGL32_UnloadICD( icd );
			continue;
		}

		/* the ICD was loaded successfully and we got a HGLRC in drvHglrc */
		break;
	}
	RegCloseKey( hKey );

	if (drvHglrc == NULL) /* no ICD was found */
	{
		/* FIXME: fallback to mesa */
		DBGPRINT( "Error: No ICD found!" );
		HeapFree( GetProcessHeap(), 0, glrc );
		return NULL;
	}

	/* we have our GLRC in glrc and the ICD's GLRC in drvHglrc */
	glrc->hglrc = drvHglrc;
	glrc->iFormat = -1; /* what is this used for? */
	glrc->icd = icd;
	memcpy( glrc->func_list, icd->func_list, sizeof (PVOID) * GLIDX_COUNT );

	/* FIXME: fill NULL-pointers in glrc->func_list with mesa functions */

	/* append glrc to context list */
	WGL_AppendContext( glrc );

	return (HGLRC)glrc;

	/* FIXME: dunno if this is right, would be nice :) */
	/*return wglCreateLayerContext( hdc, 0 );*/
}


/* FUNCTION: Create a new GL Rendering Context for the given plane on
 *           the given DC.
 * ARGUMENTS: [IN] hdc   Handle for DC for which to create context
 *            [IN] layer Layer number to bind (draw?) to
 * RETURNS: NULL on failure, new GLRC on success
 */
HGLRC WINAPI wglCreateLayerContext( HDC hdc, int layer )
{
	HKEY hKey;
	WCHAR subKey[1024] = L"SOFTWARE\\Microsoft\\Windows NT\\"
	                      "CurrentVersion\\OpenGLDrivers";
	LONG ret;
	WCHAR driver[256];
	DWORD size;
	DWORD dw;
	FILETIME time;

	GLDRIVERDATA *icd;
	GLRC *glrc;
	HGLRC drvHglrc = NULL;

	if (GetObjectType( hdc ) != OBJ_DC)
	{
		DBGPRINT( "Error: hdc is not a DC handle!" );
		return NULL;
	}

	/* open "OpenGLDrivers" key */
	ret = RegOpenKeyExW( HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey );
	if (ret != ERROR_SUCCESS)
	{
		DBGPRINT( "Error: Couldn't open registry key '%ws'", subKey );
		return NULL;
	}

	/* allocate our GLRC */
	glrc = (GLRC*)HeapAlloc( GetProcessHeap(),
	               HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS, sizeof (GLRC) );
	if (!glrc)
		return NULL;

	/* try to find an ICD */
	for (dw = 0; ; dw++)
	{
		size = 256;
		ret = RegEnumKeyExW( hKey, dw, driver, &size, NULL, NULL, NULL, &time );
		if (ret != ERROR_SUCCESS )
			break;

		icd = OPENGL32_LoadICD( driver );
		if (icd == NULL) /* try next ICD */
			continue;

		drvHglrc = icd->DrvCreateLayerContext( hdc, layer );
		if (drvHglrc == NULL) /* try next ICD */
		{
			DBGPRINT( "Info: DrvCreateLayerContext (driver = %ws) failed: %d",
			          icd->driver_name, GetLastError() );
			OPENGL32_UnloadICD( icd );
			continue;
		}

		/* the ICD was loaded successfully and we got a HGLRC in drvHglrc */
		break;
	}
	RegCloseKey( hKey );

	if (drvHglrc == NULL) /* no ICD was found */
	{
		/* FIXME: fallback to mesa */
		DBGPRINT( "Error: No ICD found!" );
		HeapFree( GetProcessHeap(), 0, glrc );
		return NULL;
	}

	/* we have our GLRC in glrc and the ICD's GLRC in drvHglrc */
	glrc->hglrc = drvHglrc;
	glrc->iFormat = -1; /* what is this used for? */
	glrc->icd = icd;
	memcpy( glrc->func_list, icd->func_list, sizeof (PVOID) * GLIDX_COUNT );

	/* FIXME: fill NULL-pointers in glrc->func_list with mesa functions */

	/* append glrc to context list */
	WGL_AppendContext( glrc );

	return (HGLRC)glrc;
}


/* FUNCTION: Delete an OpenGL context
 * ARGUMENTS: [IN] hglrc  Handle to GLRC to delete; must not be a threads RC!
 * RETURNS: TRUE on success, FALSE otherwise
 */
BOOL WINAPI wglDeleteContext( HGLRC hglrc )
{
	GLRC *glrc = (GLRC *)hglrc;

	/* check if we know about this context */
	if (!WGL_ContainsContext( glrc ))
	{
		DBGPRINT( "Error: hglrc not found!" );
		return FALSE; /* FIXME: SetLastError() */
	}

	/* make sure GLRC is not current for some thread */
	if (glrc->is_current)
	{
		DBGPRINT( "Error: GLRC is current for DC 0x%08x", glrc->hdc );
		return FALSE; /* FIXME: SetLastError() */
	}

	/* release ICD's context */
	if (glrc->hglrc != NULL)
	{
		if (!glrc->icd->DrvDeleteContext( glrc->hglrc ))
		{
			DBGPRINT( "Warning: DrvDeleteContext() failed (%d)", GetLastError() );
			return FALSE;
		}
	}

	/* free resources */
	WGL_RemoveContext( glrc );
	HeapFree( GetProcessHeap(), 0, glrc );

	return TRUE;
}


BOOL WINAPI wglDescribeLayerPlane( HDC hdc, int iPixelFormat, int iLayerPlane,
                                   UINT nBytes, LPLAYERPLANEDESCRIPTOR plpd )
{


	return FALSE;
}


/* FUNCTION: Return the current GLRC
 * RETURNS: Current GLRC (NULL if none was set current)
 */
HGLRC WINAPI wglGetCurrentContext()
{
	return (HGLRC)(OPENGL32_threaddata->glrc);
}


/* FUNCTION: Return the current DC
 * RETURNS: NULL on failure, current DC otherwise
 */
HDC WINAPI wglGetCurrentDC()
{
	/* FIXME: is it correct to return NULL when there is no current GLRC or
	   is there another way to find out the wanted HDC? */
	if (OPENGL32_threaddata->glrc == NULL)
		return NULL;
	return (HDC)(OPENGL32_threaddata->glrc->hdc);
}


int WINAPI wglGetLayerPaletteEntries( HDC hdc, int iLayerPlane, int iStart,
                               int cEntries, COLORREF *pcr )
{
	return 0;
}


/* FUNCTION: Get the address for an OpenGL extension function from the current ICD.
 * ARGUMENTS: [IN] proc:  Name of the function to look for
 * RETURNS: The address of the proc or NULL on failure.
 */
PROC WINAPI wglGetProcAddress( LPCSTR proc )
{
	if (OPENGL32_threaddata->glrc == NULL)
	{
		DBGPRINT( "Error: No current GLRC!" );
		return NULL;
	}

	if (proc[0] == 'g' && proc[1] == 'l') /* glXXX */
	{
		PROC glXXX = OPENGL32_threaddata->glrc->icd->DrvGetProcAddress( proc );
		if (glXXX)
		{
			DBGPRINT( "Info: Proc \"%s\" loaded from ICD.", proc );
			return glXXX;
		}

		/* FIXME: go through own functions? */
		DBGPRINT( "Unsupported GL extension: %s", proc );
	}
	else if (proc[0] == 'w' && proc[1] == 'g' && proc[2] == 'l') /* wglXXX */
	{
		/* FIXME: support wgl extensions? (there are such IIRC) */
		DBGPRINT( "Unsupported WGL extension: %s", proc );
	}
	else if (proc[0] == 'g' && proc[1] == 'l' && proc[2] == 'u') /* gluXXX */
	{
		DBGPRINT( "GLU extension %s requested, returning NULL", proc );
	}

	return NULL;
}


/* FUNCTION: make the given GLRC the threads current GLRC for hdc
 * ARGUMENTS: [IN] hdc   Handle for a DC to be drawn on
 *            [IN] hglrc Handle for a GLRC to make current
 * RETURNS: TRUE on success, FALSE otherwise
 */
BOOL WINAPI wglMakeCurrent( HDC hdc, HGLRC hglrc )
{
	GLRC *glrc = (GLRC *)hglrc;

	/* check hdc */
	if (GetObjectType( hdc ) != OBJ_DC)
	{
		DBGPRINT( "Error: hdc is not a DC handle!" );
		return FALSE;
	}

	/* check if we know about this glrc */
	if (!WGL_ContainsContext( glrc ))
	{
		DBGPRINT( "Error: hglrc not found!" );
		return FALSE; /* FIXME: SetLastError() */
	}

	/* check if it is available */
	if (glrc->is_current) /* used by another thread */
	{
		DBGPRINT( "Error: hglrc is current for thread 0x%08x", glrc->thread_id );
		return FALSE; /* FIXME: SetLastError() */
	}

	/* call the ICD */
	if (glrc->hglrc != NULL)
	{
		/* FIXME: which function to call? DrvSetContext?
		          does it crash with NULL as SetContextCallBack? */
		if (!glrc->icd->DrvSetContext( hdc, glrc->hglrc, NULL ))
		{
			DBGPRINT( "Error: DrvSetContext failed (%d)\n", GetLastError() );
			return FALSE;
		}
	}

	/* make it current */
	if (OPENGL32_threaddata->glrc != NULL)
		OPENGL32_threaddata->glrc->is_current = FALSE;
	glrc->is_current = TRUE;
	glrc->thread_id = GetCurrentThreadId();
	OPENGL32_threaddata->glrc = glrc;

	return TRUE;
}


BOOL WINAPI wglRealizeLayerPalette( HDC hdc, int iLayerPlane, BOOL bRealize )
{
	return FALSE;
}


int WINAPI wglSetLayerPaletteEntries( HDC hdc, int iLayerPlane, int iStart,
                               int cEntries, CONST COLORREF *pcr )
{
	return 0;
}

/* FUNCTION: Enable display-list sharing between multiple GLRCs
 * ARGUMENTS: [IN] hglrc1 GLRC number 1
 *            [IN] hglrc2 GLRC number 2
 * RETURNS: TRUR on success, FALSE on failure
 */
BOOL WINAPI wglShareLists( HGLRC hglrc1, HGLRC hglrc2 )
{
	GLRC *glrc1 = (GLRC *)hglrc1;
	GLRC *glrc2 = (GLRC *)hglrc2;

	/* check glrcs */
	if (!WGL_ContainsContext( glrc1 ))
	{
		DBGPRINT( "Error: hglrc1 not found!" );
		return FALSE; /* FIXME: SetLastError() */
	}
	if (!WGL_ContainsContext( glrc2 ))
	{
		DBGPRINT( "Error: hglrc2 not found!" );
		return FALSE; /* FIXME: SetLastError() */
	}

	/* I think this is only possible within one ICD */
	if (glrc1->icd != glrc2->icd)
	{
		DBGPRINT( "Error: hglrc1 and hglrc2 use different ICDs!" );
		return FALSE;
	}

	/* share lists (call ICD) */
	return glrc1->icd->DrvShareLists( glrc1->hglrc, glrc2->hglrc );
}

/* FUNCTION: Flushes GL and swaps front/back buffer if appropriate
 * ARGUMENTS: [IN] hdc  Handle to device context to swap buffers for
 * RETURNS: TRUE on success, FALSE on failure
 */
BOOL WINAPI wglSwapBuffers( HDC hdc )
{
	/* check if there is a current GLRC */
	if (OPENGL32_threaddata->glrc == NULL)
	{
		DBGPRINT( "Error: No current GL context!" );
		return FALSE;
	}

	/* ask ICD to swap buffers */
	/* FIXME: also ask ICD when we didnt use it to create the context/it couldnt? */
	if (OPENGL32_threaddata->glrc->hglrc != NULL)
	{
		if (!OPENGL32_threaddata->glrc->icd->DrvSwapBuffers( hdc ))
		{
			DBGPRINT( "Error: DrvSwapBuffers failed (%d)", GetLastError() );
			return FALSE;
		}
		return TRUE;
	}

	/* FIXME: implement own functionality */

	return FALSE;
}


BOOL WINAPI wglSwapLayerBuffers( HDC hdc, UINT fuPlanes )
{
	return FALSE;
}


BOOL WINAPI wglUseFontBitmapsA( HDC hdc, DWORD  first, DWORD count, DWORD listBase )
{
	return FALSE;
}


BOOL WINAPI wglUseFontBitmapsW( HDC hdc, DWORD  first, DWORD count, DWORD listBase )
{
	return FALSE;
}


BOOL WINAPI wglUseFontOutlinesA( HDC hdc, DWORD first, DWORD count, DWORD listBase,
                          FLOAT deviation, FLOAT extrusion, int  format,
                          LPGLYPHMETRICSFLOAT  lpgmf )
{
	return FALSE;
}


BOOL WINAPI wglUseFontOutlinesW( HDC hdc, DWORD first, DWORD count, DWORD listBase,
                          FLOAT deviation, FLOAT extrusion, int  format,
                          LPGLYPHMETRICSFLOAT  lpgmf )
{
	return FALSE;
}

/* EOF */
