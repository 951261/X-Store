#include "ui.h"
#include "xui.h"

#include <xtl.h>
#include <xui.h>
#include <xuiapp.h>

#include <OutputConsole.h>
#include <stdio.h>

namespace
{
const WCHAR *const XSTORE_SCENE_SECTION = L"xuiscene";
const WCHAR *const XSTORE_FONT_SECTION = L"xarifont";
const WCHAR *const XSTORE_SCENE_FILE = L"file://game:/xstore_ui.xur";
const FLOAT SCENE_WIDTH = 1280.0f;
const FLOAT SCENE_HEIGHT = 720.0f;
const int VISIBLE_ROWS = 10;
const DWORD REPEAT_DELAY_MS = 350;
const DWORD REPEAT_INTERVAL_MS = 115;

enum SceneMode
{
    MODE_DOWNLOAD_TYPE,
    MODE_GAME_RESULTS,
    MODE_MEDIA_RESULTS,
    MODE_STATUS
};

struct XStoreSceneContext
{
    SceneMode mode;
    CXuiModule *app;
    DownloadType selectedType;
    const GameList *games;
    const MediaList *media;
    const char *gameName;
    LPCWSTR statusTitle;
    LPCWSTR statusMessage;
    int selectedIndex;
    int highlightedIndex;
    int scrollIndex;
};

int GameCount(const GameList *list)
{
    if (!list || !list->items || list->count > 0x7fffffff)
        return 0;

    return (int)list->count;
}

int MediaCount(const MediaList *list)
{
    if (!list || !list->items || list->count > 0x7fffffff)
        return 0;

    return (int)list->count;
}

int ListCount(const XStoreSceneContext *context)
{
    if (!context)
        return 0;

    return (context->mode == MODE_MEDIA_RESULTS)
               ? MediaCount(context->media)
               : GameCount(context->games);
}

const char *SafeText(const char *text, const char *fallback)
{
    return (text && text[0]) ? text : fallback;
}

void AppendWide(WCHAR *dst, DWORD dstCount, LPCWSTR src)
{
    if (!dst || dstCount == 0 || !src)
        return;

    DWORD pos = 0;
    while (pos < dstCount && dst[pos] != L'\0')
        ++pos;

    if (pos >= dstCount)
        return;

    for (DWORD i = 0; pos < dstCount - 1 && src[i] != L'\0'; ++i, ++pos)
        dst[pos] = src[i];

    dst[pos] = L'\0';
}

void AppendNarrow(WCHAR *dst, DWORD dstCount, const char *src)
{
    if (!dst || dstCount == 0 || !src)
        return;

    DWORD pos = 0;
    while (pos < dstCount && dst[pos] != L'\0')
        ++pos;

    if (pos >= dstCount)
        return;

    for (DWORD i = 0; pos < dstCount - 1 && src[i] != '\0'; ++i, ++pos)
        dst[pos] = (WCHAR)(unsigned char)src[i];

    dst[pos] = L'\0';
}

void FormatGameRow(const GameList *list, int index, WCHAR *dst, DWORD dstCount)
{
    if (!dst || dstCount == 0)
        return;

    dst[0] = L'\0';
    if (swprintf_s(dst, dstCount, L"%2d. ", index + 1) < 0)
        dst[0] = L'\0';

    const char *name = "(unnamed result)";
    if (list && index >= 0 && index < GameCount(list))
        name = SafeText(list->items[index].name, "(unnamed result)");

    AppendNarrow(dst, dstCount, name);
}

void FormatMediaRow(const MediaList *list, int index, WCHAR *dst, DWORD dstCount)
{
    if (!dst || dstCount == 0)
        return;

    dst[0] = L'\0';
    if (swprintf_s(dst, dstCount, L"%2d. Disc ", index + 1) < 0)
        dst[0] = L'\0';

    const MediaEntry *item = NULL;
    if (list && index >= 0 && index < MediaCount(list))
        item = &list->items[index];

    AppendNarrow(dst, dstCount, SafeText(item ? item->disc : NULL, "?"));
    AppendWide(dst, dstCount, L"   Version ");
    AppendNarrow(dst, dstCount, SafeText(item ? item->version : NULL, "?"));
}

void FormatListRow(const XStoreSceneContext *context, int index, WCHAR *dst, DWORD dstCount)
{
    if (context && context->mode == MODE_MEDIA_RESULTS)
        FormatMediaRow(context->media, index, dst, dstCount);
    else
        FormatGameRow(context ? context->games : NULL, index, dst, dstCount);
}

void ClampListSelection(XStoreSceneContext *context)
{
    if (!context)
        return;

    int count = ListCount(context);
    if (count <= 0)
    {
        context->highlightedIndex = 0;
        context->scrollIndex = 0;
        return;
    }

    if (context->highlightedIndex < 0)
        context->highlightedIndex = 0;

    if (context->highlightedIndex >= count)
        context->highlightedIndex = count - 1;

    if (context->scrollIndex < 0)
        context->scrollIndex = 0;

    if (context->highlightedIndex < context->scrollIndex)
        context->scrollIndex = context->highlightedIndex;

    if (context->highlightedIndex >= context->scrollIndex + VISIBLE_ROWS)
        context->scrollIndex = context->highlightedIndex - VISIBLE_ROWS + 1;
}

BOOL ReadControllerButtons(WORD *buttons)
{
    if (!buttons)
        return FALSE;

    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(state));

    if (XInputGetState(0, &state) != ERROR_SUCCESS)
        return FALSE;

    *buttons = state.Gamepad.wButtons;
    return TRUE;
}

void WaitForControllerRelease()
{
    WORD buttons = 0;
    while (ReadControllerButtons(&buttons) && buttons != 0)
        Sleep(50);
}

class XStoreScene : public CXuiSceneImpl
{
protected:
    CXuiControl m_btnOriginalXbox;
    CXuiControl m_btnXbox360;
    CXuiControl m_btnXBLA;
    CXuiControl m_btnUpdate;
    XStoreSceneContext *m_context;
    HXUIFONT m_titleFont;
    HXUIFONT m_rowFont;
    HXUIFONT m_statusFont;
    HXUIBRUSH m_backgroundBrush;
    HXUIBRUSH m_headerBrush;
    HXUIBRUSH m_panelBrush;
    HXUIBRUSH m_buttonBrush;
    HXUIBRUSH m_buttonFocusBrush;
    HXUIBRUSH m_buttonBorderBrush;
    HXUIBRUSH m_buttonFocusBorderBrush;
    HXUIBRUSH m_rowBrush;
    HXUIBRUSH m_selectedRowBrush;
    HXUIBRUSH m_accentBrush;

    XUI_BEGIN_MSG_MAP()
        XUI_ON_XM_INIT(OnInit)
        XUI_ON_XM_DESTROY(OnDestroy)
        XUI_ON_XM_RENDER(OnRender)
        XUI_ON_XM_KEYDOWN(OnKeyDown)
        XUI_ON_XM_NOTIFY_PRESS(OnNotifyPress)
    XUI_END_MSG_MAP()

    HRESULT OnInit(XUIMessageInit *pInitData, BOOL &bHandled)
    {
        m_context = (pInitData && pInitData->pvInitData)
                        ? (XStoreSceneContext *)pInitData->pvInitData
                        : NULL;

        HRESULT hr = AttachMenuButtons();
        if (FAILED(hr))
            return hr;

        hr = CreateResources();
        if (FAILED(hr))
            return hr;

        ClampListSelection(m_context);
        bHandled = TRUE;
        return S_OK;
    }

    HRESULT OnDestroy()
    {
        ReleaseResources();
        return S_OK;
    }

    HRESULT OnRender(XUIMessageRender *pRenderData, BOOL &bHandled)
    {
        bHandled = TRUE;

        XUIRenderStruct renderStruct;
        HRESULT hr = BeginRender(pRenderData, &renderStruct);
        if (FAILED(hr))
            return hr;

        D3DXMATRIX identity;
        D3DXMatrixIdentity(&identity);
        XuiRenderSetTransform(pRenderData->hDC, &identity);

        if (IsStatusMode())
            DrawStatus(pRenderData->hDC);
        else if (IsListMode())
            DrawList(pRenderData->hDC);
        else
            DrawDownloadMenu(pRenderData->hDC);

        EndRender(pRenderData, &renderStruct);
        XuiRenderRestoreState(pRenderData->hDC);
        return S_OK;
    }

    HRESULT OnKeyDown(XUIMessageInput *pInputData, BOOL &bHandled)
    {
        if (!pInputData || !IsDownloadTypeMode())
            return S_OK;

        if (pInputData->dwKeyCode == VK_PAD_B &&
            !(pInputData->dwFlags & XUI_INPUT_FLAG_REPEAT))
        {
            FinishDownloadType(DOWNLOAD_TYPE_NONE);
            bHandled = TRUE;
        }

        return S_OK;
    }

    HRESULT OnNotifyPress(HXUIOBJ hObjPressed, BOOL &bHandled)
    {
        if (!IsDownloadTypeMode())
            return S_OK;

        if (hObjPressed == m_btnOriginalXbox.m_hObj)
            FinishDownloadType(ORIGINAL_XBOX);
        else if (hObjPressed == m_btnXbox360.m_hObj)
            FinishDownloadType(XBOX_360);
        else if (hObjPressed == m_btnXBLA.m_hObj)
            FinishDownloadType(XBLA);
        else if (hObjPressed == m_btnUpdate.m_hObj)
            FinishDownloadType(AUTO_UPDATE);
        else
            return S_OK;

        bHandled = TRUE;
        return S_OK;
    }

    HRESULT AttachMenuButtons()
    {
        HRESULT hr = GetChildById(L"BtnOriginalXbox", &m_btnOriginalXbox);
        if (FAILED(hr))
            return hr;

        hr = GetChildById(L"BtnXbox360", &m_btnXbox360);
        if (FAILED(hr))
            return hr;

        hr = GetChildById(L"BtnXBLA", &m_btnXBLA);
        if (FAILED(hr))
            return hr;

        return GetChildById(L"BtnUpdateXStore", &m_btnUpdate);
    }

    HRESULT CreateResources()
    {
        HRESULT hr = XuiCreateFont(L"Arial Unicode MS", 38.0f, XUI_FONT_STYLE_NORMAL, 0, &m_titleFont);
        if (FAILED(hr))
        {
            ReleaseResources();
            return hr;
        }

        hr = XuiCreateFont(L"Arial Unicode MS", 24.0f, XUI_FONT_STYLE_NORMAL, 0, &m_rowFont);
        if (FAILED(hr))
        {
            ReleaseResources();
            return hr;
        }

        hr = XuiCreateFont(L"Arial Unicode MS", 21.0f, XUI_FONT_STYLE_NORMAL, 0, &m_statusFont);
        if (FAILED(hr))
        {
            ReleaseResources();
            return hr;
        }

        XuiCreateSolidBrush(D3DCOLOR_ARGB(255, 7, 13, 21), &m_backgroundBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(235, 13, 35, 50), &m_headerBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(235, 16, 27, 40), &m_panelBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(232, 26, 49, 67), &m_buttonBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(245, 37, 91, 108), &m_buttonFocusBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(255, 70, 99, 116), &m_buttonBorderBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(255, 115, 211, 196), &m_buttonFocusBorderBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(218, 21, 38, 54), &m_rowBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(245, 34, 86, 96), &m_selectedRowBrush);
        XuiCreateSolidBrush(D3DCOLOR_ARGB(255, 115, 211, 196), &m_accentBrush);
        return S_OK;
    }

    BOOL IsDownloadTypeMode() const
    {
        return !m_context || m_context->mode == MODE_DOWNLOAD_TYPE;
    }

    BOOL IsListMode() const
    {
        return m_context &&
               (m_context->mode == MODE_GAME_RESULTS ||
                m_context->mode == MODE_MEDIA_RESULTS);
    }

    BOOL IsStatusMode() const
    {
        return m_context && m_context->mode == MODE_STATUS;
    }

    void ShowMenuButtons(BOOL show)
    {
        SetButtonVisible(m_btnOriginalXbox, show);
        SetButtonVisible(m_btnXbox360, show);
        SetButtonVisible(m_btnXBLA, show);
        SetButtonVisible(m_btnUpdate, show);
    }

    void SetButtonVisible(CXuiControl &button, BOOL show)
    {
        if (!button.m_hObj)
            return;

        button.SetShow(show);
        button.SetEnable(show);
    }

    void FillRect(HXUIDC hDC, const XUIRect &rect, HXUIBRUSH brush)
    {
        if (!brush)
            return;

        XUIRect local = rect;
        XuiSelectBrush(hDC, brush);
        XuiFillRect(hDC, &local);
        XuiSelectBrush(hDC, NULL);
    }

    void DrawText(
        HXUIDC hDC,
        HXUIFONT font,
        LPCWSTR text,
        const XUIRect &rect,
        DWORD color,
        DWORD style)
    {
        if (!font || !text)
            return;

        D3DXMATRIX transform;
        D3DXMatrixIdentity(&transform);
        transform._41 = rect.left;
        transform._42 = rect.top;
        XuiRenderSetTransform(hDC, &transform);

        XUIRect local(0.0f, 0.0f, rect.right - rect.left, rect.bottom - rect.top);
        XuiSelectFont(hDC, font);
        XuiSetColorFactor(hDC, color);
        XuiDrawText(hDC, text, style, 0, &local);

        D3DXMatrixIdentity(&transform);
        XuiRenderSetTransform(hDC, &transform);
    }

    void DrawChrome(HXUIDC hDC)
    {
        FillRect(hDC, XUIRect(0.0f, 0.0f, 1280.0f, 720.0f), m_backgroundBrush);
        FillRect(hDC, XUIRect(0.0f, 0.0f, 1280.0f, 116.0f), m_headerBrush);
        FillRect(hDC, XUIRect(0.0f, 116.0f, 1280.0f, 120.0f), m_accentBrush);
    }

    void DrawDownloadMenu(HXUIDC hDC)
    {
        DrawChrome(hDC);
        FillRect(hDC, XUIRect(300.0f, 170.0f, 980.0f, 590.0f), m_panelBrush);

        DrawText(
            hDC,
            m_titleFont,
            L"X Store",
            XUIRect(84.0f, 28.0f, 780.0f, 84.0f),
            D3DCOLOR_ARGB(255, 255, 255, 255),
            XUI_FONT_STYLE_LEFT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);

        DrawText(
            hDC,
            m_statusFont,
            L"Choose a source to search",
            XUIRect(84.0f, 82.0f, 780.0f, 112.0f),
            D3DCOLOR_ARGB(255, 184, 210, 219),
            XUI_FONT_STYLE_LEFT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);

        DrawButton(hDC, m_btnOriginalXbox);
        DrawButton(hDC, m_btnXbox360);
        DrawButton(hDC, m_btnXBLA);
        DrawButton(hDC, m_btnUpdate);

        DrawText(
            hDC,
            m_statusFont,
            L"A Select     B Back",
            XUIRect(84.0f, 646.0f, 780.0f, 682.0f),
            D3DCOLOR_ARGB(255, 199, 211, 222),
            XUI_FONT_STYLE_LEFT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);
    }

    void DrawStatus(HXUIDC hDC)
    {
        DrawChrome(hDC);
        FillRect(hDC, XUIRect(250.0f, 240.0f, 1030.0f, 480.0f), m_panelBrush);

        LPCWSTR title = (m_context && m_context->statusTitle)
                            ? m_context->statusTitle
                            : L"Loading";
        LPCWSTR message = (m_context && m_context->statusMessage)
                              ? m_context->statusMessage
                              : L"Please wait...";

        DrawText(
            hDC,
            m_titleFont,
            title,
            XUIRect(290.0f, 286.0f, 990.0f, 340.0f),
            D3DCOLOR_ARGB(255, 255, 255, 255),
            XUI_FONT_STYLE_CENTER_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_ELLIPSIS);

        DrawText(
            hDC,
            m_statusFont,
            message,
            XUIRect(290.0f, 354.0f, 990.0f, 392.0f),
            D3DCOLOR_ARGB(255, 199, 211, 222),
            XUI_FONT_STYLE_CENTER_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_ELLIPSIS);

        FillRect(hDC, XUIRect(350.0f, 420.0f, 930.0f, 430.0f), m_rowBrush);
        FillRect(hDC, XUIRect(350.0f, 420.0f, 610.0f, 430.0f), m_accentBrush);
    }

    void DrawButton(HXUIDC hDC, CXuiControl &button)
    {
        if (!button.m_hObj)
            return;

        FLOAT width = 0.0f;
        FLOAT height = 0.0f;
        if (FAILED(button.GetBounds(&width, &height)))
            return;

        D3DXMATRIX transform;
        if (FAILED(button.GetFullXForm(&transform)))
            return;

        XuiRenderSetTransform(hDC, &transform);

        BOOL focused = button.HasFocus();
        FillRect(hDC, XUIRect(0.0f, 0.0f, width, height),
                 focused ? m_buttonFocusBorderBrush : m_buttonBorderBrush);
        FillRect(hDC, XUIRect(3.0f, 3.0f, width - 3.0f, height - 3.0f),
                 focused ? m_buttonFocusBrush : m_buttonBrush);

        LPCWSTR text = button.GetText();
        if (text)
        {
            XUIRect textRect(0.0f, 0.0f, width, height);
            XuiSelectFont(hDC, m_rowFont);
            XuiSetColorFactor(hDC, D3DCOLOR_ARGB(255, 255, 255, 255));
            XuiDrawText(
                hDC,
                text,
                XUI_FONT_STYLE_CENTER_ALIGN |
                    XUI_FONT_STYLE_VERTICAL_CENTER |
                    XUI_FONT_STYLE_SINGLE_LINE |
                    XUI_FONT_STYLE_ELLIPSIS,
                0,
                &textRect);
        }

        D3DXMatrixIdentity(&transform);
        XuiRenderSetTransform(hDC, &transform);
    }

    void DrawList(HXUIDC hDC)
    {
        ClampListSelection(m_context);
        int count = ListCount(m_context);
        BOOL mediaMode = m_context && m_context->mode == MODE_MEDIA_RESULTS;

        DrawChrome(hDC);
        FillRect(hDC, XUIRect(64.0f, 142.0f, 1216.0f, 608.0f), m_panelBrush);

        DrawText(
            hDC,
            m_titleFont,
            mediaMode ? L"Choose Version" : L"Search Results",
            XUIRect(76.0f, 34.0f, 840.0f, 90.0f),
            D3DCOLOR_ARGB(255, 255, 255, 255),
            XUI_FONT_STYLE_LEFT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);

        WCHAR subtitle[384];
        subtitle[0] = L'\0';
        if (mediaMode)
        {
            AppendWide(subtitle, ARRAYSIZE(subtitle), L"For ");
            AppendNarrow(subtitle, ARRAYSIZE(subtitle), SafeText(m_context ? m_context->gameName : NULL, "selected game"));
        }
        else
        {
            AppendWide(subtitle, ARRAYSIZE(subtitle), L"Select a game to download");
        }

        DrawText(
            hDC,
            m_statusFont,
            subtitle,
            XUIRect(78.0f, 88.0f, 930.0f, 124.0f),
            D3DCOLOR_ARGB(255, 184, 210, 219),
            XUI_FONT_STYLE_LEFT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_ELLIPSIS);

        WCHAR countText[64];
        if (mediaMode)
            swprintf_s(countText, ARRAYSIZE(countText), count == 1 ? L"1 version" : L"%d versions", count);
        else
            swprintf_s(countText, ARRAYSIZE(countText), count == 1 ? L"1 result" : L"%d results", count);
        DrawText(
            hDC,
            m_statusFont,
            countText,
            XUIRect(940.0f, 88.0f, 1204.0f, 124.0f),
            D3DCOLOR_ARGB(255, 184, 210, 219),
            XUI_FONT_STYLE_RIGHT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);

        if (count <= 0)
        {
            DrawText(
                hDC,
                m_rowFont,
                mediaMode ? L"No downloadable versions were found." : L"No matching games were found.",
                XUIRect(164.0f, 330.0f, 1116.0f, 382.0f),
                D3DCOLOR_ARGB(255, 206, 216, 224),
                XUI_FONT_STYLE_CENTER_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);
        }
        else
        {
            for (int row = 0; row < VISIBLE_ROWS; ++row)
            {
                int index = m_context->scrollIndex + row;
                if (index >= count)
                    break;

                FLOAT top = 160.0f + (FLOAT)(row * 43);
                BOOL selected = index == m_context->highlightedIndex;
                FillRect(
                    hDC,
                    XUIRect(76.0f, top, 1204.0f, top + 38.0f),
                    selected ? m_selectedRowBrush : m_rowBrush);

                if (selected)
                    FillRect(hDC, XUIRect(76.0f, top, 1204.0f, top + 3.0f), m_accentBrush);

                WCHAR rowText[384];
                FormatListRow(m_context, index, rowText, ARRAYSIZE(rowText));
                DrawText(
                    hDC,
                    m_rowFont,
                    rowText,
                    XUIRect(98.0f, top, 1182.0f, top + 38.0f),
                    D3DCOLOR_ARGB(255, 255, 255, 255),
                    XUI_FONT_STYLE_LEFT_ALIGN |
                        XUI_FONT_STYLE_VERTICAL_CENTER |
                        XUI_FONT_STYLE_SINGLE_LINE |
                        XUI_FONT_STYLE_ELLIPSIS);
            }
        }

        WCHAR footer[128];
        if (count > 0)
        {
            swprintf_s(
                footer,
                ARRAYSIZE(footer),
                mediaMode
                    ? L"A Select     B Back     D-Pad Move     %d/%d"
                    : L"A Select     B Back     Y New Search     D-Pad Move     %d/%d",
                m_context->highlightedIndex + 1,
                count);
        }
        else
        {
            swprintf_s(
                footer,
                ARRAYSIZE(footer),
                mediaMode ? L"B Back" : L"B Back     Y New Search");
        }

        DrawText(
            hDC,
            m_statusFont,
            footer,
            XUIRect(76.0f, 642.0f, 1204.0f, 680.0f),
            D3DCOLOR_ARGB(255, 199, 211, 222),
            XUI_FONT_STYLE_LEFT_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER | XUI_FONT_STYLE_SINGLE_LINE);
    }

    void FinishDownloadType(DownloadType selectedType)
    {
        if (!m_context)
            return;

        m_context->selectedType = selectedType;
        if (m_context->app)
            m_context->app->Quit();
    }

public:
    XStoreScene()
        : m_context(NULL),
          m_titleFont(NULL),
          m_rowFont(NULL),
          m_statusFont(NULL),
          m_backgroundBrush(NULL),
          m_headerBrush(NULL),
          m_panelBrush(NULL),
          m_buttonBrush(NULL),
          m_buttonFocusBrush(NULL),
          m_buttonBorderBrush(NULL),
          m_buttonFocusBorderBrush(NULL),
          m_rowBrush(NULL),
          m_selectedRowBrush(NULL),
          m_accentBrush(NULL)
    {
    }

    ~XStoreScene()
    {
        ReleaseResources();
    }

    void ReleaseResources()
    {
        if (m_titleFont)
        {
            XuiReleaseFont(m_titleFont);
            m_titleFont = NULL;
        }

        if (m_rowFont)
        {
            XuiReleaseFont(m_rowFont);
            m_rowFont = NULL;
        }

        if (m_statusFont)
        {
            XuiReleaseFont(m_statusFont);
            m_statusFont = NULL;
        }

        ReleaseBrush(&m_backgroundBrush);
        ReleaseBrush(&m_headerBrush);
        ReleaseBrush(&m_panelBrush);
        ReleaseBrush(&m_buttonBrush);
        ReleaseBrush(&m_buttonFocusBrush);
        ReleaseBrush(&m_buttonBorderBrush);
        ReleaseBrush(&m_buttonFocusBorderBrush);
        ReleaseBrush(&m_rowBrush);
        ReleaseBrush(&m_selectedRowBrush);
        ReleaseBrush(&m_accentBrush);
    }

    void ReleaseBrush(HXUIBRUSH *brush)
    {
        if (brush && *brush)
        {
            XuiDestroyBrush(*brush);
            *brush = NULL;
        }
    }

    XUI_IMPLEMENT_CLASS(XStoreScene, L"XStoreScene", XUI_CLASS_SCENE)
};

class XStoreXuiApp : public CXuiModule
{
public:
    XStoreXuiApp()
        : m_context(NULL),
          m_previousButtons(0),
          m_nextRepeatTick(0),
          m_listExitPending(FALSE),
          m_listExitReadyTick(0),
          m_statusQuitTick(0)
    {
    }

    void SetContext(XStoreSceneContext *context)
    {
        m_context = context;
        m_previousButtons = 0;
        m_nextRepeatTick = GetTickCount() + REPEAT_DELAY_MS;
        m_listExitPending = FALSE;
        m_listExitReadyTick = 0;
        m_statusQuitTick = GetTickCount() + 180;
        ReadControllerButtons(&m_previousButtons);
    }

protected:
    virtual HRESULT RegisterXuiClasses()
    {
        return XStoreScene::Register();
    }

    virtual HRESULT UnregisterXuiClasses()
    {
        XStoreScene::Unregister();
        return S_OK;
    }

    virtual HRESULT Render()
    {
        HRESULT hr = XuiRenderBegin(m_hDC, D3DCOLOR_ARGB(255, 0, 0, 0));
        if (FAILED(hr))
            return hr;

        UINT width = 0;
        UINT height = 0;
        hr = XuiRenderGetBackBufferSize(m_hDC, &width, &height);
        if (FAILED(hr))
        {
            XuiRenderEnd(m_hDC);
            return hr;
        }

        FLOAT scaleX = (FLOAT)width / SCENE_WIDTH;
        FLOAT scaleY = (FLOAT)height / SCENE_HEIGHT;
        FLOAT scale = (scaleX < scaleY) ? scaleX : scaleY;
        FLOAT offsetX = ((FLOAT)width - (SCENE_WIDTH * scale)) * 0.5f;
        FLOAT offsetY = ((FLOAT)height - (SCENE_HEIGHT * scale)) * 0.5f;

        D3DXMATRIX viewTransform;
        D3DXMatrixScaling(&viewTransform, scale, scale, 1.0f);
        viewTransform._41 = offsetX;
        viewTransform._42 = offsetY;
        XuiRenderSetViewTransform(m_hDC, &viewTransform);

        XUIMessage message;
        XUIMessageRender renderData;
        XuiMessageRender(&message, &renderData, m_hDC, 0xffffffff, XUI_BLEND_NORMAL);
        XuiSendMessage(m_hObjRoot, &message);

        XuiRenderEnd(m_hDC);
        XuiRenderPresent(m_hDC, NULL, NULL, NULL);
        return S_OK;
    }

    virtual void RunFrame()
    {
        CXuiModule::RunFrame();
        PollStatusQuit();
        PollListInput();
    }

    void PollStatusQuit()
    {
        if (!m_context ||
            m_context->mode != MODE_STATUS ||
            GetTickCount() < m_statusQuitTick)
        {
            return;
        }

        Quit();
    }

    void PollListInput()
    {
        if (!m_context ||
            (m_context->mode != MODE_GAME_RESULTS &&
             m_context->mode != MODE_MEDIA_RESULTS))
        {
            return;
        }

        WORD buttons = 0;
        if (!ReadControllerButtons(&buttons))
            return;

        DWORD now = GetTickCount();
        if (m_listExitPending)
        {
            if ((buttons & (XINPUT_GAMEPAD_A |
                            XINPUT_GAMEPAD_B |
                            XINPUT_GAMEPAD_Y |
                            XINPUT_GAMEPAD_DPAD_UP |
                            XINPUT_GAMEPAD_DPAD_DOWN)) == 0 &&
                now >= m_listExitReadyTick)
            {
                Quit();
            }

            m_previousButtons = buttons;
            return;
        }

        WORD pressed = buttons & ~m_previousButtons;
        WORD heldMove = buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN);
        BOOL repeat = heldMove && now >= m_nextRepeatTick;

        if (!heldMove)
            m_nextRepeatTick = now + REPEAT_DELAY_MS;
        else if (repeat)
            m_nextRepeatTick = now + REPEAT_INTERVAL_MS;

        if (pressed & XINPUT_GAMEPAD_A)
            FinishCurrentListItem();
        else if (pressed & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_Y))
            FinishList(-1);
        else if ((pressed & XINPUT_GAMEPAD_DPAD_UP) ||
                 (repeat && (buttons & XINPUT_GAMEPAD_DPAD_UP)))
            MoveList(-1);
        else if ((pressed & XINPUT_GAMEPAD_DPAD_DOWN) ||
                 (repeat && (buttons & XINPUT_GAMEPAD_DPAD_DOWN)))
            MoveList(1);

        m_previousButtons = buttons;
    }

    void MoveList(int delta)
    {
        if (!m_context || ListCount(m_context) <= 0)
            return;

        m_context->highlightedIndex += delta;
        ClampListSelection(m_context);
    }

    void FinishCurrentListItem()
    {
        if (!m_context || ListCount(m_context) <= 0)
        {
            FinishList(-1);
            return;
        }

        ClampListSelection(m_context);
        FinishList(m_context->highlightedIndex);
    }

    void FinishList(int selectedIndex)
    {
        if (!m_context)
            return;

        m_context->selectedIndex = selectedIndex;
        m_listExitPending = TRUE;
        m_listExitReadyTick = GetTickCount() + 80;
    }

private:
    XStoreSceneContext *m_context;
    WORD m_previousButtons;
    DWORD m_nextRepeatTick;
    BOOL m_listExitPending;
    DWORD m_listExitReadyTick;
    DWORD m_statusQuitTick;
};

HRESULT ComposeEmbeddedSectionLocator(const WCHAR *sectionName, WCHAR *locator, DWORD locatorCount)
{
    if (!sectionName || !locator || locatorCount == 0)
        return E_INVALIDARG;

    HMODULE module = GetModuleHandle(NULL);
    if (!module)
        return HRESULT_FROM_WIN32(GetLastError());

    if (swprintf_s(locator, locatorCount, L"section://%08x,%s", (DWORD)module, sectionName) < 0)
        return E_FAIL;

    return S_OK;
}

HRESULT RegisterDefaultTypeface(XStoreXuiApp *app)
{
    if (!app)
        return E_INVALIDARG;

    WCHAR fontLocator[MAX_PATH];
    HRESULT hr = ComposeEmbeddedSectionLocator(
        XSTORE_FONT_SECTION,
        fontLocator,
        ARRAYSIZE(fontLocator));
    if (FAILED(hr))
        return hr;

    return app->RegisterDefaultTypeface(L"Arial Unicode MS", fontLocator);
}

HRESULT RunXStoreScene(XStoreSceneContext *context)
{
    if (!context)
        return E_INVALIDARG;

    XStoreXuiApp app;
    context->app = &app;

    HRESULT hr = app.Init(XuiD3DXTextureLoader);
    if (FAILED(hr))
    {
        context->app = NULL;
        return hr;
    }

    hr = RegisterDefaultTypeface(&app);
    if (FAILED(hr))
    {
        app.Uninit();
        context->app = NULL;
        return hr;
    }

    app.SetContext(context);

    WCHAR sceneLocator[MAX_PATH];
    hr = ComposeEmbeddedSectionLocator(
        XSTORE_SCENE_SECTION,
        sceneLocator,
        ARRAYSIZE(sceneLocator));
    if (SUCCEEDED(hr))
        hr = app.LoadFirstScene(NULL, sceneLocator, context, NULL, XUSER_INDEX_ANY);

    if (FAILED(hr))
    {
        dprintf("XUI embedded scene load failed: 0x%08X; trying %S\n", hr, XSTORE_SCENE_FILE);
        hr = app.LoadFirstScene(NULL, XSTORE_SCENE_FILE, context, NULL, XUSER_INDEX_ANY);
    }

    if (FAILED(hr))
    {
        app.Uninit();
        context->app = NULL;
        return hr;
    }

    app.Run();
    if (context->mode != MODE_STATUS)
        WaitForControllerRelease();

    app.Uninit();
    context->app = NULL;
    return S_OK;
}

void InitSceneContext(XStoreSceneContext *context, SceneMode mode)
{
    ZeroMemory(context, sizeof(*context));
    context->mode = mode;
    context->selectedType = DOWNLOAD_TYPE_NONE;
    context->selectedIndex = -1;
}

int RunListScene(
    SceneMode mode,
    const GameList *games,
    const MediaList *media,
    const char *gameName)
{
    XStoreSceneContext context;
    InitSceneContext(&context, mode);
    context.games = games;
    context.media = media;
    context.gameName = gameName;

    HRESULT hr = RunXStoreScene(&context);
    if (FAILED(hr))
    {
        dprintf("XUI list scene failed: 0x%08X\n", hr);
        return -1;
    }

    int count = ListCount(&context);
    if (context.selectedIndex < 0 || context.selectedIndex >= count)
        return -1;

    return context.selectedIndex;
}
}

DownloadType ShowDownloadTypeMenuXUI()
{
    XStoreSceneContext context;
    InitSceneContext(&context, MODE_DOWNLOAD_TYPE);

    HRESULT hr = RunXStoreScene(&context);
    if (FAILED(hr))
    {
        dprintf("XUI download type scene failed: 0x%08X\n", hr);
        return DOWNLOAD_TYPE_NONE;
    }

    return context.selectedType;
}

int ShowSearchResultsXUI(const GameList *list)
{
    return RunListScene(MODE_GAME_RESULTS, list, NULL, NULL);
}

int ShowMediaResultsXUI(const MediaList *list, const char *gameName)
{
    int count = MediaCount(list);
    if (count == 1)
        return 0;

    return RunListScene(MODE_MEDIA_RESULTS, NULL, list, gameName);
}

BOOL RunStatusTaskXUI(
    LPCWSTR title,
    LPCWSTR message,
    XuiStatusTaskProc task,
    void *taskContext)
{
    if (!task)
        return FALSE;

    XStoreSceneContext context;
    InitSceneContext(&context, MODE_STATUS);
    context.statusTitle = title;
    context.statusMessage = message;

    HRESULT hr = RunXStoreScene(&context);
    if (FAILED(hr))
    {
        dprintf("XUI status scene failed: 0x%08X\n", hr);
        return task(taskContext);
    }

    return task(taskContext);
}
