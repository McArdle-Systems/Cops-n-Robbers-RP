/**
 * RP_RadarUserActionBase
 *
 * Shared base for the speed radar's UserActions (Distance +/-, Target
 * Speed +/-, Power, Plate Reader).
 *
 * Base class: SCR_ScriptedUserAction (NOT plain ScriptedUserAction, and NOT
 * SCR_VehicleActionBase). SCR_ScriptedUserAction adds m_eShownInVehicleState
 * - the enum the interaction handler reads to decide whether an action is
 * offered to a SEATED occupant. A plain ScriptedUserAction has no such field,
 * so the handler defaults it to hidden in-vehicle no matter what
 * CanBeShownScript returns (that's why the on-foot-only behaviour we kept
 * hitting). SCR_VehicleActionBase would surface seated too, but it is built
 * for stateful on/off vehicle toggles: it overrides GetActionNameScript to
 * name the action from an on/off state and ties CanBePerformed/PerformAction
 * to a CompartmentControllerComponent - both wrong for our relay-routed,
 * non-toggle increment knobs.
 *
 * Hosting: these actions live on the COP CAR's own ActionsManagerComponent
 * (Vehicle_Base.et), not the slotted radar prop - the seated action collector
 * only reads the vehicle's own manager. The three contexts are anchored to
 * the "v_body" pivot so the points ride the radar's physical buttons.
 *
 * Original on-button context offsets (v_body space), before the diagnostic
 * "float them clear of the geometry" move - restore these once seated
 * visibility is confirmed:
 *   1. DistanceKnob    -0.1364 0.8799 1.0693
 *   2. TargetSpeedKnob -0.0978 0.8799 1.0589
 *   3. Toggles         -0.0591 0.8799 1.0486
 * (Currently floated to Y 1.5799 to rule out geometry occlusion.)
 *
 * Helpers:
 *   FindRadarVehicle(start) - walks up from the action owner (the cop car)
 *       to the Vehicle that carries the RP_SpeedRadarLogicComponent. That
 *       vehicle is the relay RPC handle; a null result also means "this car
 *       has no radar", hiding the actions on every non-radar vehicle that
 *       inherits Vehicle_Base.
 *   GetRelay() - the local player's RP_PlayerRpcRelayComponent.
 *
 * Why route through the relay: ScriptedUserAction.PerformAction runs on the
 * interacting client, not the server, and radar settings are server-
 * authoritative (RP_SpeedRadarLogicComponent). Bridging through
 * RP_PlayerRpcRelayComponent (on the client-owned player character) is the
 * same client->server pattern the impound and radar-power paths use.
 *
 * Assign one of the concrete subclasses, not this base. Action display names
 * come from each action's UIInfo configured in Workbench.
 */
class RP_RadarUserActionBase : SCR_ScriptedUserAction
{
	[Attribute(defvalue: "Police", desc: "Faction key allowed to see/use these radar actions. Empty = no restriction.")]
	protected string m_sRequiredFactionKey;

	// Force the in-vehicle visibility state. m_eShownInVehicleState (from
	// SCR_ScriptedUserAction) defaults to IGNORE = hidden to a seated
	// occupant; the interaction handler reads it directly. IN_VEHICLE_ANY
	// shows to any seated occupant (swap to IN_VEHICLE_PILOT for driver
	// only). Set here rather than in the prefab to avoid the enum-in-.et
	// reset pitfall.
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		m_eShownInVehicleState = EUserActionInVehicleState.IN_VEHICLE_ANY;
	}

	// Visibility gate for the local interacting player.
	//
	// CRITICAL: chain super FIRST. SCR_ScriptedUserAction.CanBeShownScript is
	// where m_eShownInVehicleState is actually consulted to decide seated
	// visibility - if we replace it without calling super, that seated check
	// never runs and the action is hidden to any occupant no matter what we
	// set the enum to (this was the long-standing "shows on foot, never
	// seated" bug). Then layer our own guards:
	//   1. Hide on any vehicle that carries no radar logic.
	//   2. Faction - hide the radar controls from non-Police (robbers).
	override bool CanBeShownScript(IEntity user)
	{
		if (!super.CanBeShownScript(user))
			return false;

		if (!FindRadarVehicle(GetOwner()))
			return false;

		return PassesFactionCheck(user);
	}

	// Mirrors the faction gate used by RP_ImpoundVehicleUserAction: walk
	// the user hierarchy for the affiliation component and compare keys.
	protected bool PassesFactionCheck(IEntity user)
	{
		if (m_sRequiredFactionKey.IsEmpty())
			return true;

		IEntity current = user;
		while (current)
		{
			FactionAffiliationComponent faff = FactionAffiliationComponent.Cast(current.FindComponent(FactionAffiliationComponent));
			if (faff)
				return faff.GetAffiliatedFactionKey() == m_sRequiredFactionKey;
			current = current.GetParent();
		}
		return false;
	}

	// Walk up to the Vehicle that owns the radar logic component.
	protected IEntity FindRadarVehicle(IEntity start)
	{
		IEntity current = start;
		while (current)
		{
			Vehicle v = Vehicle.Cast(current);
			if (v && v.FindComponent(RP_SpeedRadarLogicComponent))
				return v;
			current = current.GetParent();
		}
		return null;
	}

	protected RP_PlayerRpcRelayComponent GetRelay()
	{
		return RP_PlayerRpcRelayComponent.GetLocal();
	}
}
