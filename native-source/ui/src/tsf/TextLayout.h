#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <msctf.h>

typedef struct
{
    UINT nStart;
    UINT nEnd;
    TF_DISPLAYATTRIBUTE da;
} COMPOSITIONRENDERINFO;

//----------------------------------------------------------------
//
//
//
//----------------------------------------------------------------

struct CHARINFO
{
    D2D1_RECT_F rc;
    FLOAT GetWidth()
    {
        return rc.right - rc.left;
    }
};

//----------------------------------------------------------------
//
//
//
//----------------------------------------------------------------

typedef struct
{
    UINT nPos;
    UINT nCnt;
    FLOAT top;
    FLOAT bottom;
    CHARINFO *prgCharInfo;
} LINEINFO;

//----------------------------------------------------------------
//
//
//
//----------------------------------------------------------------

class CTextLayout
{
  public:
    CTextLayout()
    {
        _pDWriteFactory = NULL;
        _pTextFormat = NULL;
        _pTextLayout = NULL;
        _prgLines = NULL;
        _nLineCnt = 0;
        _fInterimCaret = FALSE;
        _fCaretVisible = TRUE;
        _layoutWidth = 0.0f;
        _nLineHeight = 0;
        _lineHeightDips = 0.0f;
        _dpiX = 96.0f;
        _dpiY = 96.0f;
        _paddingLeftDips = 0.0f;
        _paddingTopDips = 0.0f;
        _paddingRightDips = 0.0f;
        _paddingBottomDips = 0.0f;
        _horizontalScrollDips = 0.0f;
        _singleLine = FALSE;
        _rcCaret = D2D1::RectF();
        _useCustomColors = FALSE;
        _textColor = D2D1::ColorF(0x000000);
        _selectionColor = D2D1::ColorF(0x0078D4);
    }

    ~CTextLayout()
    {
        Clear();
        if (_pTextFormat)
        {
            _pTextFormat->Release();
        }
        if (_pDWriteFactory)
        {
            _pDWriteFactory->Release();
        }
    }

    BOOL Initialize(IDWriteFactory *pDWriteFactory);
    // CSS letter-spacing in DIPs, added after every character. Takes effect on the next Layout.
    void SetLetterSpacing(FLOAT dips)
    {
        _letterSpacingDips = dips;
    }
    // CSS line-height in DIPs for multi-line text (0 = the font's natural spacing). Takes
    // effect on the next Layout.
    void SetCssLineHeight(FLOAT dips)
    {
        _cssLineHeightDips = dips;
    }
    // Multi-line text scrolls vertically inside the layout height, like a <textarea>.
    FLOAT GetVerticalScroll() const
    {
        return _singleLine ? 0.0f : _verticalScrollDips;
    }
    FLOAT GetMaxVerticalScroll() const
    {
        const FLOAT overflow = _contentHeightDips - _viewportHeightDips;
        return (_singleLine || overflow < 0.0f) ? 0.0f : overflow;
    }
    FLOAT GetContentHeight() const
    {
        return _contentHeightDips;
    }
    FLOAT GetViewportHeight() const
    {
        return _viewportHeightDips;
    }
    // Clamped to [0, GetMaxVerticalScroll()]. Returns whether the offset changed.
    BOOL SetVerticalScroll(FLOAT dips);
    BOOL Layout(const WCHAR *psz, UINT nCnt, const LOGFONT *plf, FLOAT layoutWidthPixels, FLOAT layoutHeightPixels,
                FLOAT dpiX, FLOAT dpiY, FLOAT paddingLeftPixels = 0.0f, FLOAT paddingTopPixels = 0.0f,
                FLOAT paddingRightPixels = 0.0f, FLOAT paddingBottomPixels = 0.0f, BOOL singleLine = FALSE);
    BOOL Render(ID2D1RenderTarget *pRenderTarget, const WCHAR *psz, UINT nCnt, UINT nSelStart, UINT nSelEnd,
                const COMPOSITIONRENDERINFO *pCompositionRenderInfo, UINT nCompositionRenderInfo);
    BOOL RectFromCharPos(UINT nPos, RECT *prc);
    UINT CharPosFromPoint(POINT pt);
    UINT InsertionIndexFromPoint(POINT pt);
    UINT ExactCharPosFromPoint(POINT pt);
    UINT FineFirstEndCharPosInLine(UINT uCurPos, BOOL bFirst);
    // vertical FALSE only clamps the vertical scroll, so a relayout that didn't move the
    // caret keeps what the user scrolled to.
    void EnsureCaretVisible(UINT nPos, BOOL vertical = TRUE);
    void ToggleCaretBlink();
    void ResetCaretBlink();
    void SetCaretVisible(BOOL fVisible)
    {
        _fCaretVisible = fVisible;
    }
    void SetInterimCaret(BOOL fSet, UINT uPos);
    void SetColors(const D2D1_COLOR_F &textColor, const D2D1_COLOR_F &selectionColor)
    {
        _textColor = textColor;
        _selectionColor = selectionColor;
        _useCustomColors = TRUE;
    }
    BOOL IsInterimCaret()
    {
        return _fInterimCaret;
    }
    LONG GetPaddingLeftPixels() const
    {
        return DipsToPixelsX(_paddingLeftDips);
    }
    LONG GetPaddingTopPixels() const
    {
        return DipsToPixelsY(_paddingTopDips);
    }
    LONG GetPaddingRightPixels() const
    {
        return DipsToPixelsX(_paddingRightDips);
    }
    LONG GetPaddingBottomPixels() const
    {
        return DipsToPixelsY(_paddingBottomDips);
    }

    int GetLineHeight()
    {
        return _nLineHeight;
    }

  private:
    BOOL EnsureTextFormat(const LOGFONT *plf, FLOAT dpiY);
    BOOL RectFromCharPosDips(UINT nPos, D2D1_RECT_F *prc);
    BOOL RectFromCharPosDipsRaw(UINT nPos, D2D1_RECT_F *prc);
    FLOAT PixelsToDipsX(FLOAT value) const;
    FLOAT PixelsToDipsY(FLOAT value) const;
    LONG DipsToPixelsX(FLOAT value) const;
    LONG DipsToPixelsY(FLOAT value) const;
    void DrawUnderline(ID2D1RenderTarget *pRenderTarget, const TF_DISPLAYATTRIBUTE *pda, const D2D1_RECT_F &rc,
                       BOOL bClause);
    D2D1_COLOR_F GetAttributeColor(const TF_DA_COLOR *pdac, COLORREF fallbackColor);
    D2D1_COLOR_F ToColorF(COLORREF color);
    void Clear();

    IDWriteFactory *_pDWriteFactory;
    IDWriteTextFormat *_pTextFormat;
    IDWriteTextLayout *_pTextLayout;
    FLOAT _letterSpacingDips = 0.0f;
    FLOAT _cssLineHeightDips = 0.0f;
    FLOAT _verticalScrollDips = 0.0f;
    FLOAT _contentHeightDips = 0.0f;
    FLOAT _viewportHeightDips = 0.0f;
    int _nLineHeight;
    FLOAT _lineHeightDips;
    FLOAT _layoutWidth;
    FLOAT _dpiX;
    FLOAT _dpiY;
    FLOAT _paddingLeftDips;
    FLOAT _paddingTopDips;
    FLOAT _paddingRightDips;
    FLOAT _paddingBottomDips;
    FLOAT _horizontalScrollDips;
    BOOL _singleLine;

    LINEINFO *_prgLines;
    UINT _nLineCnt;

    BOOL _fInterimCaret;
    BOOL _fCaretVisible;
    D2D1_RECT_F _rcCaret;
    BOOL _useCustomColors;
    D2D1_COLOR_F _textColor;
    D2D1_COLOR_F _selectionColor;
};
