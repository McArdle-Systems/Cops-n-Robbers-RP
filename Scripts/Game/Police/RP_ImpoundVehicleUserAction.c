/**
 * RP_ImpoundVehicleUserAction
 *
 * UserActionContext action that hands the action's owner vehicle to
 * RP_ImpoundManagerComponent for teleport into the impound lot. Gated
 * on:
 *   1. The user's affiliated faction key matching m_sRequiredFactionKey
 *      (default "PD").
 *   2. The user currently holding an item that carries
 *      RP_ImpoundToolComponent — i.e. the Impound Sheet gadget is in
 *      hand. This is the "the action only appears when the cop has
 *      the tool out" pattern that stock Reforger uses for repair /
 *      refuel / bandage actions.
 *
 * Add this action to the BAMC of every vehicle prefab you want
 * impoundable. The light-touch path for "all cars" is to edit the
 * project-local base vehicle prefab (or the vanilla Vehicle.et via
 * a workbench mod-override) so every inheriting vehicle picks it
 * up automatically.
 */

class RP_ImpoundVehicleUserAction : ScriptedUserAction
{
	[Attribute(defvalue: "Police", desc: "Faction key required to see / perform the action. Walks the user entity's hierarchy for FactionAffiliationComponent. Leave empty to disable the faction gate.")]
	protected string m_sRequiredFactionKey;

	[Attribute(defvalue: "1", desc: "Require the user to be holding an item with RP_ImpoundToolComponent. Turn off if you want the action visible regardless of held item (e.g. for testing).")]
	protected bool m_bRequireHeldTool;

	override bool CanBeShownScript(IEntity user)
	{
		return IsAllowedFaction(user) && HasRequiredHeldTool(user);
	}

	override bool CanBePerformedScript(IEntity user)
	{
		if (!IsAllowedFaction(user))
		{
			SetCannotPerformReason("Police only.");
			return false;
		}
		if (!HasRequiredHeldTool(user))
		{
			SetCannotPerformReason("Hold the impound sheet.");
			return false;
		}
		RP_ImpoundManagerComponent mgr = RP_ImpoundManagerComponent.GetInstance();
		if (!mgr || !mgr.HasAnySlots())
		{
			SetCannotPerformReason("No impound lot configured.");
			return false;
		}
		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		// PerformAction runs on the client of whichever player most recently
		// drove the vehicle (it's owner is the vehicle, and vehicles carry a
		// controlling-player concept). Calling RP_ImpoundManagerComponent.Impound
		// directly here would hit its IsServer() gate and silently drop on dedi
		// when the cop isn't currently the vehicle's controller. Route through
		// the player relay (on the cop character, always client-owned) so the
		// server-side handler actually receives the request.
		RP_PlayerRpcRelayComponent relay = RP_PlayerRpcRelayComponent.Cast(pUserEntity.FindComponent(RP_PlayerRpcRelayComponent));
		if (!relay)
		{
			Print(string.Format("[RP_Impound] PerformAction: user %1 has no RP_PlayerRpcRelayComponent — add it to the cop character prefab.", pUserEntity), LogLevel.WARNING);
			return;
		}
		relay.RequestImpound(pOwnerEntity);
	}

	protected bool IsAllowedFaction(IEntity user)
	{
		if (m_sRequiredFactionKey.IsEmpty())
			return true;
		return GetUserFactionKey(user) == m_sRequiredFactionKey;
	}

	// Returns true if the user is currently holding (in-hand) an item
	// carrying RP_ImpoundToolComponent. Server-side this also resolves
	// for remote players because SCR_GadgetManagerComponent replicates
	// the held-gadget state.
	protected bool HasRequiredHeldTool(IEntity user)
	{
		if (!m_bRequireHeldTool)
			return true;
		SCR_GadgetManagerComponent gm = SCR_GadgetManagerComponent.GetGadgetManager(user);
		if (!gm)
			return false;
		IEntity held = gm.GetHeldGadget();
		if (!held)
			return false;
		return held.FindComponent(RP_ImpoundToolComponent) != null;
	}

	protected string GetUserFactionKey(IEntity user)
	{
		IEntity current = user;
		while (current)
		{
			FactionAffiliationComponent faff = FactionAffiliationComponent.Cast(current.FindComponent(FactionAffiliationComponent));
			if (faff)
				return faff.GetAffiliatedFactionKey();
			current = current.GetParent();
		}
		return "";
	}
}
