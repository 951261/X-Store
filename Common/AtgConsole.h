//--------------------------------------------------------------------------------------
// AtgConsole.h
//
// Console class for simple applications that need no input and only console output.
// Use this INSTEAD of AtgApplication.
//
// Requires .TGA/.ABC files for the console font. 
//
// Xbox Advanced Technology Group.
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#pragma once
#ifndef ATGCONSOLE_H
#define ATGCONSOLE_H

#include <xtl.h>
#include "AtgDevice.h"
#include "AtgFont.h"

namespace ATG
{

// Inline per-line color tag: when a line starts with LINE_COLOR_TAG,
// the next character selects that line's color (see ConsoleLineColor).
// Only recognized at column 0, so message text can never trigger it
// by accident. The tag bytes are consumed, never displayed.
static const CHAR LINE_COLOR_TAG = '\x1F';

enum ConsoleLineColor
{
    LINE_COLOR_DEFAULT = 1,
    LINE_COLOR_YELLOW  = 2,
    LINE_COLOR_GREEN   = 3,
    LINE_COLOR_RED     = 4
};

//--------------------------------------------------------------------------------------
// Name: class Console
// Desc: Class to implement the console.
//--------------------------------------------------------------------------------------
class Console
{
public:
                    Console();
                    ~Console();

    // Initialization
    HRESULT         Create( LPCSTR strFontFileName,
                            D3DCOLOR colBackColor,
                            D3DCOLOR colTextColor,
                            UINT nLines = 0 );

    VOID            Destroy();

    // Clear the screen
    VOID            Clear();

    // Clear the screen without drawing a blank frame
    VOID            ClearNoRender();

    // Console output
	virtual VOID    Display( LPCSTR strDisplay );
	virtual VOID    Display( LPCWSTR wstrDisplay );
    virtual VOID    Format( _In_z_ _Printf_format_string_ LPCSTR strFormat, ... );
    virtual VOID    Format( _In_z_ _Printf_format_string_ LPCWSTR wstrFormat, ... );
    virtual VOID    FormatV( _In_z_ _Printf_format_string_ LPCSTR strFormat, va_list pArgList );
    virtual VOID    FormatV( _In_z_ _Printf_format_string_ LPCWSTR wstrFormat, va_list pArgList );

    // Send output to debug channel
    VOID            SendOutputToDebugChannel( BOOL bOutputToDebugChannel )
    {
        m_bOutputToDebugChannel = bOutputToDebugChannel;
    }

    // method for rendering the console
    VOID            Render();

    // method for scrolling the text window up/down
    VOID            ScrollUp( INT nLines );

    static const INT PAGE_UP    = +255;
    static const INT PAGE_DOWN  = -255;

private:
    // Constants
    static const UINT SCREEN_SIZE_X_DEFAULT    = 640;
    static const UINT SCREEN_SIZE_Y_DEFAULT    = 480;
    static const UINT SCREEN_SIZE_X_720p       = 1280;
    static const UINT SCREEN_SIZE_Y_720p       = 720;

    static const UINT SAFE_AREA_PCT_4x3        = 85;
    static const UINT SAFE_AREA_PCT_HDTV       = 90;

    // Safe area dimensions
    UINT m_cxSafeArea;
    UINT m_cySafeArea;

    UINT m_cxSafeAreaOffset;
    UINT m_cySafeAreaOffset;

    // Send console output to debug channel
    BOOL m_bOutputToDebugChannel;

    // Main objects used for creating and rendering the 3D scene
    static D3DPRESENT_PARAMETERS m_d3dpp;
    static D3DDevice* m_pd3dDevice;

    // Font for rendering text
    Font m_Font;

    // Colors
    D3DCOLOR m_colBackColor;
    D3DCOLOR m_colTextColor;

    // Per-line text colors (parallel to the m_Lines ring buffer)
    D3DCOLOR* m_LineColors;
    BOOL m_bExpectColorCode;   // TRUE when the next char is a color index

    // Text Buffers
    UINT m_cScreenHeight;        // height in lines of screen area
    UINT m_cScreenHeightVirtual; // height in lines of text storage buffer
    UINT m_cScreenWidth;         // width in characters
    FLOAT m_fLineHeight;          // height of a single line in pixels

    WCHAR* m_Buffer;               // buffer big enough to hold a full screen
    WCHAR** m_Lines;                // pointers to individual lines
    UINT m_nCurLine;             // index of current line being written to
    UINT m_cCurLineLength;       // length of the current line
    INT m_nScrollOffset;        // offset to display text (in lines)

    BOOL m_bSuspendFlag;         // Device suspended tracking flag

    // Add a character to the current line
    VOID            Add( CHAR ch );
    VOID            Add( WCHAR wch );

    // Increment to the next line
    VOID            IncrementLine();
};

}
// namespace ATG

#endif