/**
 * RP_MFDForceOnComponent
 *
 * POC-only: bypasses the UserAction wiring path by periodically checking
 * an AG0 MFD slot's power state and calling TogglePowerAction() if it's
 * still off. Idempotent — once IsMFDOn() returns true, ticks become no-ops.
 *
 * Attach to the same entity that owns AG0_MFDManagerComponent (vehicle root).
 * Remove once the proper UserAction is wired up.
 */

[ComponentEditorProps(category: "RP/Surveillance", description: "Brute-force MFD power-on for POC. Ticks and toggles slot N ON if it's off. Attach alongside AG0_MFDManagerComponent.")]
class RP_MFDForceOnComponentClass : ScriptComponentClass
{
}

class RP_MFDForceOnComponent : ScriptComponent
{
	[Attribute("0", desc: "MFD slot index to force on.")]
	protected int m_iSlotIndex;

	[Attribute("1000", desc: "Tick interval in milliseconds.")]
	protected int m_iTickMs;

	[Attribute("2000", desc: "Initial delay before the first tick. Gives the manager time to finish OnPostInit and the slot time to register.")]
	protected int m_iInitialDelayMs;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode())
			return;
		GetGame().GetCallqueue().CallLater(StartLoop, m_iInitialDelayMs, false);
	}

	protected void StartLoop()
	{
		GetGame().GetCallqueue().CallLater(Tick, m_iTickMs, true);
	}

	protected void Tick()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;
		AG0_MFDManagerComponent mgr = AG0_MFDManagerComponent.Cast(owner.FindComponent(AG0_MFDManagerComponent));
		if (!mgr)
		{
			Print("[RP_MFDForceOn] No AG0_MFDManagerComponent on owner.", LogLevel.WARNING);
			return;
		}
		if (mgr.IsMFDOn(m_iSlotIndex))
			return;
		if (!Replication.IsServer())
			return;
		Print(string.Format("[RP_MFDForceOn] Slot %1 is OFF — calling TogglePowerAction.", m_iSlotIndex), LogLevel.NORMAL);
		mgr.TogglePowerAction(m_iSlotIndex);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(StartLoop);
		super.OnDelete(owner);
	}
}
