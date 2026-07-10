
#include "menu.hpp"

s16 m_errorLblMessage;
s16 m_errorLblIcon;
s16 m_errorLblUser[4];
/* RetroAchievements pre-boot status line — one-line progress text shown
 * while the boot path blocks on the adapter (probe / hash / RA download). */
s16 m_raStatusLbl = -1;

void CMenu::_hideError(bool instant)
{
	m_btnMgr.hide(m_errorLblIcon, instant);
	m_btnMgr.hide(m_errorLblMessage, instant);
	for(u8 i = 0; i < ARRAY_SIZE(m_errorLblUser); ++i)
		if(m_errorLblUser[i] != -1)
			m_btnMgr.hide(m_errorLblUser[i], instant);
}

void CMenu::_showError(void)
{
	_setBg(m_errorBg, m_errorBg);
	m_btnMgr.show(m_errorLblMessage);
	m_btnMgr.show(m_errorLblIcon);
	for(u8 i = 0; i < ARRAY_SIZE(m_errorLblUser); ++i)
		if(m_errorLblUser[i] != -1)
			m_btnMgr.show(m_errorLblUser[i]);
}

void CMenu::_error(const wstringEx &msg)
{
	SetupInput();
	_hideAbout();
	_hideCode();
	_hideConfigMain();
	_hideConfigGCGame();
	_hideDownload();
	_hideExitTo();
	_hideGame();
	_hideMain();
	_hideWBFS();
	_hideCFTheme();
	_hideCategorySettings();
	_hideGameInfo();
	_hideConfigGame();
	_hideWaitMessage();
	_raHideStatus();
	m_btnMgr.setText(m_errorLblMessage, msg);
	_showError();

	gprintf("error msg: %s\n", msg.toUTF8().c_str());
	do
	{
		_mainLoopCommon();
	} while (!m_exit && !BTN_B_PRESSED && !BTN_A_PRESSED && !BTN_HOME_PRESSED);
	_hideError(false);
}

/* Show one line of RetroAchievements boot progress and pump a few frames so
 * it actually lands on screen — the boot path then blocks (disc reads, EXI
 * polling) and the last rendered frame persists until the next update.
 * Mirrors the m_thrdWorking render branch of _mainLoopCommon. Must NOT run
 * while the wait-message spinner thread is active (two GX writers). */
void CMenu::_raShowStatus(const wstringEx &msg)
{
	m_btnMgr.setText(m_raStatusLbl, msg);
	m_btnMgr.show(m_raStatusLbl, true);   /* instant — fully opaque before we block */
	for(int i = 0; i < 4; ++i)
	{
		m_btnMgr.tick();
		m_vid.prepare();
		m_vid.setup2DProjection(false, true);
		_updateBg();
		if(CoverFlow.getRenderTex())
			CoverFlow.RenderTex();
		m_vid.setup2DProjection();
		_drawBg();
		m_btnMgr.draw();
		m_vid.render();
	}
}

void CMenu::_raHideStatus(void)
{
	m_btnMgr.hide(m_raStatusLbl, true);
}

void CMenu::_initErrorMenu()
{
	_addUserLabels(m_errorLblUser, ARRAY_SIZE(m_errorLblUser), "ERROR");
	m_errorBg = _texture("ERROR/BG", "texture", theme.bg, false);
	m_errorLblMessage = _addLabel("ERROR/MESSAGE", theme.lblFont, L"", 112, 20, 500, 440, theme.lblFontColor, FTGX_JUSTIFY_LEFT | FTGX_ALIGN_MIDDLE);
	m_raStatusLbl = _addLabel("RASTATUS/MESSAGE", theme.lblFont, L"", 40, 380, 560, 56, theme.lblFontColor, FTGX_JUSTIFY_CENTER | FTGX_ALIGN_MIDDLE);
	_setHideAnim(m_raStatusLbl, "RASTATUS/MESSAGE", 0, 0, 0.f, 0.f);
	m_btnMgr.hide(m_raStatusLbl, true);
	TexData texIcon;
	TexHandle.fromImageFile(texIcon, fmt("%s/error.png", m_imgsDir.c_str()));
	m_errorLblIcon = _addLabel("ERROR/ICON", theme.lblFont, L"", 40, 200, 64, 64, theme.lblFontColor, 0, texIcon);
	// 
	_setHideAnim(m_errorLblMessage, "ERROR/MESSAGE", 0, 0, 0.f, 0.f);
	_setHideAnim(m_errorLblIcon, "ERROR/ICON", -50, 0, 0.f, 0.f);
	// 
	_hideError(true);
	_textError();
}

void CMenu::_textError(void)
{
}
