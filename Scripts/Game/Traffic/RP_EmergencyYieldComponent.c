/**
 * RP_EmergencyYieldComponent — Phase 1 yield-to-emergency-vehicle.
 *
 * Server-side manager on the GameMode. Each tick walks every registered
 * RP_PoliceVehicleComponent, checks its emergency lights state, and for
 * every cop with lights on runs a front-and-back bubble scan around it
 * looking for AI-driven civilian vehicles that should pull over.
 *
 * Phase 1 SCAFFOLDING: this revision logs only — it does NOT yet splice
 * pull-over waypoints into AI groups. It exists to validate spike step 2
 * (server-side BaseLightManagerComponent.GetLightsState reads correctly
 * across remote-client toggles) and to prove the bubble + compliance
 * filter before committing to the state-machine design that step 1
 * gates. See docs/YIELD_TO_EMERGENCY_TASKS.md.
 *
 * Eligibility filter:
 *   - Target must be a Vehicle.
 *   - Target's hierarchy must contain an entity with
 *     RP_DriverComplianceComponent (i.e. the driver is one of ours, not
 *     a player or a dispatched unit not opted into this system).
 *   - Compliance component returns true from WillComplyWithPullOver.
 *
 * Bubble shape:
 *   100m forward / 50m back relative to the cop's transform forward.
 *   Front bucket gives traffic ahead time to pull off; rear bucket
 *   keeps cars stopped while the cop sits behind them.
 */

[ComponentEditorProps(category: "RP/Traffic", description: "Server-side yield-to-emergency-vehicle manager. Attach to the GameMode entity.")]
class RP_EmergencyYieldComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_EmergencyYieldComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "0.25", desc: "Tick interval in seconds. Drives the bubble scan and (later) the per-target state machine.")]
	protected float m_fTickIntervalSeconds;

	[Attribute(defvalue: "100.0", desc: "Forward bubble radius in meters. Cars ahead of the cop within this range and with the cop's lights on are pull-over candidates.")]
	protected float m_fFrontRadiusMeters;

	[Attribute(defvalue: "50.0", desc: "Rear bubble radius in meters. Cars behind the cop within this range still count — keeps a car stopped while the cop sits behind it.")]
	protected float m_fBackRadiusMeters;

	[Attribute(defvalue: "0", desc: "Per-tick verbose log of every vehicle scanned in the bubble. Off by default — the per-cop lights-on/off and the WOULD-pull-over lines are usually enough. Flip on when debugging.")]
	protected bool m_bVerboseLogging;

	protected ref array<IEntity> m_aQueryResults = {};
	protected ref map<RP_PoliceVehicleComponent, bool> m_mLastLightsOn = new map<RP_PoliceVehicleComponent, bool>();
	// Vehicles we've already logged a lookup-failure diagnostic for, so
	// the chain trace doesn't spam every tick when a non-yielding vehicle
	// (player, untagged crew) stays in the bubble.
	protected ref set<IEntity> m_sDiagnosedFailures = new set<IEntity>();

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
		{
			Print("[RP_Yield] Manager skipped — not server.", LogLevel.NORMAL);
			return;
		}
		int intervalMs = (int)(m_fTickIntervalSeconds * 1000);
		GetGame().GetCallqueue().CallLater(Tick, intervalMs, true);
		Print(string.Format("[RP_Yield] Manager started on %1, ticking every %2ms (front=%3m, back=%4m).", owner, intervalMs, m_fFrontRadiusMeters, m_fBackRadiusMeters), LogLevel.NORMAL);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Tick);
		super.OnDelete(owner);
	}

	protected int m_iEmptyRegistryWarnings;

	protected void Tick()
	{
		array<RP_PoliceVehicleComponent> emergencyVehicles = RP_PoliceVehicleComponent.GetInstances();
		if (!emergencyVehicles || emergencyVehicles.IsEmpty())
		{
			// Surface an empty registry once per ~5s so it's visible in logs
			// without being spammy. Catches "manager ticking but no police
			// vehicle registered" — i.e. RP_PoliceVehicleComponent missing
			// from prefab or registration didn't run.
			m_iEmptyRegistryWarnings++;
			if (m_iEmptyRegistryWarnings % 20 == 1)
				Print("[RP_Yield] Tick: registry is empty — no RP_PoliceVehicleComponent has registered. Check the prefab and that OnPostInit ran server-side.", LogLevel.WARNING);
			return;
		}
		m_iEmptyRegistryWarnings = 0;

		foreach (RP_PoliceVehicleComponent ev : emergencyVehicles)
		{
			if (!ev)
				continue;
			IEntity copCar = ev.GetOwner();
			if (!copCar)
				continue;
			bool lightsOn = AreEmergencyLightsOn(copCar);
			LogLightsTransition(ev, copCar, lightsOn);
			if (!lightsOn)
				continue;
			ScanBubble(copCar);
		}
	}

	// Edge-detect transitions so we get one log line per on/off rather than
	// a stream every tick. Most useful diagnostic for spike step 2 — tells
	// us whether server-side GetLightsState is reading the engine's state
	// when a remote client toggles the dashboard action.
	protected void LogLightsTransition(RP_PoliceVehicleComponent ev, IEntity copCar, bool lightsOn)
	{
		bool wasOn = false;
		if (m_mLastLightsOn.Contains(ev))
			wasOn = m_mLastLightsOn.Get(ev);
		if (wasOn == lightsOn)
			return;
		m_mLastLightsOn.Set(ev, lightsOn);
		string state;
		if (lightsOn)
			state = "ON";
		else
			state = "OFF";
		Print(string.Format("[RP_Yield] Lights %1 on %2.", state, copCar), LogLevel.NORMAL);
	}

	// Reads the cop vehicle's siren-light state directly from the engine's
	// BaseLightManagerComponent. The Suburban prefab tags siren slots with
	// LightType Dashboard, so Dashboard == "siren-on". If a future emergency
	// vehicle uses a different tag we'll need to broaden this.
	protected bool AreEmergencyLightsOn(IEntity copCar)
	{
		BaseLightManagerComponent lights = BaseLightManagerComponent.Cast(copCar.FindComponent(BaseLightManagerComponent));
		if (!lights)
			return false;
		return lights.GetLightsState(ELightType.Dashboard);
	}

	protected void ScanBubble(IEntity copCar)
	{
		vector tm[4];
		copCar.GetTransform(tm);
		vector copPos = tm[3];
		vector copForward = tm[2];

		float queryRadius = Math.Max(m_fFrontRadiusMeters, m_fBackRadiusMeters);
		m_aQueryResults.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(
			copPos, queryRadius,
			QueryCollect, null,
			EQueryEntitiesFlags.DYNAMIC);

		foreach (IEntity ent : m_aQueryResults)
		{
			if (ent == copCar)
				continue;
			if (!Vehicle.Cast(ent))
				continue;

			vector toEnt = ent.GetOrigin() - copPos;
			float dist = toEnt.Length();
			if (dist < 0.5)
				continue;

			float forwardDot = vector.Dot(toEnt, copForward);
			bool inFront = forwardDot > 0;
			float bucketRadius;
			if (inFront)
				bucketRadius = m_fFrontRadiusMeters;
			else
				bucketRadius = m_fBackRadiusMeters;
			if (dist > bucketRadius)
				continue;

			EvaluateCandidate(copCar, ent, inFront, dist);
		}
	}

	protected bool QueryCollect(IEntity entity)
	{
		m_aQueryResults.Insert(entity);
		return true;
	}

	// Phase 1: log only. Real impl will splice a pull-over Move waypoint
	// onto the driver's AI group and track per-target state.
	protected void EvaluateCandidate(IEntity copCar, IEntity vehicle, bool inFront, float dist)
	{
		bool firstFailureForThisVehicle = !m_sDiagnosedFailures.Contains(vehicle);
		string diag;
		RP_DriverComplianceComponent compliance = FindDriverCompliance(vehicle, firstFailureForThisVehicle, diag);
		if (!compliance)
		{
			if (m_bVerboseLogging)
				Print(string.Format("[RP_Yield] Skip %1 (%2m %3) — no compliance component (player or non-yielding driver).", vehicle, Math.Round(dist), BucketLabel(inFront)), LogLevel.NORMAL);
			if (firstFailureForThisVehicle)
			{
				m_sDiagnosedFailures.Insert(vehicle);
				Print(string.Format("[RP_Yield] Lookup-chain diagnosis for %1: %2", vehicle, diag), LogLevel.NORMAL);
			}
			return;
		}
		if (!compliance.WillComplyWithPullOver())
		{
			Print(string.Format("[RP_Yield] %1 (%2m %3) refuses to comply — flee logic hook will go here.", vehicle, Math.Round(dist), BucketLabel(inFront)), LogLevel.NORMAL);
			return;
		}
		Print(string.Format("[RP_Yield] WOULD pull over %1 (%2m %3) for cop %4.", vehicle, Math.Round(dist), BucketLabel(inFront), copCar), LogLevel.NORMAL);
	}

	// Resolves the compliance component on the SCR_AIGroup that owns
	// this vehicle's driver. Group-scoped because the compliance
	// decision is naturally per-group (one yield/flee answer per
	// convoy), separate from target selection which the manager handles
	// per-driver — we identify the specific driver in the bubble and
	// the group routes the pull-over waypoint to just that driver.
	//
	// Path: vehicle children → driver character (has AIControlComponent)
	// → AIAgent → parent SCR_AIGroup → component lookup. The group
	// entity itself is not a child of the vehicle, only the seated
	// character is.
	//
	// Resolves the compliance component on the SCR_AIGroup that owns
	// this vehicle's driver, by iterating the compliance registry and
	// using SCR_AIGroup.GetAgents to check membership directly.
	//
	// Why not agent.GetParentGroup()? In practice `GetParentGroup` does
	// not point at the SCR_AIGroup we expect for a character actively
	// driving a vehicle — it returns null. The registry-driven approach
	// works regardless of how that relationship is wired.
	protected RP_DriverComplianceComponent FindDriverCompliance(IEntity vehicle, bool diagnose = false, out string outDiag = "")
	{
		IEntity driver = FindDriverEntity(vehicle);
		if (!driver)
		{
			if (diagnose) outDiag = "no AIControlComponent found in vehicle hierarchy (driver may not be AI)";
			return null;
		}

		array<RP_DriverComplianceComponent> compliances = RP_DriverComplianceComponent.GetInstances();
		if (!compliances || compliances.IsEmpty())
		{
			if (diagnose) outDiag = "compliance registry is empty — no SCR_AIGroup has registered RP_DriverComplianceComponent";
			return null;
		}

		array<AIAgent> agents = {};
		string agentDump = "";
		foreach (RP_DriverComplianceComponent compliance : compliances)
		{
			if (!compliance)
				continue;
			SCR_AIGroup group = SCR_AIGroup.Cast(compliance.GetOwner());
			if (!group)
				continue;
			agents.Clear();
			group.GetAgents(agents);
			if (diagnose)
				agentDump += string.Format(" group %1 has %2 agents", group, agents.Count());
			foreach (AIAgent agent : agents)
			{
				if (!agent)
					continue;
				IEntity controlled = agent.GetControlledEntity();
				if (diagnose)
					agentDump += string.Format(" [agent %1 controls %2]", agent, controlled);
				if (controlled == driver)
					return compliance;
			}
		}

		if (diagnose)
			outDiag = string.Format("driver %1 not matched.%2", driver, agentDump);
		return null;
	}

	// Returns the first entity in the vehicle's hierarchy that carries
	// an AIControlComponent — the seated AI driver. Skips the vehicle
	// itself because AI-driven vehicles carry their own
	// AIControlComponent, which would short-circuit a naive walk before
	// reaching the seated character.
	protected IEntity FindDriverEntity(IEntity vehicle)
	{
		IEntity child = vehicle.GetChildren();
		while (child)
		{
			IEntity inner = FindDriverEntityRecursive(child);
			if (inner)
				return inner;
			child = child.GetSibling();
		}
		return null;
	}

	protected IEntity FindDriverEntityRecursive(IEntity entity)
	{
		if (entity.FindComponent(AIControlComponent))
			return entity;
		IEntity child = entity.GetChildren();
		while (child)
		{
			IEntity inner = FindDriverEntityRecursive(child);
			if (inner)
				return inner;
			child = child.GetSibling();
		}
		return null;
	}

	protected string BucketLabel(bool inFront)
	{
		if (inFront)
			return "front";
		return "back";
	}
}
