/**
 * RP_EmergencyYieldComponent — Phase 1 yield-to-emergency-vehicle.
 *
 * Server-side manager on the GameMode. Each tick walks every registered
 * RP_PoliceVehicleComponent, checks its emergency lights state, and for
 * every cop with lights on runs a front-and-back bubble scan around it
 * looking for AI-driven civilian vehicles that should pull over.
 *
 * Yield mechanism (snapshot / strip / restore):
 *   1. Snapshot the group's existing waypoints via AIGroup.GetWaypoints.
 *   2. Strip them from the queue with RemoveWaypoint (unlinks without
 *      destroying — references in our snapshot remain valid).
 *   3. Spawn a single Move waypoint at a stub pull-over position and
 *      add it to the queue.
 *   4. Empty queue == hold: when the AI finishes the Move, there's
 *      nothing else queued, so it idles in place until release.
 *   5. On release (lights off, cop > N meters away, or cop deleted):
 *      remove the Move waypoint and re-add the snapshot in original
 *      order. Whether AIWaypointCycle resumes from its prior position
 *      is up to the engine — re-adding the same instance preserves
 *      its internal state if the cycle is stateful, otherwise it
 *      restarts from waypoint 0 (acceptable visual artifact).
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
 *
 * NOT YET implemented: road-shoulder geometry (the Move waypoint is
 * currently placed at a stub 8m-forward / 3m-right of the vehicle).
 * Step 5 in YIELD_TO_EMERGENCY_TASKS.md replaces the stub with
 * RoadNetworkManager.GetClosestRoad + GetReachableWaypointInRoad.
 */

class RP_YieldedGroupState
{
	SCR_AIGroup m_Group;
	ref array<AIWaypoint> m_aSavedWaypoints = {};
	AIWaypoint m_PullOverWaypoint;
	IEntity m_AssignedCop;
	IEntity m_StoppedVehicle;
	// World time (seconds) of the last bubble-scan hit that refreshed
	// this yield. Stale = release. See CheckReleases.
	float m_fLastRefreshTime;
	// Set true if the driver dismounted during the yield. EndYield uses
	// this to prepend a GetIn waypoint so the dismounted driver walks
	// back, boards, and resumes the cycle. Without this flag the saved
	// waypoints would be restored to a driverless vehicle and the AI
	// would just stand there.
	bool m_bDriverBailed;
}

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

	[Attribute(defvalue: "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", desc: "Move waypoint prefab spawned for the pull-over.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sMoveWaypointPrefab;

	[Attribute(defvalue: "{712F4795CF8B91C7}Prefabs/AI/Waypoints/AIWaypoint_GetIn.et", desc: "GetIn waypoint prefab — prepended on release if the driver bailed during the yield, so they walk back to the vehicle and board before resuming the cycle.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sGetInWaypointPrefab;

	[Attribute(defvalue: "0.75", desc: "Yield releases if the bubble scan hasn't refreshed it for this many seconds. ~3 ticks at the default 0.25s interval — covers cop driving past, killing the lights, or being deleted in a single mechanism.")]
	protected float m_fStaleReleaseSeconds;

	[Attribute(defvalue: "1", desc: "Right-hand traffic (Reforger vanilla — Everon, Arland). When true, the pull-over goal is biased forward + right; flip to false for left-hand-drive maps. The script API doesn't expose a per-world traffic-side flag, so this is a manual toggle.")]
	protected bool m_bRightHandTraffic;

	protected ref array<IEntity> m_aQueryResults = {};
	protected ref map<RP_PoliceVehicleComponent, bool> m_mLastLightsOn = new map<RP_PoliceVehicleComponent, bool>();
	// Vehicles we've already logged a lookup-failure diagnostic for, so
	// the chain trace doesn't spam every tick when a non-yielding vehicle
	// (player, untagged crew) stays in the bubble.
	protected ref set<IEntity> m_sDiagnosedFailures = new set<IEntity>();
	// Active yields — keyed by the SCR_AIGroup currently held in place.
	protected ref map<SCR_AIGroup, ref RP_YieldedGroupState> m_mYieldedGroups = new map<SCR_AIGroup, ref RP_YieldedGroupState>();

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
		// Active yields evaluate first — this way "cop drove off / killed
		// lights / was deleted" releases fire even if the registry is now
		// empty (e.g. last cop despawned mid-stop).
		CheckReleases();

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
		SCR_AIGroup group = SCR_AIGroup.Cast(compliance.GetOwner());
		if (!group)
		{
			Print(string.Format("[RP_Yield] compliance owner is not SCR_AIGroup — cannot yield. Owner: %1", compliance.GetOwner()), LogLevel.WARNING);
			return;
		}
		// Already yielded — refresh the staleness timestamp so the yield
		// stays alive while the cop keeps the vehicle in its bubble. The
		// timestamp is the only thing keeping CheckReleases from firing.
		RP_YieldedGroupState existing = m_mYieldedGroups.Get(group);
		if (existing)
		{
			existing.m_fLastRefreshTime = GetWorldTimeSeconds();
			return;
		}
		BeginYield(group, vehicle, copCar, inFront, dist);
	}

	protected void BeginYield(SCR_AIGroup group, IEntity vehicle, IEntity copCar, bool inFront, float dist)
	{
		// Snapshot the current waypoint queue. Re-adding these references
		// on release should preserve any internal state (e.g. cycle
		// position) carried by the waypoint instances themselves.
		array<AIWaypoint> saved = {};
		group.GetWaypoints(saved);

		// Strip the queue. RemoveWaypoint unlinks from the group; we hold
		// the references in `saved` so the engine doesn't garbage them.
		foreach (AIWaypoint wp : saved)
		{
			if (wp)
				group.RemoveWaypoint(wp);
		}

		vector pullOverPos = ComputePullOverPosition(vehicle);

		AIWaypoint moveWp = SpawnMoveWaypoint(pullOverPos);
		if (moveWp)
			group.AddWaypoint(moveWp);

		RP_YieldedGroupState state = new RP_YieldedGroupState();
		state.m_Group = group;
		state.m_aSavedWaypoints = saved;
		state.m_PullOverWaypoint = moveWp;
		state.m_AssignedCop = copCar;
		state.m_StoppedVehicle = vehicle;
		state.m_fLastRefreshTime = GetWorldTimeSeconds();
		m_mYieldedGroups.Set(group, state);

		Print(string.Format("[RP_Yield] BeginYield: %1 (%2m %3) for cop %4 — saved %5 waypoints, move at %6.", vehicle, Math.Round(dist), BucketLabel(inFront), copCar, saved.Count(), pullOverPos), LogLevel.NORMAL);
	}

	protected void EndYield(RP_YieldedGroupState state)
	{
		if (!state || !state.m_Group)
			return;
		SCR_AIGroup group = state.m_Group;

		// Remove the spawned Move from the queue and free its entity —
		// otherwise each yield leaks one waypoint entity.
		if (state.m_PullOverWaypoint)
		{
			group.RemoveWaypoint(state.m_PullOverWaypoint);
			SCR_EntityHelper.DeleteEntityAndChildren(state.m_PullOverWaypoint);
		}

		// If the driver bailed, prepend a GetIn so they walk back and
		// re-board before processing the cycle. Without this they'd
		// just stand next to the parked vehicle while the saved queue
		// runs into nothing.
		if (state.m_bDriverBailed && state.m_StoppedVehicle)
		{
			AIWaypoint getIn = SpawnGetInWaypoint(state.m_StoppedVehicle);
			if (getIn)
			{
				group.AddWaypoint(getIn);
				Print(string.Format("[RP_Yield] Bail recovery: prepended GetIn for %1 to re-board.", state.m_StoppedVehicle), LogLevel.NORMAL);
			}
			else
			{
				Print("[RP_Yield] Bail recovery: SpawnGetInWaypoint failed, driver will stand idle.", LogLevel.WARNING);
			}
		}

		// Restore the original queue in saved order. Count valid vs
		// total — if any saved reference went null between snapshot
		// and restore, RemoveWaypoint may have destroyed it and we'd
		// need to switch to spawning fresh waypoints from snapshot
		// data instead of relinking instances.
		int restored = 0;
		int total = state.m_aSavedWaypoints.Count();
		foreach (AIWaypoint wp : state.m_aSavedWaypoints)
		{
			if (wp)
			{
				group.AddWaypoint(wp);
				restored++;
			}
		}

		m_mYieldedGroups.Remove(group);
		if (restored < total)
			Print(string.Format("[RP_Yield] EndYield: %1 (restored %2 of %3 waypoints — %4 references went null, RemoveWaypoint may be destroying them).", group, restored, total, total - restored), LogLevel.WARNING);
		else
			Print(string.Format("[RP_Yield] EndYield: %1 (restored %2 waypoints).", group, restored), LogLevel.NORMAL);
	}

	protected AIWaypoint SpawnGetInWaypoint(IEntity targetVehicle)
	{
		if (m_sGetInWaypointPrefab.IsEmpty() || !targetVehicle)
			return null;
		Resource res = Resource.Load(m_sGetInWaypointPrefab);
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = targetVehicle.GetOrigin();
		IEntity ent = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!ent)
			return null;
		AIWaypoint wp = AIWaypoint.Cast(ent);
		// Boarding waypoint needs the target vehicle bound.
		SCR_BoardingEntityWaypoint board = SCR_BoardingEntityWaypoint.Cast(wp);
		if (board)
			board.SetEntity(targetVehicle);
		return wp;
	}

	// Iterates active yields and releases any whose bubble-scan refresh
	// has gone stale, OR whose driver has bailed out of the vehicle.
	// Stale-refresh handles the three "cop has cleared" cases in one
	// mechanism:
	//   - Cop drives past → vehicle leaves bubble → no refresh → stale.
	//   - Cop kills lights → ScanBubble skipped for that cop → no
	//     refresh → stale.
	//   - Cop is deleted → registry shrinks → no refresh → stale.
	// Bail detection surfaces "AI gave up on the pull-over and
	// dismounted" as an explicit log line so we can tell that case
	// apart from a normal release.
	protected void CheckReleases()
	{
		if (m_mYieldedGroups.IsEmpty())
			return;
		float now = GetWorldTimeSeconds();
		array<SCR_AIGroup> toRelease = {};
		foreach (SCR_AIGroup group, RP_YieldedGroupState state : m_mYieldedGroups)
		{
			if (!state || !state.m_StoppedVehicle)
			{
				toRelease.Insert(group);
				continue;
			}
			if (!FindDriverEntity(state.m_StoppedVehicle))
			{
				Print(string.Format("[RP_Yield] Driver bailed from %1 — releasing yield, will prepend GetIn.", state.m_StoppedVehicle), LogLevel.WARNING);
				state.m_bDriverBailed = true;
				toRelease.Insert(group);
				continue;
			}
			if (now - state.m_fLastRefreshTime > m_fStaleReleaseSeconds)
				toRelease.Insert(group);
		}
		foreach (SCR_AIGroup group : toRelease)
		{
			RP_YieldedGroupState state = m_mYieldedGroups.Get(group);
			EndYield(state);
		}
	}

	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;
		return world.GetWorldTime() / 1000.0;
	}

	// Picks a target position for the pull-over Move waypoint. Strategy:
	//   1. Ask RoadNetworkManager for a reachable in-road waypoint 10m
	//      forward of the vehicle. If the road network returns a valid
	//      point AND the result is forward of the vehicle's heading,
	//      use it.
	//   2. If the snap returned a point that's behind or sharply
	//      sideways (>60° off forward), reject it — using it would
	//      force the AI to U-turn, which it dislikes enough to
	//      dismount the driver. Fall through to step 3.
	//   3. Fallback: vehicle's current position. A Move at current pos
	//      completes immediately and the empty queue holds the AI in
	//      place — worst case visual is "vehicle stops where it is"
	//      but the AI faces no impossible navigation to give up on.
	// Real shoulder-offset polish is a separate refinement; getting
	// in-lane reliable first.
	protected vector ComputePullOverPosition(IEntity vehicle)
	{
		vector tm[4];
		vehicle.GetTransform(tm);
		vector pos = tm[3];
		vector right = tm[0];
		vector forward = tm[2];

		// Bias the goal toward the curb side. tm[0] is the vehicle's
		// right basis; for left-hand-drive maps we negate. The bias is
		// just a hint — GetReachableWaypointInRoad usually snaps to the
		// road centerline regardless, but on roads with multiple in-road
		// graph nodes the snapper may pick the one closer to our hint.
		float lateralSign;
		if (m_bRightHandTraffic)
			lateralSign = 1.0;
		else
			lateralSign = -1.0;
		vector lateral = right * (3.0 * lateralSign);

		AIWorld aiWorld = GetGame().GetAIWorld();
		if (aiWorld)
		{
			ChimeraAIWorld chimeraAI = ChimeraAIWorld.Cast(aiWorld);
			if (chimeraAI)
			{
				RoadNetworkManager roadMgr = chimeraAI.GetRoadNetworkManager();
				if (roadMgr)
				{
					vector goal = pos + forward * 10.0 + lateral;
					vector outPos;
					if (roadMgr.GetReachableWaypointInRoad(pos, goal, 50.0, outPos))
					{
						vector toSnap = outPos - pos;
						float dist = toSnap.Length();
						if (dist > 0.1)
						{
							float forwardness = vector.Dot(toSnap * (1.0 / dist), forward);
							if (forwardness > 0.5)
								return outPos;
							Print(string.Format("[RP_Yield] Road snap rejected (forwardness %1 ≤ 0.5 — would force U-turn). Falling back to stop-in-place.", forwardness), LogLevel.NORMAL);
						}
					}
				}
			}
		}
		Print(string.Format("[RP_Yield] Pull-over fallback: stop in place at %1.", pos), LogLevel.NORMAL);
		return pos;
	}

	protected AIWaypoint SpawnMoveWaypoint(vector pos)
	{
		if (m_sMoveWaypointPrefab.IsEmpty())
			return null;
		Resource res = Resource.Load(m_sMoveWaypointPrefab);
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = pos;
		IEntity ent = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!ent)
			return null;
		return AIWaypoint.Cast(ent);
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
