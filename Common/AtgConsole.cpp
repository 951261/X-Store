//--------------------------------------------------------------------------------------
// AtgConsole.cpp
//
// Console class for simple applications that need no input and only console output.
// Use this INSTEAD of AtgApplication.
//
// Requires .TGA/.ABC files for the console font. 
//
// Xbox Advanced Technology Group.
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "stdafx.h"
#include <malloc.h>
#include "AtgConsole.h"
#include "AtgFont.h"
#include "AtgUtil.h"

// Ignore warning about "unused" pD3D variable
#pragma warning( disable: 4189 )

namespace ATG
{

//--------------------------------------------------------------------------------------
// Declarations
//--------------------------------------------------------------------------------------
// Global access to the main D3D device
extern D3DDevice* g_pd3dDevice;


//--------------------------------------------------------------------------------------
// Static variables
//--------------------------------------------------------------------------------------

// Private access to the main D3D device
D3DDevice* Console::m_pd3dDevice = NULL;


//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------

// The device-creation presentation params with reasonable defaults
D3DPRESENT_PARAMETERS Console::m_d3dpp =
{
    640,                // BackBufferWidth;
    480,                // BackBufferHeight;
    D3DFMT_A8R8G8B8,    // BackBufferFormat;
    1,                  // BackBufferCount;
    D3DMULTISAMPLE_NONE,// MultiSampleType;
    0,                  // MultiSampleQuality;
    D3DSWAPEFFECT_DISCARD, // SwapEffect;
    NULL,               // hDeviceWindow;
    FALSE,              // Windowed;
    TRUE,               // EnableAutoDepthStencil;
    D3DFMT_D24S8,       // AutoDepthStencilFormat;
    0,                  // Flags;
    0,                  // FullScreen_RefreshRateInHz;
    D3DPRESENT_INTERVAL_IMMEDIATE, // FullScreen_PresentationInterval;
};


//--------------------------------------------------------------------------------------
// Name: Console()
// Desc: Initialize variables
//--------------------------------------------------------------------------------------
Console::Console()
{
    m_Buffer = NULL;
    m_Lines = NULL;
    m_LineColors = NULL;
    m_bExpectColorCode = FALSE;
    m_bOutputToDebugChannel = FALSE;
}


//--------------------------------------------------------------------------------------
// Name: ~Console()
// Desc: Destroy object
//--------------------------------------------------------------------------------------
Console::~Console()
{
    Destroy();
}


//--------------------------------------------------------------------------------------
// Name: Create()
// Desc: Initialize the class, create D3D and the font texture
//--------------------------------------------------------------------------------------
HRESULT Console::Create( LPCSTR strFontFileName, D3DCOLOR colBackColor,
                         D3DCOLOR colTextColor, UINT nLines )
{
    // Create Direct3D
    LPDIRECT3D9 pD3D = Direct3DCreate9( D3D_SDK_VERSION );

    // Check video mode settings
    XVIDEO_MODE VideoMode;
    ZeroMemory( &VideoMode, sizeof( VideoMode ) );
    XGetVideoMode( &VideoMode );

    BOOL bEnable720p = VideoMode.dwDisplayWidth >= 1280;

    m_d3dpp.BackBufferWidth = bEnable720p ? SCREEN_SIZE_X_720p : SCREEN_SIZE_X_DEFAULT;
    m_d3dpp.BackBufferHeight = bEnable720p ? SCREEN_SIZE_Y_720p : SCREEN_SIZE_Y_DEFAULT;

    // Calculate the safe area
    UINT uiSafeAreaPct = bEnable720p ? SAFE_AREA_PCT_HDTV : SAFE_AREA_PCT_4x3;

    m_cxSafeArea = ( m_d3dpp.BackBufferWidth * uiSafeAreaPct ) / 100;
    m_cySafeArea = ( m_d3dpp.BackBufferHeight * uiSafeAreaPct ) / 100;

    m_cxSafeAreaOffset = ( m_d3dpp.BackBufferWidth - m_cxSafeArea ) / 2;
    m_cySafeAreaOffset = ( m_d3dpp.BackBufferHeight - m_cySafeArea ) / 2;

    // Create the D3D device
    HRESULT hr = pD3D->CreateDevice( 0, D3DDEVTYPE_HAL, NULL,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                     &m_d3dpp, ( ::D3DDevice** )&m_pd3dDevice );
    if( FAILED( hr ) )
    {
        ATG_PrintError( "Could not create D3D device!\n" );
        DebugBreak();
    }

    // Allow global access to the device
    g_pd3dDevice = m_pd3dDevice;

    // Create the font
    hr = m_Font.Create( strFontFileName );
    if( FAILED( hr ) )
    {
        ATG_PrintError( "Could not create font.\n" );
        DebugBreak();
    }

    // Save the colors
    m_colBackColor = colBackColor;
    m_colTextColor = colTextColor;

    // Calculate the number of lines on the screen
    FLOAT fCharWidth, fCharHeight;
    m_Font.GetTextExtent( L"i", &fCharWidth, &fCharHeight, FALSE );

    m_cScreenHeight = ( UINT )( m_cySafeArea / fCharHeight );
    m_cScreenWidth = ( UINT )( m_cxSafeArea / fCharWidth );

    m_cScreenHeightVirtual = max( m_cScreenHeight, nLines );

    m_fLineHeight = fCharHeight;

    // Allocate memory to hold the lines
    m_Buffer = new WCHAR[ m_cScreenHeightVirtual * ( m_cScreenWidth + 1 ) ];
    m_Lines = new WCHAR*[ m_cScreenHeightVirtual ];
    m_LineColors = new D3DCOLOR[ m_cScreenHeightVirtual ];

    // Set the line pointers as indexes into the buffer
    for( UINT i = 0; i < m_cScreenHeightVirtual; i++ )
    {
        m_Lines[ i ] = m_Buffer + ( m_cScreenWidth + 1 ) * i;
    }

    m_bSuspendFlag = FALSE;
    m_bExpectColorCode = FALSE;

    // Clear the screen
    Clear();

    return hr;
}


//--------------------------------------------------------------------------------------
// Name: Clear()
// Desc: Clear the screen
//--------------------------------------------------------------------------------------
VOID Console::Clear()
{
    m_nCurLine = 0;
    m_cCurLineLength = 0;
    ZeroMemory( m_Buffer, m_cScreenHeightVirtual * ( m_cScreenWidth + 1 ) * sizeof( WCHAR ) );
    if( m_LineColors )
    {
        for( UINT i = 0; i < m_cScreenHeightVirtual; i++ )
            m_LineColors[ i ] = m_colTextColor;
    }
    m_bExpectColorCode = FALSE;

    Render();
}

VOID Console::ClearNoRender()
{
    m_nCurLine = 0;
    m_cCurLineLength = 0;
    ZeroMemory( m_Buffer, m_cScreenHeightVirtual * ( m_cScreenWidth + 1 ) * sizeof( WCHAR ) );
    if( m_LineColors )
    {
        for( UINT i = 0; i < m_cScreenHeightVirtual; i++ )
            m_LineColors[ i ] = m_colTextColor;
    }
    m_bExpectColorCode = FALSE;
}


//--------------------------------------------------------------------------------------
// Name: IncrementLine()
// Desc: Skip to the next line
//--------------------------------------------------------------------------------------
VOID Console::IncrementLine()
{
    m_nCurLine = ( m_nCurLine + 1 ) % m_cScreenHeightVirtual;
    m_cCurLineLength = 0;
    ZeroMemory( m_Lines[m_nCurLine], ( m_cScreenWidth + 1 ) * sizeof( WCHAR ) );
    if( m_LineColors )
        m_LineColors[ m_nCurLine ] = m_colTextColor;
}


//--------------------------------------------------------------------------------------
// Name: ScrollUp()
// Desc: Scroll the text window upwards
//--------------------------------------------------------------------------------------
VOID Console::ScrollUp( INT nLines )
{
    // Scroll exactly one page height if requested
    if( nLines >= PAGE_UP )
    {
        nLines = ( INT )m_cScreenHeight;
    }
    else if( nLines <= PAGE_DOWN )
    {
        nLines = -( INT )m_cScreenHeight;
    }

    m_nScrollOffset += nLines;

    m_nScrollOffset %= m_cScreenHeightVirtual;
}


//--------------------------------------------------------------------------------------
// Name: Destroy()
// Desc: Tear everything down
//--------------------------------------------------------------------------------------
VOID Console::Destroy()
{
    // Delete the memory we've allocated
    if( m_Lines )
    {
        delete[] m_Lines;
        m_Lines = NULL;
    }

    if( m_Buffer )
    {
        delete[] m_Buffer;
        m_Buffer = NULL;
    }

    if( m_LineColors )
    {
        delete[] m_LineColors;
        m_LineColors = NULL;
    }

    // Destroy the font
    m_Font.Destroy();

    if( m_pd3dDevice )
    {
        // Give GPU control back to the title
        m_pd3dDevice->Resume();

        // Release the D3D resources
        m_pd3dDevice->Release();
    }

    m_pd3dDevice = NULL;
    g_pd3dDevice = NULL;

}


//--------------------------------------------------------------------------------------
// Name: Render()
// Desc: Render the console to the screen
//--------------------------------------------------------------------------------------
VOID Console::Render()
{
    // Give GPU control back to us
    if( m_bSuspendFlag )
        m_pd3dDevice->Resume();

    m_bSuspendFlag = TRUE;

    // Render the background
    RenderBackground( m_colBackColor, m_colBackColor );

    // The top line
    UINT nTextLine = ( m_nCurLine - m_cScreenHeight + m_cScreenHeightVirtual - m_nScrollOffset + 1 )
        % m_cScreenHeightVirtual;


    m_Font.Begin();

    for( UINT nScreenLine = 0; nScreenLine < m_cScreenHeight; nScreenLine++ )
    {
        D3DCOLOR colLine = ( m_LineColors ? m_LineColors[ nTextLine ] : m_colTextColor );
        m_Font.DrawText( ( FLOAT )( m_cxSafeAreaOffset ),
                         ( FLOAT )( m_cySafeAreaOffset + m_fLineHeight * nScreenLine ),
                         colLine, m_Lines[nTextLine] );

        nTextLine = ( nTextLine + 1 ) % m_cScreenHeightVirtual;
    }

    m_Font.End();

    // Present the scene
    m_pd3dDevice->Present( NULL, NULL, NULL, NULL );

    // Take away GPU control so that the Guide can be rendered
    m_pd3dDevice->Suspend();
}


//--------------------------------------------------------------------------------------
// Name: Add( CHAR )
// Desc: Convert ANSI to WCHAR and add to the current line
//--------------------------------------------------------------------------------------
VOID Console::Add( CHAR ch )
{
    WCHAR wch;

    INT ret = MultiByteToWideChar( CP_ACP,        // ANSI code page
                                   0,             // No flags
                                   &ch,           // Character to convert
                                   1,             // Convert one byte
                                   &wch,          // Target wide character buffer
                                   1 );           // One wide character

    assert( ret == 1 );

    Add( wch );
}



//--------------------------------------------------------------------------------------
// Name: LineColorFromIndex()
// Desc: Map a ConsoleLineColor index to a D3DCOLOR (see AtgConsole.h)
//--------------------------------------------------------------------------------------
static D3DCOLOR LineColorFromIndex( BYTE index, D3DCOLOR colDefault )
{
    switch( index )
    {
    case ATG::LINE_COLOR_YELLOW: return 0xFFFFFF00;
    case ATG::LINE_COLOR_GREEN:  return 0xFF00FF00;
    case ATG::LINE_COLOR_RED:    return 0xFFFF0000;
    case ATG::LINE_COLOR_DEFAULT:
    default:                     return colDefault;
    }
}


//--------------------------------------------------------------------------------------
// Name: Add( WCHAR )
// Desc: Add a wide character to the current line
//--------------------------------------------------------------------------------------
VOID Console::Add( WCHAR wch )
{
    // A pending color tag: this char is a ConsoleLineColor index, not text
    if( m_bExpectColorCode )
    {
        m_bExpectColorCode = FALSE;
        if( m_LineColors )
            m_LineColors[ m_nCurLine ] = LineColorFromIndex( ( BYTE )wch, m_colTextColor );
        return;
    }

    // A color tag only opens at column 0, so message text can never
    // trigger it by accident. Both tag bytes are consumed, never shown.
    if( wch == ( WCHAR )LINE_COLOR_TAG && m_cCurLineLength == 0 )
    {
        m_bExpectColorCode = TRUE;
        return;
    }

    // If this is a newline, just increment lines and move on
    if( wch == L'\n' )
    {
        IncrementLine();
        return;
    }
	else if (wch == L'\r')
	{
		m_nCurLine = m_nCurLine % m_cScreenHeightVirtual;
		m_cCurLineLength = 0;
		ZeroMemory(m_Lines[m_nCurLine], m_cScreenWidth);
		return;
	}

    BOOL bIncrementLine = FALSE;  // Whether to wrap to the next line

    if( m_cCurLineLength == m_cScreenWidth )
    {
        bIncrementLine = TRUE;
    }
    else
    {
        // Try to append the character to the line
        m_Lines[ m_nCurLine ][ m_cCurLineLength ] = wch;

        if( m_Font.GetTextWidth( m_Lines[ m_nCurLine ] ) > m_cxSafeArea )
        {
            // The line is too long, we need to wrap the character to the next line
            m_Lines[ m_nCurLine][ m_cCurLineLength ] = L'\0';
            bIncrementLine = TRUE;
        }
    }

    // If we need to skip to the next line, do so
    if( bIncrementLine )
    {
        IncrementLine();
        m_Lines[ m_nCurLine ][0] = wch;
    }

    m_cCurLineLength++;
}



//--------------------------------------------------------------------------------------
// Name: Format()
// Desc: Output a variable argument list using a format string
//--------------------------------------------------------------------------------------
VOID Console::Format( _In_z_ _Printf_format_string_ LPCSTR strFormat, ... )
{
    va_list pArgList;
    va_start( pArgList, strFormat );
    FormatV( strFormat, pArgList );
    va_end( pArgList );
}

VOID Console::Format( _In_z_ _Printf_format_string_ LPCWSTR wstrFormat, ... )
{
    va_list pArgList;
    va_start( pArgList, wstrFormat );
    FormatV( wstrFormat, pArgList );
    va_end( pArgList );
}


//--------------------------------------------------------------------------------------
// Name: FormatV()
// Desc: Output a va_list using a format string
//--------------------------------------------------------------------------------------
VOID Console::FormatV( _In_z_ _Printf_format_string_ LPCSTR strFormat, va_list pArgList )
{
    // Count the required length of the string
    DWORD dwStrLen = _vscprintf( strFormat, pArgList ) + 1;    // +1 = null terminator
    CHAR* strMessage = ( CHAR* )_malloca( dwStrLen );
    vsprintf_s( strMessage, dwStrLen, strFormat, pArgList );

    m_bExpectColorCode = FALSE;

    // Output the string to the console
    for( DWORD i = 0; i < strlen( strMessage ); i++ )
    {
        Add( strMessage[i] );
    }

    // Output the string to the debug channel, if requested
    if( m_bOutputToDebugChannel )
    {
        OutputDebugStringA( strMessage );
    }

    // Render the new output
    Render();

    _freea( strMessage );
}

VOID Console::FormatV( _In_z_ _Printf_format_string_ LPCWSTR wstrFormat, va_list pArgList )
{
    // Count the required length of the string
    DWORD dwStrLen = _vscwprintf( wstrFormat, pArgList ) + 1;    // +1 = null terminator
    WCHAR* strMessage = ( WCHAR* )_malloca( dwStrLen * sizeof( WCHAR ) );
    vswprintf_s( strMessage, dwStrLen, wstrFormat, pArgList );

    m_bExpectColorCode = FALSE;

    // Output the string to the console
    for( DWORD i = 0; i < wcslen( strMessage ); i++ )
    {
        Add( strMessage[i] );
    }

    // Output the string to the debug channel, if requested
    if( m_bOutputToDebugChannel )
    {
        OutputDebugStringW( strMessage );
    }

    // Render the new output
    Render();

    _freea( strMessage );

}

//--------------------------------------------------------------------------------------
// Name: Display()
// Desc: Output a string
//--------------------------------------------------------------------------------------
VOID Console::Display( LPCSTR strDisplay )
{
	if (strDisplay == NULL)
		return;

    m_bExpectColorCode = FALSE;

	// Output the string to the console
	for( DWORD i = 0; i < strlen(strDisplay); i++ )
	{
		Add( strDisplay[i] );
	}

	// Output the string to the debug channel, if requested
	if( m_bOutputToDebugChannel )
	{
		OutputDebugStringA( strDisplay );
	}

	// Render the new output
	Render();
}

VOID Console::Display( LPCWSTR wstrDisplay )
{
	if (wstrDisplay == NULL)
		return;

    m_bExpectColorCode = FALSE;

	// Output the string to the console
	for( DWORD i = 0; i < wcslen( wstrDisplay ); i++ )
	{
		Add( wstrDisplay[i] );
	}

	// Output the string to the debug channel, if requested
	if( m_bOutputToDebugChannel )
	{
		OutputDebugStringW( wstrDisplay );
	}

	// Render the new output
	Render();
}

}
;  // namespace