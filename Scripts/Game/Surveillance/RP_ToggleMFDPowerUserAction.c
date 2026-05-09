/**
 * RP_ToggleMFDPowerUserAction
 *
 * In-cab UserAction that toggles power on an AG0 MFD slot. Slot index is
 * the registration order on the vehicle's AG0_MFDManagerComponent
 * (0 = first slot registered, 1 = second, ...).
 *
 * Place under any UserActionContext on the vehicle (e.g. dashboard,
 * light-switch panel). The action walks up from the action's owner
 * entity to find the AG0_MFDManagerComponent on the vehicle root.
 */

class RP_ToggleMFDPowerUserAction : ScriptedUserAction
{
	[Attribute("0", desc: "MFD slot index to toggle (registration order — 0 = first slot on the vehicle's AG0_MFDManagerComponent).")]
	protected int m_iSlotIndex;

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		AG0_MFDManagerComponent manager = FindManager(pOwnerEntity);
		if (!manager)
		{
			Print("[RP_ToggleMFDPower] No AG0_MFDManagerComponent found on owner or ancestors.", LogLevel.WARNING);
			return;
		}
		manager.TogglePowerAction(m_iSlotIndex);
	}

	protected AG0_MFDManagerComponent FindManager(IEntity start)
	{
		IEntity current = start;
		while (current)
		{
			AG0_MFDManagerComponent mgr = AG0_MFDManagerComponent.Cast(current.FindComponent(AG0_MFDManagerComponent));
			if (mgr)
				return mgr;
			current = current.GetParent();
		}
		return null;
	}
}
