/**
 * RP_DispatchPopup
 *
 * Cursor-active popup menu for the dispatch system. Derives from
 * ChimeraMenuBase; the menu manager handles cursor + input focus
 * automatically (same pattern as the inventory menu).
 *
 * Registered via:
 *   - modded enum entry in RP_ChimeraMenuPreset.c
 *   - MenuPreset block in Configs/System/chimeraMenus.conf
 * Opened via GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.RP_DispatchPopup).
 *
 * Layout uses Bohemia's WLib_ButtonText prefab for the dispatch button —
 * click handling goes through SCR_ButtonTextComponent.m_OnClicked rather
 * than a raw ButtonWidget event handler.
 */
class RP_DispatchPopup : ChimeraMenuBase
{
	protected static string m_sDispatchTypeStatic = "HMMWV";
	protected static string m_sCloseHintStatic = "Press the dispatch action again to close";
	protected static string m_sToggleActionStatic = "";

	static void SetNextDispatchType(string typeTag) { m_sDispatchTypeStatic = typeTag; }
	static void SetNextCloseHint(string hint) { m_sCloseHintStatic = hint; }
	static void SetNextToggleAction(string actionName) { m_sToggleActionStatic = actionName; }

	protected string m_sBoundToggleAction;

	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		Widget root = GetRootWidget();
		if (!root)
			return;

		TextWidget title = TextWidget.Cast(root.FindAnyWidget("TitleLine"));
		if (title)
		{
			title.SetExactFontSize(28);
			title.SetColor(Color.FromInt(Color.WHITE));
			title.SetText("DISPATCH");
		}

		TextWidget hint = TextWidget.Cast(root.FindAnyWidget("HintLine"));
		if (hint)
		{
			hint.SetExactFontSize(14);
			hint.SetColor(new Color(0.7, 0.7, 0.7, 1));
			hint.SetText(m_sCloseHintStatic);
		}

		Widget btnWidget = root.FindAnyWidget("DispatchHMMWVButton");
		if (btnWidget)
		{
			SCR_ButtonTextComponent btnComp = SCR_ButtonTextComponent.Cast(btnWidget.FindHandler(SCR_ButtonTextComponent));
			if (btnComp)
			{
				btnComp.SetText(string.Format("Dispatch %1", m_sDispatchTypeStatic));
				btnComp.m_OnClicked.Insert(OnDispatchClicked);
			}
		}

		// Belt-and-suspenders close binding — register both common close
		// action names; whichever exists in this build will fire.
		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.AddActionListener("MenuBack", EActionTrigger.DOWN, OnMenuBackPressed);
			input.AddActionListener("PauseMenu", EActionTrigger.DOWN, OnMenuBackPressed);

			// Also listen for the user's toggle action (e.g. RP_OpenDispatch)
			// so they can press the same key to close the popup. The action's
			// gameplay contexts aren't active while the popup is open, but
			// listening here registers in DialogContext implicitly.
			if (!m_sToggleActionStatic.IsEmpty())
			{
				m_sBoundToggleAction = m_sToggleActionStatic;
				input.AddActionListener(m_sBoundToggleAction, EActionTrigger.DOWN, OnMenuBackPressed);
			}
		}
	}

	override void OnMenuClose()
	{
		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.RemoveActionListener("MenuBack", EActionTrigger.DOWN, OnMenuBackPressed);
			input.RemoveActionListener("PauseMenu", EActionTrigger.DOWN, OnMenuBackPressed);
			if (!m_sBoundToggleAction.IsEmpty())
			{
				input.RemoveActionListener(m_sBoundToggleAction, EActionTrigger.DOWN, OnMenuBackPressed);
				m_sBoundToggleAction = "";
			}
		}
		super.OnMenuClose();
	}

	void OnMenuBackPressed()
	{
		Close();
	}

	void OnDispatchClicked()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
		{
			Close();
			return;
		}
		IEntity player = pc.GetControlledEntity();
		if (!player)
		{
			Close();
			return;
		}

		// Route via the player character (client-owned) so the RPC
		// reaches the server. See RP_PlayerRpcRelayComponent.
		RP_PlayerRpcRelayComponent relay = RP_PlayerRpcRelayComponent.GetLocal();
		if (!relay)
		{
			Print("[RP_Dispatch] No RP_PlayerRpcRelayComponent on local player — dispatch dropped. (Are you playing a cop character?)", LogLevel.WARNING);
			Close();
			return;
		}

		Print(string.Format("[RP_Dispatch] Client requesting dispatch type=%1 pos=%2", m_sDispatchTypeStatic, player.GetOrigin()), LogLevel.NORMAL);
		relay.RequestDispatch(m_sDispatchTypeStatic, player.GetOrigin());
		Close();
	}
}
