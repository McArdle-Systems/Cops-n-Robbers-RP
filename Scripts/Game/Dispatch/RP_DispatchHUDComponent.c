/**
 * RP_DispatchHUDComponent
 *
 * Client-side input glue for the dispatch popup. Lives on the GameMode
 * entity. Listens for a configurable Bohemia input action; on press,
 * toggles the dispatch popup (cursor-active menu) open/closed.
 *
 * Action binding follows the FobBuilder pattern: piggyback on a stock
 * action by name (no custom InputContext .conf). Default TacticalPing
 * (MMB) is well-known; PerformAction (F) collides with vehicles/items.
 *
 * The popup is opened via the canonical ChimeraMenuPreset path —
 * see RP_ChimeraMenuPreset.c (modded enum entry) and
 * Configs/System/chimeraMenus.conf (preset registration).
 */

[ComponentEditorProps(category: "RP/Dispatch", description: "Client-side popup + action listener for the dispatch system. Attach to the GameMode entity.")]
class RP_DispatchHUDComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_DispatchHUDComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "HMMWV", desc: "Type tag the popup will dispatch. Must match a RP_DispatchGroupDefinition on the manager.")]
	protected string m_sDispatchType;

	[Attribute(defvalue: "PerformAction", desc: "Bohemia input action name that toggles the dispatch popup. PerformAction=F is confirmed working. TacticalPing was unverified.")]
	protected string m_sToggleAction;

	protected ChimeraMenuBase m_OpenMenu;
	protected bool m_bActionRegistered;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		GetGame().GetCallqueue().CallLater(TryClientInit, 250, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryClientInit);
		UnregisterAction();
		ClosePopup();
		super.OnDelete(owner);
	}

	protected void TryClientInit()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;
		GetGame().GetCallqueue().Remove(TryClientInit);
		RegisterAction();
	}

	protected void RegisterAction()
	{
		if (m_bActionRegistered)
			return;
		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;
		input.AddActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleAction);
		m_bActionRegistered = true;
		Print(string.Format("[RP_Dispatch] Popup toggle bound to action '%1' for type '%2'", m_sToggleAction, m_sDispatchType), LogLevel.NORMAL);
	}

	protected void UnregisterAction()
	{
		if (!m_bActionRegistered)
			return;
		InputManager input = GetGame().GetInputManager();
		if (input)
			input.RemoveActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleAction);
		m_bActionRegistered = false;
	}

	protected void OnToggleAction()
	{
		Print(string.Format("[RP_Dispatch] OnToggleAction fired. Currently open: %1", m_OpenMenu != null), LogLevel.NORMAL);
		if (m_OpenMenu)
		{
			ClosePopup();
			return;
		}
		OpenPopup();
	}

	protected void OpenPopup()
	{
		MenuManager mm = GetGame().GetMenuManager();
		if (!mm)
		{
			Print("[RP_Dispatch] No MenuManager available; can't open popup.", LogLevel.ERROR);
			return;
		}

		RP_DispatchPopup.SetNextDispatchType(m_sDispatchType);
		RP_DispatchPopup.SetNextCloseHint(string.Format("Press [%1] again to close", m_sToggleAction));

		Print("[RP_Dispatch] OpenPopup: calling mm.OpenMenu(ChimeraMenuPreset.RP_DispatchPopup)", LogLevel.NORMAL);
		ChimeraMenuBase menu = ChimeraMenuBase.Cast(mm.OpenMenu(ChimeraMenuPreset.RP_DispatchPopup));
		if (!menu)
		{
			Print("[RP_Dispatch] OpenMenu returned null for ChimeraMenuPreset.RP_DispatchPopup. Check Configs/System/chimeraMenus.conf is registered.", LogLevel.ERROR);
			return;
		}
		Print(string.Format("[RP_Dispatch] OpenPopup succeeded, menu=%1", menu), LogLevel.NORMAL);
		m_OpenMenu = menu;
	}

	protected void ClosePopup()
	{
		if (!m_OpenMenu)
			return;
		m_OpenMenu.Close();
		m_OpenMenu = null;
	}
}
