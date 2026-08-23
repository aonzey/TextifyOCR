#include "stdafx.h"
#include "resource.h"

#include <string>
#include <vector>
#include <algorithm>

#include "MainDlg.h"
#include "TextDlg.h"
#include "SettingsDlg.h"
#include "Functions.h"
#include "update.h"
#include "version.h"

BOOL CMainDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	// Center the dialog on the screen.
	CenterWindow();

	// Set icons.
	ReloadMainIcon();

	// Load and apply config.
	m_config.emplace();
	ApplyUiLanguage();
	ApplyMouseAndKeyboardHotKeys();
	ConfigToGui();

	if(lInitParam || m_config->m_hideWndOnStartup)
	{
		m_hideDialog = true;
		// Setting WS_EX_NOACTIVATE is a workaround for preventing the dialog
		// from stealing focus on startup.
		ModifyStyleEx(0, WS_EX_NOACTIVATE);
	}

	// Init and show tray icon.
	InitNotifyIconData();

	if(!m_config->m_hideTrayIcon)
	{
		Shell_NotifyIcon(NIM_ADD, &m_notifyIconData);
	}

	// Start timer to check for updates.
	if(m_config->m_checkForUpdates)
	{
		SetTimer(TIMER_UPDATE_CHECK, 1000 * 10, NULL); // 10sec
	}

	return TRUE;
}

void CMainDlg::OnDestroy()
{
	UninitMouseAndKeyboardHotKeys();

	if(!m_config->m_hideTrayIcon)
	{
		Shell_NotifyIcon(NIM_DELETE, &m_notifyIconData);
	}

	if(m_config->m_checkForUpdates)
	{
		KillTimer(TIMER_UPDATE_CHECK);
	}

	// From GDI handle checks, not all icons are freed automatically.
	::DestroyIcon(SetIcon(nullptr, TRUE));
	::DestroyIcon(SetIcon(nullptr, FALSE));
	::DestroyIcon(CStatic(GetDlgItem(IDC_MAIN_ICON)).SetIcon(nullptr));
}

void CMainDlg::OnWindowPosChanging(LPWINDOWPOS lpWndPos)
{
	if(m_hideDialog && (lpWndPos->flags & SWP_SHOWWINDOW))
	{
		ModifyStyleEx(WS_EX_NOACTIVATE, 0);
		lpWndPos->flags &= ~SWP_SHOWWINDOW;
		m_hideDialog = false;
	}
}

LRESULT CMainDlg::OnNotify(int idCtrl, LPNMHDR pnmh)
{
	switch(pnmh->idFrom)
	{
	case IDC_MAIN_SYSLINK:
		switch(pnmh->code)
		{
		case NM_CLICK:
		case NM_RETURN:
			if((int)(DWORD_PTR)ShellExecute(m_hWnd, L"open", ((PNMLINK)pnmh)->item.szUrl, NULL, NULL, SW_SHOWNORMAL) <= 32)
			{
				CString title;
				title.LoadString(IDS_ERROR);

				CString text;
				text.LoadString(IDS_ERROR_OPEN_ADDRESS);
				text += L"\n";
				text += ((PNMLINK)pnmh)->item.szUrl;

				MessageBox(text, title, MB_ICONERROR);
			}
			break;
		}
		break;
	}

	SetMsgHandled(FALSE);
	return 0;
}

void CMainDlg::OnHotKey(int nHotKeyID, UINT uModifiers, UINT uVirtKey)
{
	if(nHotKeyID == 1)
	{
		CPoint pt;
		GetCursorPos(&pt);

		CTextDlg dlgText(*m_config);
		dlgText.DoModal(NULL, reinterpret_cast<LPARAM>(&pt));
	}
	else if(nHotKeyID == 2)
	{
		// OCR hotkey
		StartOcrCapture();
	}
}

void CMainDlg::OnTimer(UINT_PTR nIDEvent)
{
	if(nIDEvent == TIMER_UPDATE_CHECK)
	{
		KillTimer(TIMER_UPDATE_CHECK);

		if(UpdateCheckInit(m_hWnd, UWM_UPDATE_CHECKED))
		{
			if(UpdateCheckQueue())
			{
				m_checkingForUpdates = true;
			}
			else
			{
				UpdateCheckCleanup();
			}
		}

		if(!m_checkingForUpdates)
		{
			SetTimer(TIMER_UPDATE_CHECK, 1000 * 60 * 60, NULL); // 1h
		}
	}
}

void CMainDlg::OnDpiChanged(UINT nDpiX, UINT nDpiY, PRECT pRect)
{
	ReloadMainIcon();
}

void CMainDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	bool ctrlKey = (CButton(GetDlgItem(IDC_CHECK_CTRL)).GetCheck() != BST_UNCHECKED);
	bool altKey = (CButton(GetDlgItem(IDC_CHECK_ALT)).GetCheck() != BST_UNCHECKED);
	bool shiftKey = (CButton(GetDlgItem(IDC_CHECK_SHIFT)).GetCheck() != BST_UNCHECKED);

	if(!ctrlKey && !altKey && !shiftKey)
	{
		CString title;
		title.LoadString(IDS_MAINDLG_WARNING_MODIFIER_TITLE);

		CString text;
		text.LoadString(IDS_MAINDLG_WARNING_MODIFIER_TEXT);

		if(MessageBox(text, title, MB_ICONWARNING | MB_YESNO) != IDYES)
		{
			return;
		}
	}

	int mouseKey;
	auto keysComboWnd = CComboBox(GetDlgItem(IDC_COMBO_KEYS));
	switch(keysComboWnd.GetCurSel())
	{
	case 0:
		mouseKey = VK_LBUTTON;
		break;

	case 1:
		mouseKey = VK_RBUTTON;
		break;

	case 2:
		mouseKey = VK_MBUTTON;
		break;
	}

	HotKey& mouseHotKey = m_config->m_mouseHotKey;
	mouseHotKey.ctrl = ctrlKey;
	mouseHotKey.alt = altKey;
	mouseHotKey.shift = shiftKey;
	mouseHotKey.key = mouseKey;

	m_config->SaveToIniFile();

	ApplyMouseAndKeyboardHotKeys();

	CButton(GetDlgItem(IDOK)).EnableWindow(FALSE);
}

void CMainDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ShowWindow(SW_HIDE);
}

void CMainDlg::OnShowIni(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CSettingsDlg settingsDlg;
	INT_PTR nRet = settingsDlg.DoModal();
	if(nRet == IDOK)
	{
		LANGID oldUiLanguage = m_config->m_uiLanguage;
		bool oldCheckForUpdates = m_config->m_checkForUpdates;
		bool oldHideTrayIcon = m_config->m_hideTrayIcon;

		m_config.emplace();

		LANGID newUiLanguage = m_config->m_uiLanguage;
		bool newCheckForUpdates = m_config->m_checkForUpdates;
		bool newHideTrayIcon = m_config->m_hideTrayIcon;

		if(oldUiLanguage != newUiLanguage)
		{
			ApplyUiLanguage();
		}

		ApplyMouseAndKeyboardHotKeys();
		ConfigToGui();

		if(newHideTrayIcon != oldHideTrayIcon)
		{
			Shell_NotifyIcon(newHideTrayIcon ? NIM_DELETE : NIM_ADD, &m_notifyIconData);
		}

		if(newCheckForUpdates != oldCheckForUpdates && !m_checkingForUpdates)
		{
			if(newCheckForUpdates)
			{
				SetTimer(TIMER_UPDATE_CHECK, 1000 * 10, NULL); // 10sec
			}
			else
			{
				KillTimer(TIMER_UPDATE_CHECK);
			}
		}
	}
}

void CMainDlg::OnExitButton(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	Exit();
}

void CMainDlg::OnConfigChanged(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CButton(GetDlgItem(IDOK)).EnableWindow();
}

LRESULT CMainDlg::OnMouseHookClicked(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//CPoint ptEvent{ static_cast<int>(wParam), static_cast<int>(lParam) };

	// Instead of using the mouse hook coordinates, we query the cursor position
	// again, because the hooked coordinates end up incorrect for DPI-unaware
	// applications in high DPI environment.

	CPoint pt;
	GetCursorPos(&pt);

	CTextDlg dlgText(*m_config);
	dlgText.DoModal(NULL, reinterpret_cast<LPARAM>(&pt));

	return 0;
}

LRESULT CMainDlg::OnTaskbarCreated(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(!m_config->m_hideTrayIcon)
	{
		// Reload icon since the DPI might have changed. From the documentation:
		// "On Windows 10, the taskbar also broadcasts this message when the DPI of
		// the primary display changes."
		m_trayIcon = LoadTrayIcon();
		m_notifyIconData.hIcon = m_trayIcon;

		Shell_NotifyIcon(NIM_ADD, &m_notifyIconData);

		// Necessary to apply the newly loaded icon in Windows 11 22H2.
		Shell_NotifyIcon(NIM_MODIFY, &m_notifyIconData);
	}

	return 0;
}

LRESULT CMainDlg::OnCustomTextifyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(lParam)
	{
	case 1:
		Exit();
		break;
	}

	return 0;
}

LRESULT CMainDlg::OnNotifyIcon(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(wParam == 1)
	{
		switch(lParam)
		{
		case WM_LBUTTONUP:
			ShowWindow(SW_SHOWNORMAL);
			::SetForegroundWindow(GetLastActivePopup());
			break;

		case WM_RBUTTONUP:
			if(IsWindowEnabled())
			{
				::SetForegroundWindow(m_hWnd);
				NotifyIconRightClickMenu();
			}
			else
			{
				ShowWindow(SW_SHOWNORMAL);
				::SetForegroundWindow(GetLastActivePopup());
			}
			break;
		}
	}

	return 0;
}

LRESULT CMainDlg::OnBringToFront(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ShowWindow(SW_SHOWNORMAL);
	::SetForegroundWindow(GetLastActivePopup());
	return 0;
}

LRESULT CMainDlg::OnUpdateChecked(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	UpdateCheckCleanup();

	if(m_config->m_checkForUpdates)
	{
		if(lParam == ERROR_SUCCESS)
		{
			DWORD dwUpdateVersion = UpdateCheckGetVersionLong();
			if(dwUpdateVersion && dwUpdateVersion > VER_FILE_VERSION_LONG)
			{
				HWND hPopup = IsWindowEnabled() ? m_hWnd : GetLastActivePopup().m_hWnd;
				UpdateTaskDialog(hPopup, UpdateCheckGetVersion());
			}

			UpdateCheckFreeVersion();

			SetTimer(TIMER_UPDATE_CHECK, 1000 * 60 * 60 * 24, NULL); // 24h
		}
		else
		{
			SetTimer(TIMER_UPDATE_CHECK, 1000 * 60 * 60, NULL); // 1h
		}
	}

	m_checkingForUpdates = false;

	if(m_closeWhenUpdateCheckDone)
	{
		::PostQuitMessage(0);
	}

	return 0;
}

LRESULT CMainDlg::OnExit(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Exit();
	return 0;
}

void CMainDlg::ReloadMainIcon()
{
	UINT dpi = GetDpiForWindowWithFallback(m_hWnd);

	HICON mainIcon = nullptr;
	LoadIconWithScaleDown(
		ModuleHelper::GetResourceInstance(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		GetSystemMetricsForDpiWithFallback(SM_CXICON, dpi),
		GetSystemMetricsForDpiWithFallback(SM_CYICON, dpi),
		&mainIcon);
	CIcon prevMainIcon = SetIcon(mainIcon, TRUE);

	CIcon prevDlgMainIcon = CStatic(GetDlgItem(IDC_MAIN_ICON)).SetIcon(CopyIcon(mainIcon));

	HICON mainIconSmall = nullptr;
	LoadIconWithScaleDown(
		ModuleHelper::GetResourceInstance(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		GetSystemMetricsForDpiWithFallback(SM_CXSMICON, dpi),
		GetSystemMetricsForDpiWithFallback(SM_CYSMICON, dpi),
		&mainIconSmall);
	CIcon prevMainIconSmall = SetIcon(mainIconSmall, FALSE);
}

void CMainDlg::ApplyUiLanguage()
{
	LANGID uiLanguage = m_config->m_uiLanguage;

	if(!uiLanguage)
	{
		CRegKey regKey;

		DWORD dwError = regKey.Open(HKEY_CURRENT_USER, L"Software\\TextifyOCR", KEY_QUERY_VALUE);
		if(dwError == ERROR_SUCCESS)
		{
			DWORD dwLanguage;
			dwError = regKey.QueryDWORDValue(L"language", dwLanguage);
			if(dwError == ERROR_SUCCESS)
			{
				uiLanguage = static_cast<LANGID>(dwLanguage);
			}
		}
	}

	if(uiLanguage)
	{
		::SetThreadUILanguage(uiLanguage);
	}

	CString str;

	str.LoadString(IDS_MAINDLG_TITLE);
	SetWindowText(str);

	str.LoadString(IDS_MAINDLG_HOMEPAGE);

	CString headerStr;
	headerStr.LoadString(IDS_MAINDLG_HEADER);
	headerStr.Replace(L"%s", VER_FILE_VERSION_WSTR);
	headerStr += L"\n<a href=\"https://ramensoftware.com/textify\">" + str + L"</a>";
	SetDlgItemText(IDC_MAIN_SYSLINK, headerStr);

	str.LoadString(IDS_MAINDLG_MOUSE_SHORTCUT);
	SetDlgItemText(IDC_MOUSE_SHORTCUT, str);

	str.LoadString(IDS_MAINDLG_CTRL);
	SetDlgItemText(IDC_CHECK_CTRL, str);

	str.LoadString(IDS_MAINDLG_ALT);
	SetDlgItemText(IDC_CHECK_ALT, str);

	str.LoadString(IDS_MAINDLG_SHIFT);
	SetDlgItemText(IDC_CHECK_SHIFT, str);

	CComboBox keysComboWnd(GetDlgItem(IDC_COMBO_KEYS));
	int keysComboCurSel = keysComboWnd.GetCurSel();
	keysComboWnd.ResetContent();
	str.LoadString(IDS_MAINDLG_MOUSE_LEFT);
	keysComboWnd.AddString(str);
	str.LoadString(IDS_MAINDLG_MOUSE_RIGHT);
	keysComboWnd.AddString(str);
	str.LoadString(IDS_MAINDLG_MOUSE_MIDDLE);
	keysComboWnd.AddString(str);
	keysComboWnd.SetCurSel(keysComboCurSel);

	str.LoadString(IDS_MAINDLG_APPLY);
	SetDlgItemText(IDOK, str);

	str.LoadString(IDS_MAINDLG_ADVANCED);
	SetDlgItemText(IDC_ADVANCED, str);

	str.LoadString(IDS_MAINDLG_MORE_SETTINGS);
	SetDlgItemText(IDC_SHOW_INI, str);

	str.LoadString(IDS_MAINDLG_EXIT);
	SetDlgItemText(IDC_EXIT, str);

	str.LoadString(IDS_MAINDLG_OCR);
	SetDlgItemText(IDC_OCR, str);

	// Populate the OCR engine combo box (below the OCR button). Called here
	// so the default entry is translated along with the rest of the UI.
	InitOcrEngineCombo();
}

void CMainDlg::ApplyMouseAndKeyboardHotKeys()
{
	if(m_config->m_mouseHotKey.key != 0)
	{
		_ATLTRY
		{
			const HotKey & mouseHotKey = m_config->m_mouseHotKey;
			m_mouseGlobalHook.emplace(m_hWnd, UWM_MOUSEHOOKCLICKED,
				mouseHotKey.key, mouseHotKey.ctrl, mouseHotKey.alt, mouseHotKey.shift,
				m_config->m_excludedPrograms);
		}
		_ATLCATCH(e)
		{
			CString str = AtlGetErrorDescription(e);
			MessageBox(
				L"The following error has occurred during the initialization of TextifyOCR:\n" + str,
				L"TextifyOCR mouse hotkey initialization error", MB_ICONERROR);
		}
	}
	else
	{
		m_mouseGlobalHook.reset();
	}

	if(m_config->m_keybdHotKey.key != 0 && !m_registeredHotKey)
	{
		m_registeredHotKey = RegisterConfiguredKeybdHotKey(m_config->m_keybdHotKey);
		if(!m_registeredHotKey)
		{
			CString str = AtlGetErrorDescription(HRESULT_FROM_WIN32(GetLastError()));
			MessageBox(
				L"The following error has occurred during the initialization of TextifyOCR:\n" + str,
				L"TextifyOCR keyboard hotkey initialization error", MB_ICONERROR);
		}
	}
	else 	if(m_config->m_keybdHotKey.key == 0 && m_registeredHotKey)
	{
		::UnregisterHotKey(m_hWnd, 1);
		m_registeredHotKey = false;
	}

	// Register OCR hotkey (ID 2)
	if(m_config->m_ocrHotKey.key != 0 && !m_registeredOcrHotKey)
	{
		UINT ocrModifiers = MOD_NOREPEAT;
		if(m_config->m_ocrHotKey.ctrl)
			ocrModifiers |= MOD_CONTROL;
		if(m_config->m_ocrHotKey.alt)
			ocrModifiers |= MOD_ALT;
		if(m_config->m_ocrHotKey.shift)
			ocrModifiers |= MOD_SHIFT;

		m_registeredOcrHotKey = ::RegisterHotKey(m_hWnd, 2, ocrModifiers, m_config->m_ocrHotKey.key) != FALSE;
	}
	else if(m_config->m_ocrHotKey.key == 0 && m_registeredOcrHotKey)
	{
		::UnregisterHotKey(m_hWnd, 2);
		m_registeredOcrHotKey = false;
	}
}

void CMainDlg::UninitMouseAndKeyboardHotKeys()
{
	if(m_registeredHotKey)
	{
		::UnregisterHotKey(m_hWnd, 1);
		m_registeredHotKey = false;
	}

	if(m_registeredOcrHotKey)
	{
		::UnregisterHotKey(m_hWnd, 2);
		m_registeredOcrHotKey = false;
	}

	m_mouseGlobalHook.reset();
}

bool CMainDlg::RegisterConfiguredKeybdHotKey(const HotKey& keybdHotKey)
{
	UINT hotKeyModifiers = MOD_NOREPEAT;

	if(keybdHotKey.ctrl)
		hotKeyModifiers |= MOD_CONTROL;

	if(keybdHotKey.alt)
		hotKeyModifiers |= MOD_ALT;

	if(keybdHotKey.shift)
		hotKeyModifiers |= MOD_SHIFT;

	return ::RegisterHotKey(m_hWnd, 1, hotKeyModifiers, keybdHotKey.key) != FALSE;
}

void CMainDlg::ConfigToGui()
{
	const HotKey& mouseHotKey = m_config->m_mouseHotKey;

	CButton(GetDlgItem(IDC_CHECK_CTRL)).SetCheck(mouseHotKey.ctrl ? BST_CHECKED : BST_UNCHECKED);
	CButton(GetDlgItem(IDC_CHECK_ALT)).SetCheck(mouseHotKey.alt ? BST_CHECKED : BST_UNCHECKED);
	CButton(GetDlgItem(IDC_CHECK_SHIFT)).SetCheck(mouseHotKey.shift ? BST_CHECKED : BST_UNCHECKED);

	auto keysComboWnd = CComboBox(GetDlgItem(IDC_COMBO_KEYS));
	switch(mouseHotKey.key)
	{
	case VK_LBUTTON:
		keysComboWnd.SetCurSel(0);
		break;

	case VK_RBUTTON:
		keysComboWnd.SetCurSel(1);
		break;

	case VK_MBUTTON:
		keysComboWnd.SetCurSel(2);
		break;

	default:
		keysComboWnd.SetCurSel(-1);
		break;
	}

	CButton(GetDlgItem(IDOK)).EnableWindow(FALSE);
}

void CMainDlg::InitNotifyIconData()
{
	m_trayIcon = LoadTrayIcon();

	m_notifyIconData.cbSize = NOTIFYICONDATA_V1_SIZE;
	m_notifyIconData.hWnd = m_hWnd;
	m_notifyIconData.uID = 1;
	m_notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_notifyIconData.uCallbackMessage = UWM_NOTIFYICON;
	m_notifyIconData.hIcon = m_trayIcon;

	CString sWindowText;
	GetWindowText(sWindowText);
	wcscpy_s(m_notifyIconData.szTip, sWindowText);
}

HICON CMainDlg::LoadTrayIcon()
{
	HWND hTaskbarWnd = FindWindow(L"Shell_TrayWnd", NULL);
	UINT dpi = GetDpiForWindowWithFallback(hTaskbarWnd ? hTaskbarWnd : m_hWnd);

	HICON trayIcon = nullptr;
	LoadIconWithScaleDown(
		ModuleHelper::GetResourceInstance(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		GetSystemMetricsForDpiWithFallback(SM_CXSMICON, dpi),
		GetSystemMetricsForDpiWithFallback(SM_CYSMICON, dpi),
		&trayIcon);
	return trayIcon;
}

void CMainDlg::NotifyIconRightClickMenu()
{
	CMenu menu;
	menu.CreatePopupMenu();

	CString str;

	str.LoadString(IDS_TRAY_TEXTIFY);
	menu.AppendMenu(MF_STRING, RCMENU_SHOW, str);

	menu.AppendMenu(MF_SEPARATOR);

	str.LoadString(IDS_TRAY_EXIT);
	menu.AppendMenu(MF_STRING, RCMENU_EXIT, str);

	CPoint point;
	GetCursorPos(&point);
	int nCmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, m_hWnd);
	switch(nCmd)
	{
	case RCMENU_SHOW:
		ShowWindow(SW_SHOWNORMAL);
		::SetForegroundWindow(GetLastActivePopup());
		break;

	case RCMENU_EXIT:
		Exit();
		break;
	}
}

void CMainDlg::Exit()
{
	if(m_checkingForUpdates)
	{
		UpdateCheckAbort();
		m_closeWhenUpdateCheckDone = true;
	}
	else
	{
		::PostQuitMessage(0);
	}
}

// ==================== OCR Feature ====================

void CMainDlg::OnOcrButton(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	StartOcrCapture();
}

void CMainDlg::InitOcrEngineCombo()
{
	CComboBox combo(GetDlgItem(IDC_COMBO_OCR_ENGINE));

	combo.ResetContent();
	m_ocrEngineDirs.clear();

	// Entry 0: the built-in RapidOCR engine
	CString str;
	str.LoadString(IDS_MAINDLG_OCR_ENGINE_DEFAULT);
	combo.AddString(str);
	m_ocrEngineDirs.emplace_back();

	// Scan the plugins directory for Umi-OCR style OCR plugins (a folder
	// with __init__.py exposing PluginInfo). Look in two places: alongside
	// the exe (installed layout: <app>\TextifyOCR.exe + <app>\plugins\)
	// and one level up (development layout: build_x64\TextifyOCR.exe +
	// parent\plugins\).
	CPath modulePath;
	GetModuleFileName(NULL, modulePath.m_strPath.GetBuffer(MAX_PATH), MAX_PATH);
	modulePath.m_strPath.ReleaseBuffer();
	modulePath.RemoveFileSpec();

	std::vector<CString> scanned;
	auto tryScan = [&](const CString& dir)
	{
		if(std::find(scanned.begin(), scanned.end(), dir) != scanned.end())
			return;
		scanned.push_back(dir);

		WIN32_FIND_DATA findData = {};
		HANDLE hFind = FindFirstFile(dir + L"\\*", &findData);
		if(hFind == INVALID_HANDLE_VALUE)
			return;

		do
		{
			if(!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;

			if(wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
				continue;

			CString initPath = dir + L"\\" + findData.cFileName + L"\\__init__.py";
			DWORD attr = GetFileAttributes(initPath);
			if(attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
				continue;

			// De-duplicate against plugins already loaded from earlier
			// locations.
			bool already = false;
			for(const auto& existing : m_ocrEngineDirs)
			{
				if(existing.CompareNoCase(findData.cFileName) == 0)
				{
					already = true;
					break;
				}
			}
			if(already)
				continue;

			m_ocrEngineDirs.push_back(CString(findData.cFileName));
			combo.AddString(CString(findData.cFileName));
		} while(FindNextFile(hFind, &findData));

		FindClose(hFind);
	};

	tryScan(modulePath.m_strPath + L"\\plugins");
	CPath parentPath = modulePath;
	parentPath.RemoveFileSpec();
	if(_wcsicmp(parentPath.m_strPath, modulePath.m_strPath) != 0)
	{
		tryScan(parentPath.m_strPath + L"\\plugins");
	}

	// Restore the selection from the config; fall back to the built-in
	// engine when the configured plugin no longer exists.
	int select = 0;
	if(m_config && !m_config->m_ocrEngine.IsEmpty())
	{
		for(size_t i = 0; i < m_ocrEngineDirs.size(); i++)
		{
			if(m_ocrEngineDirs[i].CompareNoCase(m_config->m_ocrEngine) == 0)
			{
				select = static_cast<int>(i);
				break;
			}
		}
	}

	combo.SetCurSel(select);
}

void CMainDlg::OnOcrEngineChanged(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(!m_config)
		return;

	CComboBox combo(GetDlgItem(IDC_COMBO_OCR_ENGINE));
	int sel = combo.GetCurSel();
	if(sel < 0 || sel >= static_cast<int>(m_ocrEngineDirs.size()))
		return;

	m_config->m_ocrEngine = m_ocrEngineDirs[sel];
	m_config->SaveToIniFile();
}

void CMainDlg::StartOcrCapture()
{
	if(m_ocrCapture)
		return; // Already capturing

	m_ocrCapture = std::make_unique<COcrCapture>(m_hWnd);
	m_ocrCapture->Create();
}

LRESULT CMainDlg::OnOcrCaptureComplete(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// wParam: 0 = success, 1 = cancelled
	// lParam: pointer to CString (image path) when success

	m_ocrCapture.reset();

	if(wParam != 0)
	{
		// Cancelled
		return 0;
	}

	CString* pImagePath = reinterpret_cast<CString*>(lParam);
	if(!pImagePath)
		return 0;

	CString imagePath = *pImagePath;
	delete pImagePath;

	if(imagePath.IsEmpty())
		return 0;

	// Run OCR recognition
	CString ocrText = RunOcrRecognition(imagePath);

	// Clean up temp file
	::DeleteFile(imagePath);

	if(ocrText.IsEmpty())
	{
		CString title;
		title.LoadString(IDS_ERROR);

		MessageBox(L"OCR did not recognize any text in the selected region.",
			title, MB_ICONINFORMATION);
		return 0;
	}

	// Get cursor position for placing the result window
	CPoint pt;
	GetCursorPos(&pt);

	ShowOcrResult(ocrText, pt);

	return 0;
}

CString CMainDlg::RunOcrRecognition(const CString& imagePath)
{
	// Get the module directory to find ocr_helper.py and the Python executable
	CPath modulePath;
	GetModuleFileName(NULL, modulePath.m_strPath.GetBuffer(MAX_PATH), MAX_PATH);
	modulePath.m_strPath.ReleaseBuffer();
	modulePath.RemoveFileSpec();

	// Path to ocr_helper.py
	CString scriptPath = modulePath.m_strPath + L"\\ocr_helper.py";

	// Prefer the self-contained ocr_helper.exe (PyInstaller build, no Python
	// needed on the machine). Fall back to ocr_helper.py + python.exe.
	CString helperExePath = modulePath.m_strPath + L"\\ocr_helper\\ocr_helper.exe";
	DWORD helperAttr = GetFileAttributes(helperExePath);

	CString pythonPath;
	bool useDirectExe = false;
	if(!m_config->m_ocrPythonPath.IsEmpty())
	{
		// Configured path: if it points to an .exe, run it directly
		// (works for the packaged ocr_helper.exe); otherwise treat it
		// as the Python interpreter.
		pythonPath = m_config->m_ocrPythonPath;
		if(pythonPath.GetLength() >= 4 && pythonPath.Right(4).CompareNoCase(L".exe") == 0)
			useDirectExe = true;
	}
	else if(helperAttr != INVALID_FILE_ATTRIBUTES && !(helperAttr & FILE_ATTRIBUTE_DIRECTORY))
	{
		// Auto-detect: bundled ocr_helper.exe next to Textify.exe
		pythonPath = helperExePath;
		useDirectExe = true;
	}
	else
	{
		// Auto-detect: check common Python locations
		CString candidates[] = {
			L"C:\\Users\\Administrator\\.workbuddy\\binaries\\python\\envs\\default\\Scripts\\python.exe",
			L"python.exe",
			L"python3.exe"
		};
		for(int i = 0; i < 3; i++)
		{
			DWORD attr = GetFileAttributes(candidates[i]);
			if(attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
			{
				pythonPath = candidates[i];
				break;
			}
			// For "python.exe" (no path), GetFileAttributes might fail but it could still be in PATH
			if(i == 1)
			{
				pythonPath = candidates[i];
				break;
			}
		}
	}

	if(pythonPath.IsEmpty())
		pythonPath = L"python.exe";

	// Selected OCR engine: "" = built-in RapidOCR, otherwise a plugin folder
	// name under plugins\. Fall back to the built-in engine if the plugin
	// has been removed since it was selected.
	CString engineName;
	if(m_config && !m_config->m_ocrEngine.IsEmpty())
	{
		auto hasPlugin = [&](const CString& base) -> bool
		{
			CString initPath = base + L"\\plugins\\" + m_config->m_ocrEngine + L"\\__init__.py";
			DWORD attr = GetFileAttributes(initPath);
			return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
		};

		if(hasPlugin(modulePath.m_strPath))
		{
			engineName = m_config->m_ocrEngine;
		}
		else
		{
			CPath parentPath = modulePath;
			parentPath.RemoveFileSpec();
			if(_wcsicmp(parentPath.m_strPath, modulePath.m_strPath) != 0 && hasPlugin(parentPath.m_strPath))
			{
				engineName = m_config->m_ocrEngine;
			}
		}
	}

	// Build the command line.
	// Direct exe mode: "ocr_helper.exe" "image_path" "engine"
	// (the helper already forces UTF-8 on stdout).
	// Python mode: python.exe -X utf8 script.py image_path engine — -X utf8
	// forces Python to use UTF-8 for stdout even when piped (otherwise
	// Python uses the ANSI code page, e.g. GBK on Chinese Windows, which
	// garbles the Chinese OCR text on our UTF-8 side).
	CString commandLine;
	if(useDirectExe)
	{
		commandLine.Format(L"\"%s\" \"%s\" \"%s\"",
			pythonPath.GetString(), imagePath.GetString(), engineName.GetString());
	}
	else
	{
		commandLine.Format(L"\"%s\" -X utf8 \"%s\" \"%s\" \"%s\"",
			pythonPath.GetString(), scriptPath.GetString(), imagePath.GetString(), engineName.GetString());
	}

	// Create pipes for stdout
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	HANDLE hStdoutRead = NULL;
	HANDLE hStdoutWrite = NULL;

	if(!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0))
		return L"";

	// Ensure the read handle is not inherited
	SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFO si = {};
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hStdoutWrite;
	si.hStdError = hStdoutWrite;
	si.hStdInput = NULL;

	PROCESS_INFORMATION pi = {};

	// Create the process
	BOOL success = CreateProcess(
		NULL,
		commandLine.GetBuffer(commandLine.GetLength() + 1),
		NULL, NULL, TRUE,
		CREATE_NO_WINDOW,
		NULL,
		modulePath.m_strPath.GetBuffer(MAX_PATH),
		&si, &pi);

	commandLine.ReleaseBuffer();
	modulePath.m_strPath.ReleaseBuffer();

	CloseHandle(hStdoutWrite);

	if(!success)
	{
		CloseHandle(hStdoutRead);
		return L"";
	}

	// Read stdout — accumulate raw bytes first, decode UTF-8 once at the
	// end. Decoding chunk-by-chunk could split a multi-byte UTF-8 sequence
	// across two reads and corrupt it.
	CString result;
	std::string raw;
	char buffer[4096];
	DWORD bytesRead;

	while(ReadFile(hStdoutRead, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0)
	{
		raw.append(buffer, bytesRead);
	}

	if(!raw.empty())
	{
		int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), NULL, 0);
		if(wlen > 0)
		{
			std::vector<WCHAR> wbuf(wlen);
			MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), wbuf.data(), wlen);
			result.SetString(wbuf.data(), wlen);
		}
	}

	// Wait for process to exit
	WaitForSingleObject(pi.hProcess, 30000); // 30 second timeout

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(hStdoutRead);

	// Trim trailing whitespace/newlines
	result.TrimRight(L"\r\n ");

	return result;
}

void CMainDlg::ShowOcrResult(const CString& ocrText, CPoint ptScreen)
{
	CTextDlg dlgText(*m_config, ocrText, ptScreen);
	dlgText.DoModal(NULL, 0);
}
