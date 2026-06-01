/**
 * RP_RadarUserActionBase
 *
 * Shared base for the speed radar's UserActions (Distance +/-, Target
 * Speed +/-, Power, Plate Reader).
 *
 * These actions currently live on the cop vehicle's ActionsManagerComponent.
 * A slotted radar prop can instantiate actions, but the player interaction
 * collector does not query those child actions in this setup.
 */
class RP_RadarUserActionBase : SCR_VehicleActionBase
{
	[Attribute(defvalue: "Police", desc: "Faction key allowed to see/use these radar actions. Empty = no restriction.")]
	protected string m_sRequiredFactionKey;

	[Attribute(defvalue: "", desc: "Optional action context name to force as this action's active context.")]
	protected string m_sForcedContextName;

	protected string m_sLastVisibilityDebug;
	protected string m_sLastPerformDebug;

	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		m_bInteriorOnly = true;
		m_bPilotOnly = false;
		m_bIsToggle = false;
		ForceRadarContext();
		Print(string.Format("[RP_RadarActionDbg] Init %1 owner=%2 manager=%3",
			this,
			pOwnerEntity,
			pManagerComponent),
			LogLevel.NORMAL);
		Print(string.Format("[RP_RadarActionDbg] ContextIndexes %1 Toggles=%2 starter_switch=%3 DistanceKnob=%4 TargetSpeedKnob=%5",
			this,
			GetContextIndex("Toggles"),
			GetContextIndex("starter_switch"),
			GetContextIndex("DistanceKnob"),
			GetContextIndex("TargetSpeedKnob")),
			LogLevel.NORMAL);
	}

	override bool CanBeShownScript(IEntity user)
	{
		ForceRadarContext();
		bool shown = PassesFactionCheck(user);
		DebugVisibility(shown, user);
		return shown;
	}

	override bool CanBePerformedScript(IEntity user)
	{
		ForceRadarContext();
		bool canPerform = PassesFactionCheck(user);
		DebugPerform(canPerform, user);

		if (!canPerform)
			SetCannotPerformReason("Police only.");

		return canPerform;
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

	protected void DebugVisibility(bool shown, IEntity user)
	{
		string state = string.Format("%1:%2:%3", shown, user, GetOwner());
		if (state == m_sLastVisibilityDebug)
			return;
		m_sLastVisibilityDebug = state;

		Print(string.Format("[RP_RadarActionDbg] ShowCheck %1 activeContext='%2' shown=%3 owner=%4 user=%5",
			this,
			GetActiveContextDebugName(),
			shown,
			GetOwner(),
			user),
			LogLevel.NORMAL);
	}

	protected void DebugPerform(bool canPerform, IEntity user)
	{
		string state = string.Format("%1:%2:%3:%4", canPerform, user, GetOwner(), GetActiveContextDebugName());
		if (state == m_sLastPerformDebug)
			return;
		m_sLastPerformDebug = state;

		Print(string.Format("[RP_RadarActionDbg] PerformCheck %1 activeContext='%2' name='%3' canPerform=%4 user=%5",
			this,
			GetActiveContextDebugName(),
			GetActionName(),
			canPerform,
			user),
			LogLevel.NORMAL);
	}

	protected string GetActiveContextDebugName()
	{
		UserActionContext context = GetActiveContext();
		if (!context)
			return "<none>";

		return context.GetContextName();
	}

	protected string GetActiveContextDebugDescription()
	{
		UserActionContext context = GetActiveContext();
		if (!context)
			return "<none>";

		RP_RadarUserActionContext radarContext = RP_RadarUserActionContext.Cast(context);
		if (radarContext)
			return radarContext.GetRadarDebugDescription();

		return string.Format("name='%1' radius=%2 origin=%3 actions=%4",
			context.GetContextName(),
			context.GetRadius(),
			context.GetOrigin(),
			context.GetActionsCount());
	}

	protected void ForceRadarContext()
	{
		if (m_sForcedContextName.IsEmpty())
			return;

		UserActionContext activeContext = GetActiveContext();
		if (activeContext && activeContext.GetContextName() == m_sForcedContextName)
			return;

		ActionsManagerComponent manager = GetActionsManager();
		if (!manager)
			return;

		UserActionContext forcedContext = manager.GetContext(m_sForcedContextName);
		if (!forcedContext)
			return;

		SetActiveContext(forcedContext);
		Print(string.Format("[RP_RadarActionDbg] ForcedContext %1 %2",
			this,
			GetActiveContextDebugDescription()),
			LogLevel.NORMAL);
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
