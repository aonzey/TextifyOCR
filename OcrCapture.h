#pragma once

#include "UserConfig.h"

// Message posted to parent when OCR capture is complete
// wParam = 0 (success) or 1 (cancelled), lParam = pointer to CString (temp image path)
#define UWM_OCR_CAPTURE_COMPLETE (WM_APP + 100)

class COcrCapture : public CWindowImpl<COcrCapture>
{
public:
	COcrCapture(HWND hNotifyWnd);
	~COcrCapture();

	bool Create();

	BEGIN_MSG_MAP_EX(COcrCapture)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_KEYDOWN(OnKeyDown)
		MSG_WM_CAPTURECHANGED(OnCaptureChanged)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_SETCURSOR(OnSetCursor)
		END_MSG_MAP()

	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	void OnDestroy();
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnKeyDown(UINT vk, UINT nRepCnt, UINT nFlags);
	void OnCaptureChanged(CWindow wnd);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	BOOL OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);

private:
	void CaptureScreenRegion(const CRect& rc);
	void Cleanup();
	bool CreateScreenSnapshot();
	CRect GetSelectionRect() const;

	HWND m_hNotifyWnd;
	bool m_capturing;
	CPoint m_startPt;
	CPoint m_currentPt;
	CRect m_captureRect;
	CString m_tempImagePath;

	// Full-screen snapshot taken when the overlay opens (clean, without overlay)
	CBitmap m_screenBitmap;
	// Solid black bitmap used as AlphaBlend source for the dim overlay
	CBitmap m_blackBitmap;
	// Back buffer: full composite is rendered here first, then blitted to
	// the screen in a single operation — eliminates flicker.
	CBitmap m_backBitmap;
	CDC m_backDC;
	// Virtual screen origin (top-left of the snapshot in screen coordinates)
	CPoint m_screenOrigin{ 0, 0 };
	CSize m_screenSize{ 0, 0 };
};
