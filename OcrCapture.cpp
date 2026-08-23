#include "stdafx.h"
#include "resource.h"
#include "OcrCapture.h"
#include "Functions.h"
#include <atlimage.h>
#pragma comment(lib, "msimg32.lib")

// GDI+ initialization helper
namespace
{
	ULONG_PTR g_gdiplusToken = 0;
	bool InitGdiPlus()
	{
		if(g_gdiplusToken != 0)
			return true;

		Gdiplus::GdiplusStartupInput gdiplusStartupInput;
		if(Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok)
			return true;

		return false;
	}
}

COcrCapture::COcrCapture(HWND hNotifyWnd) :
	m_hNotifyWnd(hNotifyWnd),
	m_capturing(false)
{
}

COcrCapture::~COcrCapture()
{
	Cleanup();
}

bool COcrCapture::Create()
{
	InitGdiPlus();

	// Take a clean snapshot of the whole virtual screen BEFORE the
	// overlay window is shown, so the snapshot never contains the overlay.
	if(!CreateScreenSnapshot())
		return false;

	// Get the virtual screen coordinates (all monitors)
	RECT rc = { m_screenOrigin.x, m_screenOrigin.y,
		m_screenOrigin.x + m_screenSize.cx, m_screenOrigin.y + m_screenSize.cy };

	CWindowImpl<COcrCapture>::Create(NULL, rc, NULL,
		WS_POPUP,
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW);

	if(m_hWnd)
	{
		ShowWindow(SW_SHOWNORMAL);
		::SetForegroundWindow(m_hWnd);
		SetFocus();
		SetCursor(LoadCursor(NULL, IDC_CROSS));
		// Note: no SetCapture() and no selection here — the selection
		// rectangle only appears once the user actually drags the mouse
		// (see OnLButtonDown / OnMouseMove).
		m_capturing = false;
		m_startPt = { 0, 0 };
		m_currentPt = { 0, 0 };
	}

	return m_hWnd != NULL;
}

bool COcrCapture::CreateScreenSnapshot()
{
	m_screenOrigin.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
	m_screenOrigin.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
	m_screenSize.cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	m_screenSize.cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	if(m_screenSize.cx <= 0 || m_screenSize.cy <= 0)
		return false;

	CDC screenDC = ::GetDC(NULL);
	if(!screenDC)
		return false;

	CDC memDC;
	memDC.CreateCompatibleDC(screenDC);

	// Snapshot of the entire virtual screen.
	m_screenBitmap.CreateCompatibleBitmap(screenDC, m_screenSize.cx, m_screenSize.cy);
	HBITMAP hOldBitmap = memDC.SelectBitmap(m_screenBitmap);
	memDC.BitBlt(0, 0, m_screenSize.cx, m_screenSize.cy, screenDC,
		m_screenOrigin.x, m_screenOrigin.y, SRCCOPY);

	// Solid black bitmap, used as the AlphaBlend source for dimming.
	m_blackBitmap.CreateCompatibleBitmap(screenDC, m_screenSize.cx, m_screenSize.cy);
	HBITMAP hOldBlack = memDC.SelectBitmap(m_blackBitmap);
	RECT rcAll = { 0, 0, m_screenSize.cx, m_screenSize.cy };
	::FillRect(memDC, &rcAll, (HBRUSH)::GetStockObject(BLACK_BRUSH));
	memDC.SelectBitmap(hOldBlack);

	// Back buffer for flicker-free painting. The bitmap stays selected in
	// m_backDC for the lifetime of the window (never deselected — this way
	// OnPaint can always blit directly from m_backDC).
	m_backBitmap.CreateCompatibleBitmap(screenDC, m_screenSize.cx, m_screenSize.cy);
	m_backDC.CreateCompatibleDC(screenDC);
	m_backDC.SelectBitmap(m_backBitmap);

	memDC.SelectBitmap(hOldBitmap);

	::ReleaseDC(NULL, screenDC);
	return true;
}

int COcrCapture::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return 0;
}

void COcrCapture::OnDestroy()
{
	if(GetCapture() == m_hWnd)
		ReleaseCapture();
}

CRect COcrCapture::GetSelectionRect() const
{
	CRect rc{ m_startPt, m_currentPt };
	rc.NormalizeRect();
	return rc;
}

void COcrCapture::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_startPt = point;
	m_currentPt = point;
	m_capturing = true;
	SetCapture(); // capture only while the button is held down
}

void COcrCapture::OnMouseMove(UINT nFlags, CPoint point)
{
	if(!m_capturing || !(nFlags & MK_LBUTTON))
		return;

	// Repaint only the area covered by the old and the new selection
	// rectangle (plus the border and the size label), never the whole
	// screen — this keeps dragging smooth and flicker-free.
	CRect rcOld = GetSelectionRect();
	m_currentPt = point;
	CRect rcNew = GetSelectionRect();

	CRect rcUpdate;
	rcUpdate.UnionRect(&rcOld, &rcNew);
	// Room for the 2px border, the size label above (or below) the
	// rectangle, and some slack.
	rcUpdate.InflateRect(80, 32, 80, 28);
	rcUpdate.IntersectRect(rcUpdate, CRect(0, 0, m_screenSize.cx, m_screenSize.cy));
	InvalidateRect(&rcUpdate, FALSE);
}

void COcrCapture::OnCaptureChanged(CWindow /*wnd*/)
{
	// Mouse capture was lost (another window grabbed it, or the window is
	// being destroyed) — stop drawing the selection.
	m_capturing = false;
}

void COcrCapture::OnLButtonUp(UINT nFlags, CPoint point)
{
	if(!m_capturing)
		return;

	m_capturing = false;
	ReleaseCapture();

	m_captureRect.left = (m_startPt.x < m_currentPt.x) ? m_startPt.x : m_currentPt.x;
	m_captureRect.top = (m_startPt.y < m_currentPt.y) ? m_startPt.y : m_currentPt.y;
	m_captureRect.right = (m_startPt.x > m_currentPt.x) ? m_startPt.x : m_currentPt.x;
	m_captureRect.bottom = (m_startPt.y > m_currentPt.y) ? m_startPt.y : m_currentPt.y;

	// Check if the selection is big enough
	if(m_captureRect.Width() < 5 || m_captureRect.Height() < 5)
	{
		DestroyWindow();
		::PostMessage(m_hNotifyWnd, UWM_OCR_CAPTURE_COMPLETE, 1, 0);
		return;
	}

	// Convert client coordinates to screen coordinates
	CPoint screenPt{ m_captureRect.TopLeft() };
	ClientToScreen(&screenPt);
	CPoint screenPtBottom{ m_captureRect.BottomRight() };
	ClientToScreen(&screenPtBottom);

	CRect screenRect{ screenPt, screenPtBottom };

	// Capture the region from the clean snapshot (no need to hide the window)
	CaptureScreenRegion(screenRect);

	DestroyWindow();

	// Notify parent with the temp image path
	CString* pPath = new CString(m_tempImagePath);
	::PostMessage(m_hNotifyWnd, UWM_OCR_CAPTURE_COMPLETE, 0,
		reinterpret_cast<LPARAM>(pPath));
}

void COcrCapture::OnKeyDown(UINT vk, UINT nRepCnt, UINT nFlags)
{
	if(vk == VK_ESCAPE)
	{
		DestroyWindow();
		::PostMessage(m_hNotifyWnd, UWM_OCR_CAPTURE_COMPLETE, 1, 0);
	}
}

BOOL COcrCapture::OnEraseBkgnd(CDCHandle dc)
{
	// The whole client area is repainted in OnPaint.
	return TRUE;
}

void COcrCapture::OnPaint(CDCHandle /*dc*/)
{
	CPaintDC dc(m_hWnd);

	CRect rcClient;
	GetClientRect(&rcClient);
	int cx = rcClient.Width();
	int cy = rcClient.Height();
	if(cx <= 0 || cy <= 0)
		return;

	CDC snapshotDC;
	snapshotDC.CreateCompatibleDC(dc);
	HBITMAP hOldSnapshot = snapshotDC.SelectBitmap(m_screenBitmap);
	if(!hOldSnapshot)
		return;

	if(m_backBitmap.m_hBitmap == NULL || m_backDC.m_hDC == NULL)
	{
		snapshotDC.SelectBitmap(hOldSnapshot);
		return;
	}

	// ---- Composite everything into the back buffer first ----
	// (m_backBitmap is permanently selected in m_backDC — see
	// CreateScreenSnapshot — so it can never be missing when we blit.)

	// 1. Draw the clean screen snapshot as the background.
	m_backDC.BitBlt(0, 0, cx, cy, snapshotDC, 0, 0, SRCCOPY);

	// 2. Dim the whole screen with a semi-transparent black overlay.
	if(m_blackBitmap.m_hBitmap != NULL)
	{
		CDC blackDC;
		blackDC.CreateCompatibleDC(dc);
		HBITMAP hOldBlack = blackDC.SelectBitmap(m_blackBitmap);
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 110, 0 };
		::AlphaBlend(m_backDC, 0, 0, cx, cy, blackDC, 0, 0, cx, cy, blend);
		blackDC.SelectBitmap(hOldBlack);
	}

	// 3. Draw the selection rectangle (restore original brightness inside it).
	if(m_capturing)
	{
		CRect rc = GetSelectionRect();

		if(rc.Width() > 0 && rc.Height() > 0)
		{
			// Restore the original (undimmed) content inside the selection.
			m_backDC.BitBlt(rc.left, rc.top, rc.Width(), rc.Height(),
				snapshotDC, rc.left, rc.top, SRCCOPY);

			// Selection border.
			CPen pen;
			pen.CreatePen(PS_SOLID, 2, RGB(0, 150, 255));
			HPEN hOldPen = m_backDC.SelectPen(pen);
			HBRUSH hOldBrush = m_backDC.SelectBrush((HBRUSH)::GetStockObject(NULL_BRUSH));
			m_backDC.Rectangle(&rc);
			m_backDC.SelectBrush(hOldBrush);
			m_backDC.SelectPen(hOldPen);

			// Size label above the rectangle (below if there is no room).
			CString sizeText;
			sizeText.Format(L"%dx%d", rc.Width(), rc.Height());
			m_backDC.SetBkMode(OPAQUE);
			m_backDC.SetBkColor(RGB(0, 120, 215));
			m_backDC.SetTextColor(RGB(255, 255, 255));
			int textY = (rc.top - 20 < 0) ? rc.bottom + 2 : rc.top - 20;
			m_backDC.TextOut(rc.left, textY, sizeText);
			m_backDC.SetBkMode(TRANSPARENT);
		}
	}

	snapshotDC.SelectBitmap(hOldSnapshot);

	// ---- Single blit to the screen (clipped to the invalid region) ----
	dc.BitBlt(0, 0, cx, cy, m_backDC, 0, 0, SRCCOPY);
}

BOOL COcrCapture::OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
{
	if(nHitTest == HTCLIENT)
	{
		SetCursor(LoadCursor(NULL, IDC_CROSS));
		return TRUE;
	}
	return FALSE;
}

void COcrCapture::CaptureScreenRegion(const CRect& rc)
{
	if(m_screenBitmap.m_hBitmap == NULL)
		return;

	// rc is in screen coordinates; the snapshot origin is the virtual
	// screen top-left corner.
	int srcX = rc.left - m_screenOrigin.x;
	int srcY = rc.top - m_screenOrigin.y;

	CDC snapshotDC;
	snapshotDC.CreateCompatibleDC(NULL);
	HBITMAP hOldSnapshot = snapshotDC.SelectBitmap(m_screenBitmap);

	CDC memDC;
	memDC.CreateCompatibleDC(NULL);

	CBitmap bitmap;
	bitmap.CreateCompatibleBitmap(snapshotDC, rc.Width(), rc.Height());

	HBITMAP hOldBitmap = memDC.SelectBitmap(bitmap);
	memDC.BitBlt(0, 0, rc.Width(), rc.Height(), snapshotDC, srcX, srcY, SRCCOPY);
	memDC.SelectBitmap(hOldBitmap);

	snapshotDC.SelectBitmap(hOldSnapshot);

	// Save to temp PNG file using CImage
	WCHAR tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);

	CString tempFile;
	tempFile.Format(L"%sTextifyOCR_%lu.png", tempPath, GetTickCount());

	CImage image;
	image.Attach(bitmap.Detach());

	HRESULT hr = image.Save(tempFile);
	if(SUCCEEDED(hr))
	{
		m_tempImagePath = tempFile;
	}
	else
	{
		// Fallback: save as BMP
		CString bmpFile;
		bmpFile.Format(L"%sTextifyOCR_%lu.bmp", tempPath, GetTickCount());
		hr = image.Save(bmpFile);
		if(SUCCEEDED(hr))
			m_tempImagePath = bmpFile;
	}
}

void COcrCapture::Cleanup()
{
	// Temp file cleanup is handled by the parent after OCR processing
}
