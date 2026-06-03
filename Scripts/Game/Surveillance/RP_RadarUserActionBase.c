/**
 * RP_RadarUserActionBase
 *
 * Shared base for the speed radar's vehicle UserActions (Distance +/-,
 * Target Speed +/-, Power, Plate Reader).
 *
 * Canonical SCR_VehicleActionBase usage: the engine owns discovery and
 * vehicle/compartment visibility. The only thing we layer on is a
 * police-faction gate, applied on top of the base show/perform checks.
 *
 * Positioning comes from each action's ParentContextList in the vehicle
 * prefab. Interior / pilot visibility is driven by the inherited prefab
 * attributes (m_bInteriorOnly, m_bPilotOnly) - we do NOT force them in code.
 */
class RP_RadarUserActionBase : SCR_VehicleActionBase
{
	[Attribute(defvalue: "Police", desc: "Faction key allowed to see/use these radar actions. Empty = no restriction.")]
	protected string m_sRequiredFactionKey;

	// Cached at Init so the label builders (GetActionNameScript, which has no
	// owner param) can resolve the radar logic each frame.
	protected IEntity m_OwnerEntity;

	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		m_OwnerEntity = pOwnerEntity;
	}

	// Resolves the radar logic component from the cached owner. Null until
	// Init has run or if the owner isn't under a radar-equipped vehicle.
	protected RP_SpeedRadarLogicComponent GetRadarLogic()
	{
		IEntity car = FindRadarVehicle(m_OwnerEntity);
		if (!car)
			return null;
		return RP_SpeedRadarLogicComponent.FindOnVehicle(car);
	}

	override bool CanBeShownScript(IEntity user)
	{
		if (!super.CanBeShownScript(user))
			return false;

		return PassesFactionCheck(user);
	}

	override bool CanBePerformedScript(IEntity user)
	{
		if (!super.CanBePerformedScript(user))
			return false;

		if (!PassesFactionCheck(user))
		{
			SetCannotPerformReason("Police only.");
			return false;
		}

		return true;
	}

	protected bool PassesFactionCheck(IEntity user)
	{
		if (m_sRequiredFactionKey.IsEmpty())
			return true;

		if (EntityHasFaction(user, m_sRequiredFactionKey))
			return true;

		IEntity controlled = GetLocalControlledEntity();
		if (controlled && controlled != user)
			return EntityHasFaction(controlled, m_sRequiredFactionKey);

		return false;
	}

	protected bool EntityHasFaction(IEntity user, string factionKey)
	{
		return GetEntityFactionKey(user) == factionKey;
	}

	protected string GetEntityFactionKey(IEntity user)
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

	protected IEntity GetLocalControlledEntity()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;
		return pc.GetControlledEntity();
	}

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
