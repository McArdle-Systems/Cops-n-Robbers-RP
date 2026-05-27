/**
 * RP_ImpoundLotSlotComponent
 *
 * Marks a placed entity as one parking slot in the impound lot. Each
 * slot contributes a position + rotation that impounded vehicles get
 * teleported to. Self-registers with RP_ImpoundManagerComponent on
 * spawn — mirrors RP_TrafficMarkerComponent's deferred-poll pattern
 * since the manager may not be alive yet at OnPostInit time.
 *
 * Placement guidance: drop any lightweight entity (e.g. a Reference)
 * onto the ground where you want a vehicle to sit, orient it so the
 * forward axis matches the desired vehicle facing, then attach this
 * component. The marker itself is never deleted — the manager keeps
 * a reference so impoundments after eviction can re-read the
 * transform.
 */

[ComponentEditorProps(category: "RP/Police", description: "One parking slot in the impound lot. Server reads this entity's transform when teleporting impounded vehicles.")]
class RP_ImpoundLotSlotComponentClass : ScriptComponentClass
{
}

class RP_ImpoundLotSlotComponent : ScriptComponent
{
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		// Register on every peer (not just the server). The action's
		// CanBePerformedScript runs client-side and needs HasAnySlots()
		// to be truthful — on dedi the client's slot list would otherwise
		// stay empty and the action errors with "No impound lot configured."
		// Occupancy (m_Vehicle / m_iImpoundedAtMs) stays server-only since
		// Impound() is server-gated.
		GetGame().GetCallqueue().CallLater(TryRegister, 100, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryRegister);
		super.OnDelete(owner);
	}

	protected void TryRegister()
	{
		RP_ImpoundManagerComponent mgr = RP_ImpoundManagerComponent.GetInstance();
		if (!mgr)
			return;
		GetGame().GetCallqueue().Remove(TryRegister);
		mgr.RegisterSlot(GetOwner());
	}
}
