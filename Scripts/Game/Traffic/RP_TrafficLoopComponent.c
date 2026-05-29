/**
 * RP_TrafficLoopComponent — Phase 1 traffic POC (multi-vehicle pool).
 *
 * Manager component on the GameMode. Maintains a pool of N AI-driven
 * civilian vehicles cycling around the same set of waypoints. Each
 * spawn pairs a group prefab with a vehicle prefab; the manager
 * rotates through the configured pairs round-robin. When an active
 * vehicle is destroyed or its group dies out, the manager spawns a
 * replacement — wreckage and corpses are left for cleanup crews
 * (EMS, tow, etc.) to pick up.
 *
 * Server-authoritative — AI runs only on the server.
 *
 * World setup (placed entities + RP_TrafficMarkerComponent):
 *   - 1 entity with type=CREW_SPAWN   (placed character-or-marker;
 *                                      gets deleted on start, vehicles
 *                                      spawn from this position)
 *   - 2+ entities with type=WAYPOINT  (each with a unique m_iIndex,
 *                                      in cycle order)
 *
 * Component config (in RP_TrafficLoopComponent attributes):
 *   - m_aSpawnConfigs: array of {group prefab, vehicle prefab}
 *   - m_iTargetActiveCount: pool size (target active vehicles)
 *   - m_fSpawnIntervalSeconds: time between top-up spawns
 *
 * The legacy VEHICLE marker is deprecated — placing a vehicle in the
 * world and tagging it as VEHICLE now logs a warning and is ignored.
 */

enum ERP_TrafficMarkerType
{
	VEHICLE,        // DEPRECATED — vehicles now spawned from m_aSpawnConfigs.
	CREW_SPAWN,
	WAYPOINT,
}

[ComponentEditorProps(category: "RP/Traffic", description: "Marks a placed entity as part of the traffic loop. Pair with RP_TrafficLoopComponent on the GameMode.")]
class RP_TrafficMarkerComponentClass : ScriptComponentClass
{
}

class RP_TrafficMarkerComponent : ScriptComponent
{
	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "What this marker represents.", enums: ParamEnumArray.FromEnum(ERP_TrafficMarkerType))]
	protected ERP_TrafficMarkerType m_eType;

	[Attribute(defvalue: "1", desc: "Order in cycle (1, 2, 3, ...). Only used when type = WAYPOINT.")]
	protected int m_iIndex;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
			return;
		// Manager may not be initialised yet; defer registration.
		GetGame().GetCallqueue().CallLater(TryRegister, 100, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryRegister);
		super.OnDelete(owner);
	}

	protected void TryRegister()
	{
		RP_TrafficLoopComponent mgr = RP_TrafficLoopComponent.GetInstance();
		if (!mgr)
			return;
		GetGame().GetCallqueue().Remove(TryRegister);

		switch (m_eType)
		{
			case ERP_TrafficMarkerType.VEHICLE:
				mgr.RegisterVehicle(GetOwner());
				break;
			case ERP_TrafficMarkerType.CREW_SPAWN:
				mgr.RegisterCrewSpawn(GetOwner());
				break;
			case ERP_TrafficMarkerType.WAYPOINT:
				mgr.RegisterWaypoint(m_iIndex, GetOwner());
				break;
		}
	}
}

// One row in the manager's m_aSpawnConfigs array — pairs a group
// prefab with the vehicle the group's driver will board.
[BaseContainerProps()]
class RP_TrafficSpawnConfig
{
	[Attribute(desc: "AI crew group prefab — should carry RP_DriverComplianceComponent if you want this group to yield to emergency vehicles.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	ResourceName m_sGroupPrefab;

	[Attribute(desc: "Vehicle prefab the group's driver will board.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	ResourceName m_sVehiclePrefab;
}

// One live spawn — vehicle + crew references tracked for the
// maintain-N-active logic. PruneDead drops entries whose vehicle has
// been destroyed or whose crew has no agents left.
class RP_TrafficActiveSpawn
{
	IEntity m_Vehicle;
	SCR_AIGroup m_Crew;
	// Plate string assigned at spawn — "[FactionKey]_Car_[N]". Mirrored
	// into RP_TrafficLoopComponent's m_mPlatesByRplId so clients can
	// resolve it; kept here too for spawn/prune logs.
	string m_sName;
	// Agents observed parented to m_Vehicle at least once. ReapBailedCrew
	// uses this to distinguish "still walking to the car" (never boarded
	// yet, leave alone) from "boarded then bailed" (reap).
	ref set<AIAgent> m_BoardedAgents = new set<AIAgent>();
}

[ComponentEditorProps(category: "RP/Traffic", description: "Traffic loop manager. Attach to the GameMode entity.")]
class RP_TrafficLoopComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_TrafficLoopComponent : SCR_BaseGameModeComponent
{
	[Attribute(desc: "Spawn configurations — {group prefab, vehicle prefab} pairs. Manager round-robins through this list as it tops up the pool. At least one entry required.")]
	protected ref array<ref RP_TrafficSpawnConfig> m_aSpawnConfigs;

	[Attribute(defvalue: "5", desc: "Target pool size — how many vehicles to keep active. Manager spawns replacements as vehicles die.")]
	protected int m_iTargetActiveCount;

	[Attribute(defvalue: "8.0", desc: "Seconds between top-up attempts. Doubles as min spacing between consecutive spawns at the same point.")]
	protected float m_fSpawnIntervalSeconds;

	[Attribute(defvalue: "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", desc: "Move waypoint prefab.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sMoveWaypointPrefab;

	[Attribute(defvalue: "{712F4795CF8B91C7}Prefabs/AI/Waypoints/AIWaypoint_GetIn.et", desc: "GetIn waypoint prefab.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sGetInWaypointPrefab;

	[Attribute(desc: "AIWaypointCycle prefab — pick the engine's stock cycle waypoint.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sCycleWaypointPrefab;

	[Attribute(defvalue: "2.0", desc: "Seconds to wait after game start before launching the loop. Gives marker components time to register.")]
	protected float m_fStartDelaySeconds;

	[Attribute(defvalue: "3.0", desc: "Clearance radius at the spawn position. Positions with another Vehicle or character within this radius are treated as blocked and the manager tries a small set of offsets.")]
	protected float m_fClearanceRadiusMeters;

	[Attribute(defvalue: "4.0", desc: "Offset distance when searching for an unblocked spawn position around the CREW_SPAWN marker.")]
	protected float m_fOffsetSearchDistance;

	[Attribute(defvalue: "2.0", desc: "Lateral offset (m) for the crew spawn, toward the driver side (left of the spawn point's facing), so the AI does NOT materialize inside the just-spawned vehicle's collision (which launches/crushes them on spawn). The GetIn waypoint then walks the crew in to board.")]
	protected float m_fCrewSpawnLateralOffset;

	[Attribute(defvalue: "300.0", desc: "Seconds between orphan-crew sweeps. Safety net for civilian AI groups that lost their vehicle without going through PruneDead (pre-fix accumulation, engine GarbageSystem races). Per-pass cost is tiny (~one set-lookup per active group), so this can run as fast or slow as you want — default 5 min is more than enough.")]
	protected float m_fOrphanReapIntervalSeconds;

	protected static RP_TrafficLoopComponent s_Instance;
	protected IEntity m_CrewSpawnMarker;
	protected vector m_aCrewSpawnRot[4];
	protected ref array<IEntity> m_aWaypointMarkersByIndex = {};

	// Cached after StartLoop deletes the placeholder marker entities.
	protected vector m_vCrewSpawnPosition;
	protected ref array<vector> m_aWaypointPositions = {};

	// Active pool. Pruned each tick; new spawns added to keep up with
	// m_iTargetActiveCount.
	protected ref array<ref RP_TrafficActiveSpawn> m_aActiveSpawns = new array<ref RP_TrafficActiveSpawn>();
	protected int m_iNextConfigIndex;
	protected bool m_bStarted;

	// Per-faction-key counters for diagnostic vehicle naming. Each
	// faction starts at 1 and never resets — a long session reads
	// CIV_Car_47 etc. and that's fine.
	protected ref map<string, int> m_mFactionCounters = new map<string, int>();

	// Plate registry, replicated server->client via RPC. IEntity.SetName
	// is not networked, so the HUD on a remote client can't read plates
	// off the vehicle entity directly. The traffic loop registers each
	// spawned vehicle here (server) and broadcasts to clients; the HUD
	// queries by IEntity → RplId. Late-joiners RPC the server for a
	// full resync on component init.
	protected ref map<RplId, string> m_mPlatesByRplId = new map<RplId, string>();

	// Scratch state for the clearance query callback (the query API
	// uses a free-function-style callback, not a closure, so we set a
	// flag and read it after the call).
	protected bool m_bClearCheck_Blocked;

	static RP_TrafficLoopComponent GetInstance() { return s_Instance; }

	// ----------------------------------------------------------------------
	// Live cap control (admin UI)
	// ----------------------------------------------------------------------

	int GetTargetActiveCount() { return m_iTargetActiveCount; }

	// Server-only. Updates the pool target and immediately culls excess so
	// the slider feels responsive. The natural-attrition path (just lower
	// the cap, wait for vehicles to die) is left for the loop's normal
	// behavior on the way back up — TrySpawnOne already gates on the cap.
	void SetTargetActiveCount(int newTarget)
	{
		if (!Replication.IsServer())
			return;
		if (newTarget < 0)
			newTarget = 0;

		int oldTarget = m_iTargetActiveCount;
		m_iTargetActiveCount = newTarget;
		Print(string.Format("[RP_Traffic] Target active count: %1 → %2 (currently %3 active)", oldTarget, newTarget, m_aActiveSpawns.Count()), LogLevel.NORMAL);

		int culled = 0;
		while (m_aActiveSpawns.Count() > m_iTargetActiveCount)
		{
			DespawnLast();
			culled++;
		}
		if (culled > 0)
			Print(string.Format("[RP_Traffic] Culled %1 active vehicle(s) to honor lowered cap.", culled), LogLevel.NORMAL);

		// Pool size just changed — rebalance the LPR watchlist against
		// the new active set (drop orphans, top up to ~5% of pool).
		NotifyWatchlistOfPoolChange();
	}

	// Despawns the most-recently-spawned entry. Mirrors PruneDead's
	// cleanup (ReapCrewLiveAgents + remove from m_aActiveSpawns) but also
	// deletes the still-alive vehicle entity, which PruneDead never has
	// to do because its entries are already dead.
	protected void DespawnLast()
	{
		int idx = m_aActiveSpawns.Count() - 1;
		if (idx < 0)
			return;
		RP_TrafficActiveSpawn entry = m_aActiveSpawns[idx];
		if (entry)
		{
			if (entry.m_Crew)
				ReapCrewLiveAgents(entry.m_Crew);
			entry.m_Crew = null;
			if (entry.m_Vehicle)
			{
				SCR_EntityHelper.DeleteEntityAndChildren(entry.m_Vehicle);
				entry.m_Vehicle = null;
			}
		}
		m_aActiveSpawns.Remove(idx);
	}

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
		{
			// Late-join plate resync is fired by the local player's
			// RP_PlayerRpcRelayComponent (the relay is owned by the
			// joining client, so RplRcver.Server on it actually
			// arrives — unlike client->server on this GameMode
			// component, which gets dropped because the GameMode is
			// server-owned).
			return;
		}
		GetGame().GetCallqueue().CallLater(StartLoop, (int)(m_fStartDelaySeconds * 1000), false);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(SpawnTick);
		GetGame().GetCallqueue().Remove(ReapOrphanCrews);
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	// Legacy compat — VEHICLE marker is no longer how vehicles enter
	// the pool. Log so the user knows to remove the marker (or the
	// placed vehicle entirely) from their world.
	void RegisterVehicle(IEntity vehicle)
	{
		Print(string.Format("[RP_Traffic] VEHICLE marker on %1 is deprecated and ignored — vehicles are now spawned from RP_TrafficLoopComponent.m_aSpawnConfigs. Remove the marker (or the placed vehicle) from the world.", vehicle), LogLevel.WARNING);
	}

	void RegisterCrewSpawn(IEntity marker)
	{
		m_CrewSpawnMarker = marker;
		Print(string.Format("[RP_Traffic] Crew spawn registered at %1", marker.GetOrigin()), LogLevel.NORMAL);
	}

	void RegisterWaypoint(int index, IEntity wp)
	{
		while (m_aWaypointMarkersByIndex.Count() <= index)
			m_aWaypointMarkersByIndex.Insert(null);
		m_aWaypointMarkersByIndex[index] = wp;
		Print(string.Format("[RP_Traffic] Waypoint #%1 registered at %2", index, wp.GetOrigin()), LogLevel.NORMAL);
	}

	protected void StartLoop()
	{
		if (m_bStarted)
			return;
		if (!m_CrewSpawnMarker)
		{
			Print("[RP_Traffic] No CREW_SPAWN marker registered — loop won't start.", LogLevel.WARNING);
			return;
		}
		if (!m_aSpawnConfigs || m_aSpawnConfigs.IsEmpty())
		{
			Print("[RP_Traffic] m_aSpawnConfigs is empty — loop won't start. Add at least one {group prefab, vehicle prefab} pair.", LogLevel.WARNING);
			return;
		}

		// Cache waypoint positions so we can delete the marker entities
		// if the user used placeholder characters. (Currently we leave
		// them; only the CREW_SPAWN marker is deleted.)
		m_aWaypointPositions.Clear();
		foreach (IEntity wp : m_aWaypointMarkersByIndex)
		{
			if (wp)
				m_aWaypointPositions.Insert(wp.GetOrigin());
		}
		if (m_aWaypointPositions.Count() < 2)
		{
			Print(string.Format("[RP_Traffic] Need at least 2 WAYPOINT markers, got %1 — loop won't start.", m_aWaypointPositions.Count()), LogLevel.WARNING);
			return;
		}

		// Cache spawn position and delete the placeholder marker so it
		// doesn't sit at the spawn location as an always-blocking
		// character.
		m_CrewSpawnMarker.GetTransform(m_aCrewSpawnRot);
		m_vCrewSpawnPosition = m_aCrewSpawnRot[3];
		SCR_EntityHelper.DeleteEntityAndChildren(m_CrewSpawnMarker);
		m_CrewSpawnMarker = null;

		m_bStarted = true;
		Print(string.Format("[RP_Traffic] Loop started — target %1 active, spawn interval %2s, %3 waypoints in cycle, %4 configs.", m_iTargetActiveCount, m_fSpawnIntervalSeconds, m_aWaypointPositions.Count(), m_aSpawnConfigs.Count()), LogLevel.NORMAL);

		// Repeating spawn tick. First call happens immediately so the
		// pool starts populating without waiting for the first interval.
		GetGame().GetCallqueue().CallLater(SpawnTick, (int)(m_fSpawnIntervalSeconds * 1000), true);
		SpawnTick();

		// Slow orphan-reaper. Decoupled from SpawnTick so the per-3s
		// hot path stays minimal, and so the reap cadence reads
		// independently (5 min by default — see attribute). First fire
		// is one interval in; pre-fix accumulation gets cleared on the
		// first sweep after server restart.
		int reapMs = (int)(m_fOrphanReapIntervalSeconds * 1000);
		if (reapMs > 0)
			GetGame().GetCallqueue().CallLater(ReapOrphanCrews, reapMs, true);
	}

	protected void SpawnTick()
	{
		ReapBailedCrew();
		int beforePrune = m_aActiveSpawns.Count();
		PruneDead();
		int afterPrune = m_aActiveSpawns.Count();
		int beforeSpawn = afterPrune;
		if (m_aActiveSpawns.Count() < m_iTargetActiveCount)
			TrySpawnOne();
		int afterSpawn = m_aActiveSpawns.Count();

		// Rebalance the LPR watchlist if the live pool changed this tick
		// (vehicle pruned, vehicle spawned, or both). A no-op tick is the
		// common case — skip the work in that case.
		if (afterPrune != beforePrune || afterSpawn != beforeSpawn)
			NotifyWatchlistOfPoolChange();
	}

	// Drops entries whose vehicle has been destroyed (wreckage) or
	// whose crew has been wiped out. Wreckage and corpses are left in
	// the world for cleanup gameplay (EMS, tow). Live AI orphaned by a
	// vanished vehicle, however, get cleaned up here — they have no
	// vehicle, no waypoints, and no purpose, and pile up otherwise.
	protected void PruneDead()
	{
		array<int> toRemove = {};
		for (int i = 0; i < m_aActiveSpawns.Count(); i++)
		{
			RP_TrafficActiveSpawn entry = m_aActiveSpawns[i];
			if (IsEntryDead(entry))
				toRemove.Insert(i);
		}
		// Remove highest index first so earlier indices stay valid.
		for (int j = toRemove.Count() - 1; j >= 0; j--)
		{
			int idx = toRemove[j];
			RP_TrafficActiveSpawn entry = m_aActiveSpawns[idx];
			int killed = ReapCrewLiveAgents(entry.m_Crew);
			Print(string.Format("[RP_Traffic] Pruning dead entry %1 (vehicle=%2 crew=%3, orphans killed=%4).", entry.m_sName, entry.m_Vehicle, entry.m_Crew, killed), LogLevel.NORMAL);
			entry.m_Crew = null;
			m_aActiveSpawns.Remove(idx);
		}
	}

	// Per-tick sweep over active spawns: any crew member who has been in
	// the vehicle and then left is reaped on the spot. The traffic loop's
	// contract is "keep N vehicles cycling" — wandering civilians on foot
	// have no role, and leaving them around piles up AI that the engine
	// then has to path-find. Once a member is reaped, SCR_AIGroup drops
	// the agent and (when crew empties) IsEntryDead picks up naturally.
	// The per-entry m_BoardedAgents set gates this so freshly-spawned
	// crew still walking to the vehicle isn't killed pre-boarding.
	// Captive / unconscious civilians are explicitly skipped — a cop in
	// the middle of arresting someone (dragged out of the car, cuffed,
	// or stuffed into a cop car) must not have the suspect vanish
	// out from under them.
	protected void ReapBailedCrew()
	{
		foreach (RP_TrafficActiveSpawn entry : m_aActiveSpawns)
		{
			if (!entry || !entry.m_Vehicle || !entry.m_Crew)
				continue;
			array<AIAgent> agents = {};
			entry.m_Crew.GetAgents(agents);
			int reaped = 0;
			foreach (AIAgent agent : agents)
			{
				if (!agent)
					continue;
				IEntity character = agent.GetControlledEntity();
				if (!character)
					continue;
				if (character.GetParent() == entry.m_Vehicle)
				{
					entry.m_BoardedAgents.Insert(agent);
					continue;
				}
				if (!entry.m_BoardedAgents.Contains(agent))
					continue;  // never boarded — still inbound
				SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.Cast(character.FindComponent(SCR_DamageManagerComponent));
				if (dmg && dmg.GetState() == EDamageState.DESTROYED)
					continue;
				if (IsArrestedOrDowned(character))
					continue;  // mid-arrest / unconscious — leave for the cop
				SCR_EntityHelper.DeleteEntityAndChildren(character);
				reaped++;
			}
			if (reaped > 0)
				Print(string.Format("[RP_Traffic] Reaped %1 bailed crew member(s) from %2.", reaped, entry.m_sName), LogLevel.NORMAL);
		}
	}

	// True when the character is either ACE-captive (cuffed) or not in
	// the ALIVE life state (unconscious, incapacitated, the brief
	// downed phase before damage state flips to DESTROYED). Mirrors
	// the cross-mod check used by RP_EmergencyYieldComponent.
	protected bool IsArrestedOrDowned(IEntity character)
	{
		SCR_ChimeraCharacter chim = SCR_ChimeraCharacter.Cast(character);
		if (!chim)
			return false;
		SCR_CharacterControllerComponent ctl = SCR_CharacterControllerComponent.Cast(chim.GetCharacterController());
		if (!ctl)
			return false;
		if (ctl.GetLifeState() != ECharacterLifeState.ALIVE)
			return true;
		if (ctl.ACE_Captives_IsCaptive())
			return true;
		return false;
	}

	// Sweeps the RP_DriverComplianceComponent registry for civilian
	// groups that aren't held by any active spawn. Catches:
	//   - pre-fix accumulation: groups whose entry was pruned before
	//     ReapCrewLiveAgents existed.
	//   - race window: engine's GarbageSystem reaps a vehicle and the
	//     crew is orphan for one tick before PruneDead sees it (this
	//     sweep catches them on the same tick the prune does, so no
	//     visible delay).
	// Cops/dispatch groups don't carry RP_DriverComplianceComponent,
	// so they're inherently excluded.
	// Until civilians get something to do on foot (call a tow, request
	// a new car), the right answer for an orphan is just reap. Re-think
	// when that gameplay lands.
	protected void ReapOrphanCrews()
	{
		array<RP_DriverComplianceComponent> instances = RP_DriverComplianceComponent.GetInstances();
		if (!instances || instances.IsEmpty())
			return;
		set<SCR_AIGroup> activeGroups = new set<SCR_AIGroup>();
		foreach (RP_TrafficActiveSpawn entry : m_aActiveSpawns)
		{
			if (entry && entry.m_Crew)
				activeGroups.Insert(entry.m_Crew);
		}
		// Snapshot the registry — deleting groups inside the loop
		// mutates s_aInstances via the component's OnDelete.
		array<SCR_AIGroup> toReap = {};
		foreach (RP_DriverComplianceComponent compliance : instances)
		{
			if (!compliance)
				continue;
			SCR_AIGroup group = SCR_AIGroup.Cast(compliance.GetOwner());
			if (!group)
				continue;
			if (activeGroups.Contains(group))
				continue;
			toReap.Insert(group);
		}
		if (toReap.IsEmpty())
			return;
		int totalKilled = 0;
		foreach (SCR_AIGroup group : toReap)
		{
			totalKilled += ReapCrewLiveAgents(group);
		}
		Print(string.Format("[RP_Traffic] Reaped %1 orphan crew group(s), %2 live AI deleted.", toReap.Count(), totalKilled), LogLevel.NORMAL);
	}

	// Deletes character entities for any AI agent in the group whose
	// controlled character is still alive, then deletes the
	// SCR_AIGroup container. Corpses (DESTROYED damage state) are left
	// alone — cleanup gameplay still applies. Returns the number of
	// live characters deleted.
	protected int ReapCrewLiveAgents(SCR_AIGroup crew)
	{
		if (!crew)
			return 0;
		array<AIAgent> agents = {};
		crew.GetAgents(agents);
		int killed = 0;
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;
			IEntity character = agent.GetControlledEntity();
			if (!character)
				continue;
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.Cast(character.FindComponent(SCR_DamageManagerComponent));
			if (dmg && dmg.GetState() == EDamageState.DESTROYED)
				continue;
			SCR_EntityHelper.DeleteEntityAndChildren(character);
			killed++;
		}
		SCR_EntityHelper.DeleteEntityAndChildren(crew);
		return killed;
	}

	protected bool IsEntryDead(RP_TrafficActiveSpawn entry)
	{
		if (!entry)
			return true;
		if (!entry.m_Vehicle)
			return true;
		if (IsVehicleDestroyed(entry.m_Vehicle))
			return true;
		if (!entry.m_Crew)
			return true;
		// Empty group == every member dead/removed; treat as dead so
		// the pool tops up. Driverless-but-intact vehicle stays as
		// scenery for cleanup gameplay.
		array<AIAgent> agents = {};
		entry.m_Crew.GetAgents(agents);
		if (agents.IsEmpty())
			return true;
		return false;
	}

	protected bool IsVehicleDestroyed(IEntity vehicle)
	{
		SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.Cast(vehicle.FindComponent(SCR_DamageManagerComponent));
		if (!dmg)
			return false;
		return dmg.GetState() == EDamageState.DESTROYED;
	}

	protected void TrySpawnOne()
	{
		vector spawnPos;
		if (!TryFindClearSpawnPos(m_vCrewSpawnPosition, spawnPos))
		{
			Print(string.Format("[RP_Traffic] Spawn area around %1 is blocked, deferring to next tick.", m_vCrewSpawnPosition), LogLevel.NORMAL);
			return;
		}

		// Round-robin through configs.
		RP_TrafficSpawnConfig cfg = m_aSpawnConfigs[m_iNextConfigIndex];
		int usedIndex = m_iNextConfigIndex;
		m_iNextConfigIndex = (m_iNextConfigIndex + 1) % m_aSpawnConfigs.Count();

		if (!cfg || cfg.m_sGroupPrefab.IsEmpty() || cfg.m_sVehiclePrefab.IsEmpty())
		{
			Print(string.Format("[RP_Traffic] Spawn config at index %1 has missing prefab — skipping this tick.", usedIndex), LogLevel.WARNING);
			return;
		}

		SpawnVehicleAndCrew(cfg, spawnPos);
	}

	protected void SpawnVehicleAndCrew(RP_TrafficSpawnConfig cfg, vector spawnPos)
	{
		vector spawnTm[4];
		spawnTm[0] = m_aCrewSpawnRot[0];
		spawnTm[1] = m_aCrewSpawnRot[1];
		spawnTm[2] = m_aCrewSpawnRot[2];
		spawnTm[3] = spawnPos;

		IEntity vehicleEnt = SpawnEntityAtTransform(cfg.m_sVehiclePrefab, spawnTm);
		if (!vehicleEnt)
		{
			Print(string.Format("[RP_Traffic] Vehicle prefab %1 failed to spawn.", cfg.m_sVehiclePrefab), LogLevel.ERROR);
			return;
		}

		// Spawn the crew beside the vehicle (driver side), not on top of it.
		// Materializing a character inside the just-spawned vehicle's collision
		// launches / crushes it ("the car crushed the driver on spawn"). Offset
		// along the spawn transform's -right axis (driver/left side); the GetIn
		// waypoint below then walks them in to board normally. Vehicle stays at
		// spawnPos so it sits correctly on the road/cycle start.
		vector crewTm[4];
		crewTm[0] = spawnTm[0];
		crewTm[1] = spawnTm[1];
		crewTm[2] = spawnTm[2];
		crewTm[3] = spawnPos - spawnTm[0] * m_fCrewSpawnLateralOffset;

		IEntity crewEnt = SpawnEntityAtTransform(cfg.m_sGroupPrefab, crewTm);
		SCR_AIGroup crew = SCR_AIGroup.Cast(crewEnt);
		if (!crew)
		{
			Print(string.Format("[RP_Traffic] Group prefab %1 failed to spawn — deleting orphan vehicle.", cfg.m_sGroupPrefab), LogLevel.ERROR);
			SCR_EntityHelper.DeleteEntityAndChildren(vehicleEnt);
			return;
		}

		AIWaypoint getIn = SpawnWaypoint(m_sGetInWaypointPrefab, vehicleEnt.GetOrigin());
		if (getIn)
		{
			SCR_BoardingEntityWaypoint board = SCR_BoardingEntityWaypoint.Cast(getIn);
			if (board)
				board.SetEntity(vehicleEnt);
			crew.AddWaypoint(getIn);
		}

		AIWaypoint cycleWp = SpawnWaypoint(m_sCycleWaypointPrefab, spawnPos);
		AIWaypointCycle cycle = AIWaypointCycle.Cast(cycleWp);
		if (!cycle)
		{
			Print("[RP_Traffic] Cycle prefab missing or wrong type — check m_sCycleWaypointPrefab.", LogLevel.ERROR);
			return;
		}
		array<AIWaypoint> children = {};
		foreach (vector wpPos : m_aWaypointPositions)
		{
			AIWaypoint moveWp = SpawnWaypoint(m_sMoveWaypointPrefab, wpPos);
			if (moveWp)
				children.Insert(moveWp);
		}
		cycle.SetWaypoints(children);
		crew.AddWaypoint(cycleWp);

		string assignedName = AllocateVehicleName(crew);
		RegisterPlate(vehicleEnt, assignedName);
		TurnOnHeadlights(vehicleEnt);

		RP_TrafficActiveSpawn entry = new RP_TrafficActiveSpawn();
		entry.m_Vehicle = vehicleEnt;
		entry.m_Crew = crew;
		entry.m_sName = assignedName;
		m_aActiveSpawns.Insert(entry);

		Print(string.Format("[RP_Traffic] Spawned %1 (vehicle=%2 crew=%3 at %4, active %5/%6).", entry.m_sName, vehicleEnt, crew, spawnPos, m_aActiveSpawns.Count(), m_iTargetActiveCount), LogLevel.NORMAL);
	}

	// Builds "[FactionKey]_Car_[N]" using a per-faction counter.
	// Falls back to "Unknown" if the group has no resolvable faction.
	//
	// Tries GetFactionName() first because it usually returns the raw
	// m_faction string from the prefab without going through the
	// FactionManager resolver, which may not have bound the spawned
	// group to a Faction instance yet in the same frame as the spawn.
	// Falls through to GetFaction().GetFactionKey() if that's empty.
	protected string AllocateVehicleName(SCR_AIGroup crew)
	{
		string key = ResolveFactionKey(crew);
		if (key.IsEmpty())
		{
			key = "Unknown";
			Print(string.Format("[RP_Traffic] Could not resolve faction key for group %1 — plate will read 'Unknown_Car_N'. Check the group prefab's m_faction or wait for FactionManager binding.", crew), LogLevel.WARNING);
		}
		int next = 1;
		if (m_mFactionCounters.Contains(key))
			next = m_mFactionCounters.Get(key) + 1;
		m_mFactionCounters.Set(key, next);
		return string.Format("%1_Car_%2", key, next);
	}

	protected string ResolveFactionKey(SCR_AIGroup crew)
	{
		if (!crew)
			return "";
		string fromName = crew.GetFactionName();
		if (!fromName.IsEmpty())
			return fromName;
		Faction faction = crew.GetFaction();
		if (faction)
		{
			string fromKey = faction.GetFactionKey();
			if (!fromKey.IsEmpty())
				return fromKey;
		}
		return "";
	}

	// Tries the base position first; if blocked, walks around it in 8
	// cardinal directions at m_fOffsetSearchDistance. Returns true and
	// writes outPos if a clear spot was found.
	protected bool TryFindClearSpawnPos(vector basePos, out vector outPos)
	{
		if (IsSpotClear(basePos))
		{
			outPos = basePos;
			return true;
		}
		for (int i = 0; i < 8; i++)
		{
			float angleRad = i * 45.0 * Math.DEG2RAD;
			vector offset = Vector(Math.Cos(angleRad) * m_fOffsetSearchDistance, 0, Math.Sin(angleRad) * m_fOffsetSearchDistance);
			vector tryPos = basePos + offset;
			if (IsSpotClear(tryPos))
			{
				outPos = tryPos;
				return true;
			}
		}
		outPos = basePos;
		return false;
	}

	// Treats a position as blocked iff any Vehicle or character entity
	// lies within m_fClearanceRadiusMeters. Static props are ignored
	// (DYNAMIC-only query) so e.g. a parked but unowned vehicle would
	// block but a bench wouldn't.
	protected bool IsSpotClear(vector pos)
	{
		m_bClearCheck_Blocked = false;
		GetGame().GetWorld().QueryEntitiesBySphere(pos, m_fClearanceRadiusMeters, ClearQueryCheck, null, EQueryEntitiesFlags.DYNAMIC);
		return !m_bClearCheck_Blocked;
	}

	protected bool ClearQueryCheck(IEntity entity)
	{
		if (Vehicle.Cast(entity) || SCR_ChimeraCharacter.Cast(entity))
		{
			m_bClearCheck_Blocked = true;
			return false;  // stop iterating
		}
		return true;
	}

	protected IEntity SpawnEntityAt(ResourceName prefab, vector pos)
	{
		if (prefab.IsEmpty())
			return null;
		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = pos;
		return GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
	}

	protected IEntity SpawnEntityAtTransform(ResourceName prefab, vector mat[4])
	{
		if (prefab.IsEmpty())
			return null;
		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[0] = mat[0];
		params.Transform[1] = mat[1];
		params.Transform[2] = mat[2];
		params.Transform[3] = mat[3];
		return GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
	}

	// Forces the vehicle's main headlights on right after spawn and
	// leaves them on. Civilian AI traffic doesn't toggle lights on its
	// own, so without this every car looks "off" — useful at night and
	// harmless during the day. Mirrors the dispatch headlight-toggle
	// pattern (SCR_LightsPresenceUserAction.PerformAction). User param
	// is the vehicle itself since the crew hasn't boarded yet; the
	// action doesn't validate the user for this toggle.
	protected void TurnOnHeadlights(IEntity vehicle)
	{
		if (!vehicle)
			return;
		BaseActionsManagerComponent actionMgr = BaseActionsManagerComponent.Cast(vehicle.FindComponent(BaseActionsManagerComponent));
		if (!actionMgr)
			return;
		array<BaseUserAction> actions = {};
		actionMgr.GetActionsList(actions);
		foreach (BaseUserAction a : actions)
		{
			SCR_LightsPresenceUserAction headlights = SCR_LightsPresenceUserAction.Cast(a);
			if (!headlights)
				continue;
			string actionName = a.GetActionName();
			if (actionName.Contains("_State_Off"))
				return;  // already on
			headlights.PerformAction(vehicle, vehicle);
			return;
		}
	}

	protected AIWaypoint SpawnWaypoint(ResourceName prefab, vector pos)
	{
		IEntity ent = SpawnEntityAt(prefab, pos);
		if (!ent)
			return null;
		return AIWaypoint.Cast(ent);
	}

	// ----------------------------------------------------------------------
	// Plate registry (replicated)
	// ----------------------------------------------------------------------

	// Server-side: stash the plate and broadcast to clients. Vehicles
	// without an RplComponent (shouldn't happen for networked vehicles,
	// but be defensive) just live in the map under RplId(0) and won't
	// resolve from a remote proxy — fine for SP/host where IsServer
	// always matches.
	protected void RegisterPlate(IEntity vehicle, string plate)
	{
		if (!vehicle)
			return;
		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (!rpl)
		{
			Print(string.Format("[RP_Traffic] Vehicle %1 has no RplComponent — plate '%2' stored locally only.", vehicle, plate), LogLevel.WARNING);
			return;
		}
		RplId id = rpl.Id();
		m_mPlatesByRplId.Set(id, plate);
		Rpc(RpcDo_SetPlate, id, plate);
	}

	// External spawners (e.g. RP_CopVehicleSpawnerComponent) call this
	// to add their vehicles to the same registry the civ traffic loop
	// uses. Allocates "<factionKey>_Car_N" from the shared counter map
	// and registers + broadcasts. Server-only. Returns the assigned
	// plate (empty string on failure).
	string AllocateAndRegisterPlate(IEntity vehicle, string factionKey)
	{
		if (!Replication.IsServer() || !vehicle || factionKey.IsEmpty())
			return "";
		int next = 1;
		if (m_mFactionCounters.Contains(factionKey))
			next = m_mFactionCounters.Get(factionKey) + 1;
		m_mFactionCounters.Set(factionKey, next);
		string plate = string.Format("%1_Car_%2", factionKey, next);
		RegisterPlate(vehicle, plate);
		return plate;
	}

	// Server-only. Snapshots the plates of currently active spawns into
	// outPlates. Consumed by RP_PlateWatchlistComponent.MaintainBudget
	// to keep its watchlist scoped to live vehicles. Cheap — a copy of
	// strings, called only on pool-change events.
	void GetActivePlates(out array<string> outPlates)
	{
		if (!outPlates)
			return;
		outPlates.Clear();
		foreach (RP_TrafficActiveSpawn entry : m_aActiveSpawns)
		{
			if (entry && !entry.m_sName.IsEmpty())
				outPlates.Insert(entry.m_sName);
		}
	}

	// Server-only. Pushes the current active plate set to the watchlist
	// so it can re-budget. Called from SetTargetActiveCount and SpawnTick
	// only when the pool composition actually changed. Safe no-op if the
	// watchlist component isn't present.
	protected void NotifyWatchlistOfPoolChange()
	{
		if (!Replication.IsServer())
			return;
		RP_PlateWatchlistComponent watchlist = RP_PlateWatchlistComponent.GetInstance();
		if (!watchlist)
			return;
		array<string> active = {};
		GetActivePlates(active);
		watchlist.MaintainBudget(active);
	}

	// HUD/etc. call this with a (possibly remote-proxy) vehicle entity.
	// Returns the empty string if unknown — caller decides the fallback.
	string GetVehiclePlate(IEntity vehicle)
	{
		if (!vehicle)
			return "";
		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (!rpl)
			return "";
		return m_mPlatesByRplId.Get(rpl.Id());
	}

	// Server-only. Called via RP_PlayerRpcRelayComponent when a client
	// joins (the relay is the only RPC route that works for
	// client->server on a GameMode-hosted component; see the relay
	// header). Re-broadcasts every entry — redundant on already-synced
	// clients (they just re-write the same value); cost is one
	// (RplId, string) per active spawn, bounded by the pool size.
	void BroadcastAllPlates()
	{
		if (!Replication.IsServer())
			return;
		foreach (RplId id, string plate : m_mPlatesByRplId)
		{
			Rpc(RpcDo_SetPlate, id, plate);
		}
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetPlate(RplId id, string plate)
	{
		m_mPlatesByRplId.Set(id, plate);
	}
}
