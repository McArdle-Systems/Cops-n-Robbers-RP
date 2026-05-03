/**
 * RP_DispatchManagerComponent
 *
 * On the GameMode entity. Holds the catalogue of dispatchable group
 * definitions, manages spawn-on-demand up to per-type max, runs the
 * per-unit state machine ticks, and exposes Dispatch(typeTag, target)
 * as the entry point used by the HUD button.
 *
 * Server-authoritative — AI lives only on the server. RPCs in/out of
 * client UI route through here.
 */

[ComponentEditorProps(category: "RP/Dispatch", description: "Dispatch system controller. Attach to the GameMode entity.")]
class RP_DispatchManagerComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_DispatchManagerComponent : SCR_BaseGameModeComponent
{
	// --- Configuration ---

	[Attribute(desc: "Group definitions — one per dispatchable type.")]
	protected ref array<ref RP_DispatchGroupDefinition> m_aDefinitions;

	[Attribute(desc: "Move waypoint prefab (engine default Prefabs/AI/Waypoints/AIWaypoint_Move.et).", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sMoveWaypointPrefab;

	[Attribute(desc: "GetIn waypoint prefab (engine default Prefabs/AI/Waypoints/AIWaypoint_GetIn.et).", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sGetInWaypointPrefab;

	[Attribute(desc: "GetOut waypoint prefab — leave empty to skip explicit dismount waypoint and rely on clearing waypoints.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sGetOutWaypointPrefab;

	[Attribute(defvalue: "1", desc: "Tick interval in seconds for the state machine.")]
	protected float m_fTickIntervalSeconds;

	// --- Runtime ---

	protected static RP_DispatchManagerComponent s_Instance;
	protected ref array<ref RP_DispatchedUnit> m_aUnits = {};
	protected ref map<string, IEntity> m_mSpawnPoints = new map<string, IEntity>();
	protected bool m_bTickRunning;

	static RP_DispatchManagerComponent GetInstance() { return s_Instance; }

	// ----------------------------------------------------------------------
	// Lifecycle
	// ----------------------------------------------------------------------

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
			return;
		int periodMs = (int)(m_fTickIntervalSeconds * 1000);
		if (periodMs < 250)
			periodMs = 1000;
		GetGame().GetCallqueue().CallLater(Tick, periodMs, true);
		m_bTickRunning = true;
	}

	override void OnDelete(IEntity owner)
	{
		if (m_bTickRunning)
		{
			GetGame().GetCallqueue().Remove(Tick);
			m_bTickRunning = false;
		}
		// Clean up any live units.
		foreach (RP_DispatchedUnit unit : m_aUnits)
		{
			DespawnUnit(unit);
		}
		m_aUnits.Clear();
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	// ----------------------------------------------------------------------
	// Spawn point registry
	// ----------------------------------------------------------------------

	void RegisterSpawnPoint(string name, IEntity entity)
	{
		if (name.IsEmpty() || !entity)
			return;
		m_mSpawnPoints.Set(name, entity);
		Print(string.Format("[RP_Dispatch] Spawn point registered: %1 at %2", name, entity.GetOrigin()), LogLevel.NORMAL);
	}

	void UnregisterSpawnPoint(string name)
	{
		m_mSpawnPoints.Remove(name);
	}

	protected IEntity GetSpawnPointEntity(string name)
	{
		return m_mSpawnPoints.Get(name);
	}

	// ----------------------------------------------------------------------
	// Public dispatch entry point
	// ----------------------------------------------------------------------

	void Dispatch(string typeTag, vector targetPos)
	{
		if (!Replication.IsServer())
		{
			Rpc(RpcAsk_Dispatch, typeTag, targetPos);
			return;
		}
		DoDispatch(typeTag, targetPos);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Dispatch(string typeTag, vector targetPos)
	{
		DoDispatch(typeTag, targetPos);
	}

	protected void DoDispatch(string typeTag, vector targetPos)
	{
		RP_DispatchGroupDefinition def = FindDefinition(typeTag);
		if (!def)
		{
			Print(string.Format("[RP_Dispatch] No definition for type '%1'", typeTag), LogLevel.WARNING);
			return;
		}

		// Try to repurpose nearest available existing unit of this type.
		RP_DispatchedUnit existing = FindNearestAvailable(typeTag, targetPos);
		if (existing)
		{
			RedispatchExisting(existing, targetPos);
			return;
		}

		// Else spawn a new one if under the cap.
		int currentCount = CountUnitsOfType(typeTag);
		if (currentCount >= def.m_iMaxSpawned)
		{
			Print(string.Format("[RP_Dispatch] Type '%1' at max (%2/%3), no available units to redispatch.", typeTag, currentCount, def.m_iMaxSpawned), LogLevel.WARNING);
			return;
		}

		IEntity sp = GetSpawnPointEntity(def.m_sSpawnPointName);
		if (!sp)
		{
			Print(string.Format("[RP_Dispatch] Spawn point '%1' not registered for type '%2'.", def.m_sSpawnPointName, typeTag), LogLevel.ERROR);
			return;
		}

		RP_DispatchedUnit unit = SpawnUnit(def, sp.GetOrigin());
		if (!unit)
			return;
		unit.m_vTarget = targetPos;
		EnterState(unit, ERP_DispatchState.BOARDING_FOR_DISPATCH);
	}

	// ----------------------------------------------------------------------
	// Lookups
	// ----------------------------------------------------------------------

	protected RP_DispatchGroupDefinition FindDefinition(string typeTag)
	{
		if (!m_aDefinitions)
			return null;
		foreach (RP_DispatchGroupDefinition def : m_aDefinitions)
		{
			if (def && def.m_sTypeTag == typeTag)
				return def;
		}
		return null;
	}

	protected int CountUnitsOfType(string typeTag)
	{
		int n = 0;
		foreach (RP_DispatchedUnit u : m_aUnits)
		{
			if (u && u.IsAlive() && u.m_sTypeTag == typeTag)
				n++;
		}
		return n;
	}

	protected RP_DispatchedUnit FindNearestAvailable(string typeTag, vector targetPos)
	{
		RP_DispatchedUnit best = null;
		float bestDist = float.MAX;
		foreach (RP_DispatchedUnit u : m_aUnits)
		{
			if (!u || !u.IsAlive() || u.m_sTypeTag != typeTag || !u.IsAvailable())
				continue;
			float d = vector.Distance(u.GetCurrentPosition(), targetPos);
			if (d < bestDist)
			{
				bestDist = d;
				best = u;
			}
		}
		return best;
	}

	protected void RedispatchExisting(RP_DispatchedUnit unit, vector targetPos)
	{
		unit.m_vTarget = targetPos;

		switch (unit.m_eState)
		{
			case ERP_DispatchState.SPAWNED:
				EnterState(unit, ERP_DispatchState.BOARDING_FOR_DISPATCH);
				break;

			case ERP_DispatchState.BOARDING_TO_RETURN:
				// Don't interrupt boarding — let them finish, then redirect.
				unit.m_bRedispatchPending = true;
				unit.m_vPendingRedispatchTarget = targetPos;
				break;

			case ERP_DispatchState.RETURNING:
				// Already in vehicle, just redirect.
				EnterState(unit, ERP_DispatchState.DRIVING_TO_TARGET);
				break;

			case ERP_DispatchState.IDLE_AT_SPAWN:
				// Sitting at spawn (probably out of vehicle). Board, then drive.
				EnterState(unit, ERP_DispatchState.BOARDING_FOR_DISPATCH);
				break;

			default:
				Print(string.Format("[RP_Dispatch] RedispatchExisting called on non-available state %1, ignoring.", typename.EnumToString(ERP_DispatchState, unit.m_eState)), LogLevel.WARNING);
				break;
		}
	}

	// ----------------------------------------------------------------------
	// Spawn / despawn
	// ----------------------------------------------------------------------

	protected RP_DispatchedUnit SpawnUnit(RP_DispatchGroupDefinition def, vector spawnPos)
	{
		IEntity vehicle = SpawnEntityAt(def.m_sVehiclePrefab, spawnPos);
		if (!vehicle)
		{
			Print(string.Format("[RP_Dispatch] Failed to spawn vehicle prefab for type '%1'", def.m_sTypeTag), LogLevel.ERROR);
			return null;
		}
		vector crewPos = OffsetGround(spawnPos, 5, 0);
		IEntity crewEnt = SpawnEntityAt(def.m_sCrewGroupPrefab, crewPos);
		SCR_AIGroup crew = SCR_AIGroup.Cast(crewEnt);
		if (!crew)
		{
			Print(string.Format("[RP_Dispatch] Failed to spawn crew group for type '%1'", def.m_sTypeTag), LogLevel.ERROR);
			SCR_EntityHelper.DeleteEntityAndChildren(vehicle);
			return null;
		}

		RP_DispatchedUnit unit = new RP_DispatchedUnit();
		unit.m_sTypeTag = def.m_sTypeTag;
		unit.m_Def = def;
		unit.m_Crew = crew;
		unit.m_Vehicle = vehicle;
		unit.m_vSpawnPoint = spawnPos;
		unit.m_eState = ERP_DispatchState.SPAWNED;
		unit.m_fStateChangedAt = GetWorldTimeSeconds();
		m_aUnits.Insert(unit);
		Print(string.Format("[RP_Dispatch] Spawned %1 (vehicle=%2 crew=%3)", def.m_sTypeTag, vehicle, crew), LogLevel.NORMAL);
		return unit;
	}

	protected void DespawnUnit(RP_DispatchedUnit unit)
	{
		if (!unit)
			return;
		if (unit.m_Crew)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(unit.m_Crew);
			unit.m_Crew = null;
		}
		if (unit.m_Vehicle)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(unit.m_Vehicle);
			unit.m_Vehicle = null;
		}
	}

	// ----------------------------------------------------------------------
	// State machine tick
	// ----------------------------------------------------------------------

	protected void Tick()
	{
		float now = GetWorldTimeSeconds();
		for (int i = m_aUnits.Count() - 1; i >= 0; i--)
		{
			RP_DispatchedUnit unit = m_aUnits[i];
			if (!unit)
			{
				m_aUnits.Remove(i);
				continue;
			}
			if (!unit.IsAlive() && unit.m_eState != ERP_DispatchState.DESPAWN)
			{
				// Vehicle or crew destroyed externally — reap.
				EnterState(unit, ERP_DispatchState.DESPAWN);
			}
			TickUnit(unit, now);
			if (unit.m_eState == ERP_DispatchState.DESPAWN)
			{
				DespawnUnit(unit);
				m_aUnits.Remove(i);
			}
		}
	}

	protected void TickUnit(RP_DispatchedUnit unit, float now)
	{
		float elapsed = now - unit.m_fStateChangedAt;
		RP_DispatchGroupDefinition def = unit.m_Def;
		if (!def)
			return;

		// Per-tick diagnostic for in-progress units. Reduces to a single
		// line so we can watch the state machine without flooding the log.
		if (unit.m_eState != ERP_DispatchState.SPAWNED && unit.m_eState != ERP_DispatchState.IDLE_AT_SPAWN)
		{
			float distToTarget = vector.Distance(unit.GetCurrentPosition(), unit.m_vTarget);
			Print(string.Format("[RP_Dispatch] %1 state=%2 elapsed=%3 boarded=%4/%5 distToTarget=%6", unit.m_sTypeTag, typename.EnumToString(ERP_DispatchState, unit.m_eState), elapsed, unit.GetCrewInVehicleCount(), unit.GetCrewCount(), distToTarget), LogLevel.NORMAL);
		}

		switch (unit.m_eState)
		{
			case ERP_DispatchState.SPAWNED:
				// Wait for first dispatch.
				break;

			case ERP_DispatchState.BOARDING_FOR_DISPATCH:
				// Wait for ALL crew (driver included) before driving. Partial
				// mount means driver may not be in yet — vehicle won't move.
				if (unit.IsAllCrewInVehicle())
					EnterState(unit, ERP_DispatchState.DRIVING_TO_TARGET);
				else if (elapsed >= def.m_fBoardingTimeSeconds * 3)
				{
					Print(string.Format("[RP_Dispatch] %1 boarding timeout (%2s) — only %3/%4 boarded, driving anyway.", unit.m_sTypeTag, def.m_fBoardingTimeSeconds * 3, unit.GetCrewInVehicleCount(), unit.GetCrewCount()), LogLevel.WARNING);
					EnterState(unit, ERP_DispatchState.DRIVING_TO_TARGET);
				}
				break;

			case ERP_DispatchState.DRIVING_TO_TARGET:
				// Hold the dismount transition until we've spent at least
				// m_fMinDriveSeconds in this state. Stops "spawn next to
				// target → instant dismount" from skipping the drive entirely.
				if (elapsed >= def.m_fMinDriveSeconds
					&& vector.Distance(unit.GetCurrentPosition(), unit.m_vTarget) <= def.m_fDismountDistanceMeters)
					EnterState(unit, ERP_DispatchState.DISMOUNTING);
				break;

			case ERP_DispatchState.DISMOUNTING:
				if (elapsed >= def.m_fDismountTimeSeconds)
					EnterState(unit, ERP_DispatchState.APPROACHING_ON_FOOT);
				break;

			case ERP_DispatchState.APPROACHING_ON_FOOT:
				if (vector.Distance(unit.GetCurrentPosition(), unit.m_vTarget) <= def.m_fApproachRadiusMeters)
					EnterState(unit, ERP_DispatchState.LOITERING);
				break;

			case ERP_DispatchState.LOITERING:
				if (elapsed >= def.m_fLoiterSeconds)
					EnterState(unit, ERP_DispatchState.BOARDING_TO_RETURN);
				break;

			case ERP_DispatchState.BOARDING_TO_RETURN:
				// Same all-crew check as BOARDING_FOR_DISPATCH.
				bool boarded = unit.IsAllCrewInVehicle();
				if (boarded || elapsed >= def.m_fBoardingTimeSeconds * 3)
				{
					if (!boarded)
						Print(string.Format("[RP_Dispatch] %1 return-boarding timeout — returning anyway.", unit.m_sTypeTag), LogLevel.WARNING);
					if (unit.m_bRedispatchPending)
					{
						unit.m_bRedispatchPending = false;
						unit.m_vTarget = unit.m_vPendingRedispatchTarget;
						EnterState(unit, ERP_DispatchState.DRIVING_TO_TARGET);
					}
					else
					{
						EnterState(unit, ERP_DispatchState.RETURNING);
					}
				}
				break;

			case ERP_DispatchState.RETURNING:
				if (vector.Distance(unit.GetCurrentPosition(), unit.m_vSpawnPoint) <= def.m_fApproachRadiusMeters)
					EnterState(unit, ERP_DispatchState.IDLE_AT_SPAWN);
				break;

			case ERP_DispatchState.IDLE_AT_SPAWN:
				if (elapsed >= def.m_fCleanupTimeoutSeconds)
					EnterState(unit, ERP_DispatchState.DESPAWN);
				break;
		}
	}

	protected void EnterState(RP_DispatchedUnit unit, ERP_DispatchState next)
	{
		unit.m_eState = next;
		unit.m_fStateChangedAt = GetWorldTimeSeconds();

		switch (next)
		{
			case ERP_DispatchState.BOARDING_FOR_DISPATCH:
				IssueGetIn(unit.m_Crew, unit.m_Vehicle);
				break;

			case ERP_DispatchState.DRIVING_TO_TARGET:
				IssueMove(unit.m_Crew, unit.m_vTarget);
				break;

			case ERP_DispatchState.DISMOUNTING:
				IssueGetOut(unit.m_Crew, unit.m_Vehicle);
				break;

			case ERP_DispatchState.APPROACHING_ON_FOOT:
				IssueMove(unit.m_Crew, unit.m_vTarget);
				break;

			case ERP_DispatchState.LOITERING:
				ClearWaypoints(unit.m_Crew);
				break;

			case ERP_DispatchState.BOARDING_TO_RETURN:
				IssueGetIn(unit.m_Crew, unit.m_Vehicle);
				break;

			case ERP_DispatchState.RETURNING:
				IssueMove(unit.m_Crew, unit.m_vSpawnPoint);
				break;

			case ERP_DispatchState.IDLE_AT_SPAWN:
				IssueGetOut(unit.m_Crew, unit.m_Vehicle);
				break;
		}

		Print(string.Format("[RP_Dispatch] %1 -> %2", unit.m_sTypeTag, typename.EnumToString(ERP_DispatchState, next)), LogLevel.NORMAL);
	}

	// ----------------------------------------------------------------------
	// Waypoint / spawn helpers
	// ----------------------------------------------------------------------

	protected void IssueMove(SCR_AIGroup group, vector pos)
	{
		if (!group)
			return;
		AIWaypoint wp = SpawnWaypoint(m_sMoveWaypointPrefab, pos);
		if (!wp)
		{
			Print("[RP_Dispatch] Move waypoint prefab missing.", LogLevel.ERROR);
			return;
		}
		ClearWaypoints(group);
		group.AddWaypoint(wp);
	}

	protected void IssueGetIn(SCR_AIGroup group, IEntity vehicle)
	{
		if (!group || !vehicle)
			return;
		AIWaypoint wp = SpawnWaypoint(m_sGetInWaypointPrefab, vehicle.GetOrigin());
		if (!wp)
		{
			Print("[RP_Dispatch] GetIn waypoint prefab missing.", LogLevel.ERROR);
			return;
		}
		SCR_BoardingEntityWaypoint getIn = SCR_BoardingEntityWaypoint.Cast(wp);
		if (getIn)
			getIn.SetEntity(vehicle);
		ClearWaypoints(group);
		group.AddWaypoint(wp);
	}

	protected void IssueGetOut(SCR_AIGroup group, IEntity vehicle = null)
	{
		if (!group)
			return;
		if (m_sGetOutWaypointPrefab.IsEmpty())
		{
			Print("[RP_Dispatch] GetOut prefab not configured on manager — clearing waypoints only (crew won't actually dismount).", LogLevel.WARNING);
			ClearWaypoints(group);
			return;
		}
		// Spawn the waypoint at the vehicle's position rather than the group's
		// world origin. Group origin on SCR_AIGroup is often (0,0,0) since the
		// group is a logical container, not a physical entity.
		vector pos;
		if (vehicle)
			pos = vehicle.GetOrigin();
		else
			pos = group.GetOrigin();

		AIWaypoint wp = SpawnWaypoint(m_sGetOutWaypointPrefab, pos);
		if (!wp)
		{
			Print(string.Format("[RP_Dispatch] GetOut waypoint failed to spawn from prefab %1", m_sGetOutWaypointPrefab), LogLevel.ERROR);
			return;
		}
		Print(string.Format("[RP_Dispatch] GetOut waypoint issued at %1 (class=%2)", pos, wp.Type()), LogLevel.NORMAL);
		ClearWaypoints(group);
		group.AddWaypoint(wp);
	}

	protected void ClearWaypoints(SCR_AIGroup group)
	{
		if (!group)
			return;
		array<AIWaypoint> existing = {};
		group.GetWaypoints(existing);
		foreach (AIWaypoint wp : existing)
		{
			if (wp)
				group.RemoveWaypoint(wp);
		}
	}

	protected AIWaypoint SpawnWaypoint(ResourceName prefab, vector pos)
	{
		if (prefab.IsEmpty())
			return null;
		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = pos;
		IEntity ent = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		return AIWaypoint.Cast(ent);
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

	protected vector OffsetGround(vector origin, float distance, float bearingDeg)
	{
		float rad = bearingDeg * Math.DEG2RAD;
		vector pos = origin + Vector(Math.Sin(rad) * distance, 0, Math.Cos(rad) * distance);
		float surfaceY = SCR_TerrainHelper.GetTerrainY(pos, GetGame().GetWorld());
		pos[1] = surfaceY;
		return pos;
	}

	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;
		return world.GetWorldTime() / 1000.0;
	}
}
