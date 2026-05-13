modded class PauseMenuUI
{
	protected Widget m_wRPAdminButton;

	override void OnMenuOpen()
	{
		super.OnMenuOpen();
		InjectAdminButton();
	}

	override void OnMenuClose()
	{
		if (m_wRPAdminButton)
		{
			m_wRPAdminButton.RemoveFromHierarchy();
			m_wRPAdminButton = null;
		}
		super.OnMenuClose();
	}

	protected void InjectAdminButton()
	{
		// Hide the entry-point button for non-admins. Cosmetic only — the
		// RPC handler re-checks server-side, so a non-admin can't bypass
		// by hand-opening the menu either.
		if (!RP_AdminUtils.IsLocalAdmin())
			return;

		if (!m_SettingsButton)
		{
			Print("[RP_Admin] Pause menu inject: m_SettingsButton null, cannot anchor.", LogLevel.WARNING);
			return;
		}

		Widget anchor = m_SettingsButton.GetRootWidget();
		if (!anchor)
			return;

		Widget parent = anchor.GetParent();
		if (!parent)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		m_wRPAdminButton = ws.CreateWidgets(
			"{75C912A1C89BE6C2}UI/layouts/WidgetLibrary/Buttons/WLib_ButtonText.layout",
			parent);
		if (!m_wRPAdminButton)
		{
			Print("[RP_Admin] CreateWidgets returned null for admin button.", LogLevel.WARNING);
			return;
		}

		m_wRPAdminButton.SetName("RP_AdminPauseButton");

		SCR_ButtonTextComponent btn = SCR_ButtonTextComponent.Cast(
			m_wRPAdminButton.FindHandler(SCR_ButtonTextComponent));
		if (btn)
		{
			btn.SetText("Admin Panel");
			btn.m_OnClicked.Insert(OnAdminButtonClicked);
		}

		Print("[RP_Admin] Injected admin button into pause menu.", LogLevel.NORMAL);
	}

	void OnAdminButtonClicked()
	{
		MenuManager mm = GetGame().GetMenuManager();
		if (!mm)
			return;
		mm.OpenMenu(ChimeraMenuPreset.RP_AdminPanel);
	}
}
