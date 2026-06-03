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
	protected int m_iNextUnitId = 1;

	// Per-client cache of "carrier -> their last-known radio entity". Lets
	// us keep emitting from a radio after it's been dropped, since the
	// inventory lookup obviously fails for entities no longer in inventory.
	// Refreshed every time inventory lookup succeeds.
	protected ref map<IEntity, IEntity> m_mCarrierToRadio = new map<IEntity, IEntity>();

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
			BroadcastSoundOnCarriersRadio(existing.GetCrewLeaderEntity(), "SOUND_DISPATCH_ONWAY");
			return;
		}

		// Else spawn a new one if under the cap.
		int currentCount = CountUnitsOfType(typeTag);
		if (currentCount >= def.m_iMaxSpawned)
		{
			Print(string.Format("[RP_Dispatch] Type '%1' at max (%2/%3), no available units to redispatch.", typeTag, currentCount, def.m_iMaxSpawned), LogLevel.WARNING);
			BroadcastSoundOnEachLocalPlayersRadio("SOUND_DISPATCH_UNABLE");
			return;
		}

		IEntity sp = GetSpawnPointEntity(def.m_sSpawnPointName);
		if (!sp)
		{
			Print(string.Format("[RP_Dispatch] Spawn point '%1' not registered for type '%2'.", def.m_sSpawnPointName, typeTag), LogLevel.ERROR);
			return;
		}

		RP_DispatchedUnit unit = SpawnUnit(def, sp);
		if (!unit)
			return;
		unit.m_vTarget = targetPos;
		EnterState(unit, ERP_DispatchState.BOARDING_FOR_DISPATCH);
		// AI agents + inventory items spawn over the next frame or two — defer the
		// audio trigger so GetCrewLeaderEntity() and inventory lookup succeed.
		GetGame().GetCallqueue().CallLater(DeferredOnWaySound, 1500, false, unit);
	}

	protected void DeferredOnWaySound(RP_DispatchedUnit unit)
	{
		if (!unit || !unit.IsAlive())
			return;
		BroadcastSoundOnCarriersRadio(unit.GetCrewLeaderEntity(), "SOUND_DISPATCH_ONWAY");
	}

	// ----------------------------------------------------------------------
	// Radio chatter
	// ----------------------------------------------------------------------
	// Audio plays through the radio prop in the carrier's inventory, not
	// directly on the carrier. Two trigger contexts:
	//   - "On the way" — carrier is the responding unit's crew leader.
	//   - "Unable" — no responding unit, so each client plays it on the
	//     radio in their own local player's inventory.

	protected void BroadcastSoundOnCarriersRadio(IEntity carrier, string eventName)
	{
		if (!carrier)
			return;
		RplComponent rpl = RplComponent.Cast(carrier.FindComponent(RplComponent));
		if (!rpl)
		{
			Print(string.Format("[RP_Dispatch] No RplComponent on %1, falling back to local play.", carrier), LogLevel.WARNING);
			PlaySoundOnCarriersRadioLocal(carrier, eventName);
			return;
		}
		Rpc(RpcDo_PlaySoundOnCarriersRadio, rpl.Id(), eventName);
		RpcDo_PlaySoundOnCarriersRadio(rpl.Id(), eventName);  // also play on host/server
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_PlaySoundOnCarriersRadio(RplId entityId, string eventName)
	{
		// Duplex playback: play on the carrier's radio (cop's voice emanating
		// from their location, positional 3D) AND on the local player's own
		// radio (the radio relay in their pocket). Net effect: nearby cops are
		// heard directly + via your radio; distant cops are only heard via
		// your radio.
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(entityId));
		IEntity carrier = null;
		if (rpl)
		{
			carrier = rpl.GetEntity();
			PlaySoundOnCarriersRadioLocal(carrier, eventName);
		}
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;
		IEntity localPlayer = pc.GetControlledEntity();
		if (!localPlayer || localPlayer == carrier)
			return;
		PlaySoundOnCarriersRadioLocal(localPlayer, eventName);
	}

	protected void BroadcastSoundOnEachLocalPlayersRadio(string eventName)
	{
		Rpc(RpcDo_PlaySoundOnLocalPlayersRadio, eventName);
		RpcDo_PlaySoundOnLocalPlayersRadio(eventName);  // also play on host/server
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_PlaySoundOnLocalPlayersRadio(string eventName)
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;
		PlaySoundOnCarriersRadioLocal(pc.GetControlledEntity(), eventName);
	}

	protected void PlaySoundOnCarriersRadioLocal(IEntity carrier, string eventName)
	{
		if (!carrier)
			return;
		// Prefer the radio currently in carrier's inventory; if not found,
		// fall back to the cached "last-known" radio for this carrier so a
		// dropped radio still emits from where it landed.
		IEntity radio = null;
		bool fromInventory = false;
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(carrier.FindComponent(SCR_InventoryStorageManagerComponent));
		if (inv)
		{
			array<typename> componentsQuery = { BaseRadioComponent };
			radio = inv.FindItemWithComponents(componentsQuery, EStoragePurpose.PURPOSE_ANY);
			if (radio)
			{
				m_mCarrierToRadio.Set(carrier, radio);
				fromInventory = true;
			}
		}
		if (!radio)
			radio = m_mCarrierToRadio.Get(carrier);
		if (!radio)
		{
			Print(string.Format("[RP_Dispatch] %1 has no radio (inventory empty + no cached radio) for event %2.", carrier, eventName), LogLevel.WARNING);
			return;
		}
		SoundComponent comm = SoundComponent.Cast(radio.FindComponent(SoundComponent));
		if (!comm)
		{
			Print(string.Format("[RP_Dispatch] Radio %1 has no SoundComponent for event %2.", radio, eventName), LogLevel.WARNING);
			return;
		}
		// Stowed inventory items often have a bogus world transform of
		// (0,0,0) since they're not in the scene graph — pin to the
		// carrier's transform so the audio emanates from the cop. Once
		// dropped, the radio is a real world entity with a valid transform,
		// so emit from the radio itself (continues playing from where it
		// landed even if the player walks away).
		vector transf[4];
		if (fromInventory)
			carrier.GetTransform(transf);
		else
			radio.GetTransform(transf);
		AudioHandle handle = comm.SoundEventTransform(eventName, transf);
		Print(string.Format("[RP_Dispatch] SoundEventTransform('%1') on radio %2 (fromInventory=%3) at %4 -> handle=%5", eventName, radio, fromInventory, transf[3], handle), LogLevel.NORMAL);
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

	protected RP_DispatchedUnit SpawnUnit(RP_DispatchGroupDefinition def, IEntity spawnPoint)
	{
		vector spawnPos = spawnPoint.GetOrigin();
		// Vehicle inherits the spawn point's full transform so it faces the
		// direction the marker was placed (otherwise it spawns at world
		// identity rotation regardless of the marker's facing).
		IEntity vehicle = SpawnEntityAtTransform(def.m_sVehiclePrefab, spawnPoint);
		if (!vehicle)
		{
			Print(string.Format("[RP_Dispatch] Failed to spawn vehicle prefab for type '%1'", def.m_sTypeTag), LogLevel.ERROR);
			return null;
		}
		vector crewPos = OffsetGround(spawnPos, 15, 0);
		IEntity crewEnt = SpawnEntityAt(def.m_sCrewGroupPrefab, crewPos);
		SCR_AIGroup crew = SCR_AIGroup.Cast(crewEnt);
		if (!crew)
		{
			Print(string.Format("[RP_Dispatch] Failed to spawn crew group for type '%1'", def.m_sTypeTag), LogLevel.ERROR);
			SCR_EntityHelper.DeleteEntityAndChildren(vehicle);
			return null;
		}

		RP_DispatchedUnit unit = new RP_DispatchedUnit();
		unit.m_iId = m_iNextUnitId++;
		unit.m_sTypeTag = def.m_sTypeTag;
		unit.m_Def = def;
		unit.m_Crew = crew;
		unit.m_Vehicle = vehicle;
		unit.m_vSpawnPoint = spawnPos;
		unit.m_eState = ERP_DispatchState.SPAWNED;
		unit.m_fStateChangedAt = GetWorldTimeSeconds();

		// Keep dispatched units out of the engine savegame. They're a
		// runtime spawn pool reconciled only while this manager is alive
		// (m_aUnits starts empty on a fresh server start), so anything the
		// previous session persisted reloads as orphaned standing cops and
		// parked cruisers with no manager — the same "AI standing around"
		// failure the traffic system already guards against. Excludes the
		// vehicle subtree (incl. supply-storage children, so cruiser supplies
		// don't float), the group container, and the deferred crew members.
		ExcludeFromPersistence(vehicle);
		ExcludeFromPersistence(crewEnt);
		ExcludeCrewMembers(crew);
		crew.GetOnAllDelayedEntitySpawned().Insert(ExcludeCrewMembers);

		m_aUnits.Insert(unit);
		Print(string.Format("[RP_Dispatch] Spawned %1#%2 (vehicle=%3 crew=%4)", def.m_sTypeTag, unit.m_iId, vehicle, crew), LogLevel.NORMAL);
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

		switch (unit.m_eState)
		{
			case ERP_DispatchState.SPAWNED:
				// Wait for first dispatch.
				break;

			case ERP_DispatchState.BOARDING_FOR_DISPATCH:
				// Primary trigger: all crew physically in the vehicle.
				// Failsafe is one-shot per state entry — once force-board
				// has fired, we wait for the natural IsAllCrewInVehicle
				// trigger (or the unit getting reaped) rather than tearing
				// down + re-issuing the boarding waypoint repeatedly.
				if (unit.IsAllCrewInVehicle())
					EnterState(unit, ERP_DispatchState.DRIVING_TO_TARGET);
				else if (elapsed >= def.m_fBoardingTimeSeconds && !unit.m_bForceBoardingActive)
				{
					Print(string.Format("[RP_Dispatch] %1#%2 boarding failsafe fired (%3s) — only %4/%5 boarded, force-boarding stragglers.", unit.m_sTypeTag, unit.m_iId, def.m_fBoardingTimeSeconds, unit.GetCrewInVehicleCount(), unit.GetCrewCount()), LogLevel.WARNING);
					unit.m_bForceBoardingActive = true;
					ForceBoardSeq_Start(unit);
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
				// Primary trigger: all crew physically out of the vehicle.
				// Failsafe: m_fDismountTimeSeconds expired without success.
				if (unit.GetCrewInVehicleCount() == 0)
					EnterState(unit, ERP_DispatchState.APPROACHING_ON_FOOT);
				else if (elapsed >= def.m_fDismountTimeSeconds)
				{
					Print(string.Format("[RP_Dispatch] %1#%2 dismount failsafe fired (%3s) — %4 still in vehicle, proceeding anyway.", unit.m_sTypeTag, unit.m_iId, def.m_fDismountTimeSeconds, unit.GetCrewInVehicleCount()), LogLevel.WARNING);
					EnterState(unit, ERP_DispatchState.APPROACHING_ON_FOOT);
				}
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
				if (boarded)
				{
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
				else if (elapsed >= def.m_fBoardingTimeSeconds && !unit.m_bForceBoardingActive)
				{
					Print(string.Format("[RP_Dispatch] %1#%2 return-boarding failsafe fired (%3s) — force-boarding stragglers.", unit.m_sTypeTag, unit.m_iId, def.m_fBoardingTimeSeconds), LogLevel.WARNING);
					unit.m_bForceBoardingActive = true;
					ForceBoardSeq_Start(unit);
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
		// Force-boarding is one-shot per state entry. Reset the flag so
		// future BOARDING states get a fresh failsafe attempt; suppression
		// applies only within a single state instance.
		unit.m_bForceBoardingActive = false;

		switch (next)
		{
			case ERP_DispatchState.BOARDING_FOR_DISPATCH:
				IssueGetIn(unit);
				break;

			case ERP_DispatchState.DRIVING_TO_TARGET:
				IssueMove(unit.m_Crew, unit.m_vTarget);
				SetSirenLights(unit, true);
				SetVehicleHeadlightsIfDark(unit);
				break;

			case ERP_DispatchState.DISMOUNTING:
				IssueGetOut(unit.m_Crew, unit.m_Vehicle);
				BroadcastSoundOnCarriersRadio(unit.GetCrewLeaderEntity(), "SOUND_DISPATCH_ARRIVING");
				break;

			case ERP_DispatchState.APPROACHING_ON_FOOT:
				IssueMove(unit.m_Crew, unit.m_vTarget);
				break;

			case ERP_DispatchState.LOITERING:
				ClearWaypoints(unit.m_Crew);
				BroadcastSoundOnCarriersRadio(unit.GetCrewLeaderEntity(), "SOUND_DISPATCH_ONSCENE");
				break;

			case ERP_DispatchState.BOARDING_TO_RETURN:
				IssueGetIn(unit);
				BroadcastSoundOnCarriersRadio(unit.GetCrewLeaderEntity(), "SOUND_DISPATCH_RTB");
				break;

			case ERP_DispatchState.RETURNING:
				IssueMove(unit.m_Crew, unit.m_vSpawnPoint);
				SetSirenLights(unit, false);
				SetVehicleHeadlightsIfDark(unit);
				break;

			case ERP_DispatchState.IDLE_AT_SPAWN:
				IssueGetOut(unit.m_Crew, unit.m_Vehicle);
				break;
		}

		Print(string.Format("[RP_Dispatch] %1#%2 -> %3", unit.m_sTypeTag, unit.m_iId, typename.EnumToString(ERP_DispatchState, next)), LogLevel.NORMAL);
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

	protected void IssueGetIn(RP_DispatchedUnit unit)
	{
		if (!unit || !unit.m_Crew || !unit.m_Vehicle)
			return;
		AIWaypoint wp = SpawnWaypoint(m_sGetInWaypointPrefab, unit.m_Vehicle.GetOrigin());
		if (!wp)
		{
			Print("[RP_Dispatch] GetIn waypoint prefab missing.", LogLevel.ERROR);
			return;
		}
		SCR_BoardingEntityWaypoint getIn = SCR_BoardingEntityWaypoint.Cast(wp);
		if (getIn)
			getIn.SetEntity(unit.m_Vehicle);
		ClearWaypoints(unit.m_Crew);
		unit.m_Crew.AddWaypoint(wp);
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

	// Force-board sequence — runs when the boarding failsafe trips because
	// crew couldn't reach the vehicle naturally (combat, distance,
	// obstacles). Two phases, both synchronous in the same tick:
	//   _Start    : register the vehicle with the group's usability table
	//   _Teleport : GetInVehicle each crew member into a free slot
	//               (PILOT first, leader to cargo so the AI driver drives),
	//               then CompleteAllWaypoints to fire the boarding-WP
	//               on-completion handlers that wire up the AI driver task.
	// State machine's natural IsAllCrewInVehicle trigger handles the
	// transition to DRIVING_TO_TARGET on the next tick.
	protected void ForceBoardSeq_Start(RP_DispatchedUnit unit)
	{
		if (!unit || !unit.m_Crew || !unit.m_Vehicle)
			return;

		// Don't touch the boarding waypoint here. Removing + re-issuing it
		// each failsafe cycle was making crew dismount as soon as the
		// task disappeared from the queue. We let the existing waypoint
		// auto-complete once GetInVehicle parents the crew into the seats.

		SCR_AIGroupUtilityComponent groupUtil = SCR_AIGroupUtilityComponent.Cast(unit.m_Crew.FindComponent(SCR_AIGroupUtilityComponent));
		SCR_AIVehicleUsageComponent vehUsage = SCR_AIVehicleUsageComponent.Cast(unit.m_Vehicle.FindComponent(SCR_AIVehicleUsageComponent));
		if (groupUtil && vehUsage)
		{
			groupUtil.AddUsableVehicle(vehUsage);
			Print(string.Format("[RP_Dispatch::ForceBoard] %1#%2 — registered vehicle as usable with group.", unit.m_sTypeTag, unit.m_iId), LogLevel.NORMAL);
		}
		else
		{
			Print(string.Format("[RP_Dispatch::ForceBoard] %1#%2 — could not register vehicle (groupUtil=%3 vehUsage=%4).", unit.m_sTypeTag, unit.m_iId, groupUtil, vehUsage), LogLevel.WARNING);
		}

		ForceBoardSeq_Teleport(unit);
	}

	protected void ForceBoardSeq_Teleport(RP_DispatchedUnit unit)
	{
		if (!unit || !unit.IsAlive())
			return;

		BaseCompartmentManagerComponent mgr = BaseCompartmentManagerComponent.Cast(unit.m_Vehicle.FindComponent(BaseCompartmentManagerComponent));
		if (!mgr)
		{
			Print(string.Format("[RP_Dispatch::ForceBoard] %1#%2 — vehicle has no BaseCompartmentManagerComponent.", unit.m_sTypeTag, unit.m_iId), LogLevel.ERROR);
			unit.m_bForceBoardingActive = false;
			return;
		}

		// Slot priority: PILOT, then TURRET, then CARGO. Failsafe time means
		// the vehicle is empty, so we don't filter by occupancy.
		array<BaseCompartmentSlot> allSlots = {};
		mgr.GetCompartments(allSlots);
		array<BaseCompartmentSlot> ordered = {};
		foreach (BaseCompartmentSlot s : allSlots) if (s && s.GetType() == ECompartmentType.PILOT) ordered.Insert(s);
		foreach (BaseCompartmentSlot s : allSlots) if (s && s.GetType() == ECompartmentType.TURRET) ordered.Insert(s);
		foreach (BaseCompartmentSlot s : allSlots) if (s && s.GetType() == ECompartmentType.CARGO) ordered.Insert(s);

		// Leader rides as passenger; non-leaders fill driver/turret first.
		IEntity leaderEnt = unit.m_Crew.GetLeaderEntity();
		array<AIAgent> agents = {};
		unit.m_Crew.GetAgents(agents);
		array<AIAgent> nonLeaders = {};
		AIAgent leaderAgent = null;
		foreach (AIAgent a : agents)
		{
			if (!a)
				continue;
			if (a.GetControlledEntity() == leaderEnt)
				leaderAgent = a;
			else
				nonLeaders.Insert(a);
		}
		array<AIAgent> seatingOrder = {};
		foreach (AIAgent nl : nonLeaders) seatingOrder.Insert(nl);
		if (leaderAgent)
			seatingOrder.Insert(leaderAgent);

		int forced = 0;
		int skipped = 0;
		int nextSlot = 0;
		foreach (AIAgent agent : seatingOrder)
		{
			IEntity ent = agent.GetControlledEntity();
			if (!ent)
				continue;
			if (ent.GetParent() == unit.m_Vehicle)
			{
				Print(string.Format("[RP_Dispatch::ForceBoard] %1 already parented to vehicle, skipping.", ent), LogLevel.NORMAL);
				skipped++;
				continue;
			}
			SCR_CompartmentAccessComponent access = SCR_CompartmentAccessComponent.Cast(ent.FindComponent(SCR_CompartmentAccessComponent));
			if (!access)
			{
				Print(string.Format("[RP_Dispatch::ForceBoard] Crew %1 has no SCR_CompartmentAccessComponent.", ent), LogLevel.WARNING);
				continue;
			}
			if (nextSlot >= ordered.Count())
			{
				Print(string.Format("[RP_Dispatch::ForceBoard] %1 — no more slots available.", ent), LogLevel.WARNING);
				break;
			}
			BaseCompartmentSlot target = ordered[nextSlot++];
			bool isLeader = (ent == leaderEnt);
			bool ok = access.GetInVehicle(unit.m_Vehicle, target, true, 0, ECloseDoorAfterActions.LEAVE_OPEN, false);
			Print(string.Format("[RP_Dispatch::ForceBoard] %1 (leader=%2) GetInVehicle(type=%3, teleport) -> %4", ent, isLeader, typename.EnumToString(ECompartmentType, target.GetType()), ok), LogLevel.NORMAL);
			if (ok)
				forced++;
		}
		Print(string.Format("[RP_Dispatch] %1#%2 force-board: %3 teleported, %4 already aboard.", unit.m_sTypeTag, unit.m_iId, forced, skipped), LogLevel.NORMAL);

		// The boarding waypoint doesn't auto-complete on a teleport-induced
		// parent change (it watches for the AI's natural boarding action).
		// Fire CompleteAllWaypoints now — crew is parented in, so the
		// completion handler runs the on-board side effects (driver-task
		// assignment, etc.) that we can't reproduce by hand and that
		// AddUsableVehicle alone isn't enough for.
		unit.m_Crew.CompleteAllWaypoints();
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

	// Toggles the vehicle's siren_lights user action only when the live
	// state (queried from the action's BaseLightManagerComponent) differs
	// from what we want. Reading state-of-truth from the engine each time
	// means a player jumping in and toggling the lights manually doesn't
	// desync our intent — next state transition reconciles correctly.
	protected void SetSirenLights(RP_DispatchedUnit unit, bool desiredOn)
	{
		if (!unit)
		{
			Print("[RP_Dispatch] SetSirenLights early-out: unit is null", LogLevel.WARNING);
			return;
		}
		if (!unit.m_Vehicle)
		{
			Print(string.Format("[RP_Dispatch] %1#%2 SetSirenLights early-out: vehicle is null", unit.m_sTypeTag, unit.m_iId), LogLevel.WARNING);
			return;
		}
		BaseActionsManagerComponent actionMgr = BaseActionsManagerComponent.Cast(unit.m_Vehicle.FindComponent(BaseActionsManagerComponent));
		if (!actionMgr)
		{
			Print(string.Format("[RP_Dispatch] Vehicle %1 has no BaseActionsManagerComponent for siren_lights.", unit.m_Vehicle), LogLevel.WARNING);
			return;
		}
		// Action name carries a state suffix that inverts with the prompt:
		//   '..._State_On'  -> action will turn it ON, so currently OFF
		//   '..._State_Off' -> action will turn it OFF, so currently ON
		// Reading state from the suffix is self-syncing — if a player jumps
		// in and toggles manually, the engine updates the action name and
		// our next call sees the right state. (BaseLightManagerComponent's
		// GetLightsEnabled is the master enable flag, not the on/off state.)
		array<BaseUserAction> actions = {};
		actionMgr.GetActionsList(actions);
		BaseUserAction lightAction = null;
		string actionName;
		foreach (BaseUserAction a : actions)
		{
			if (!a)
				continue;
			string n = a.GetActionName();
			if (n.StartsWith("siren_lights"))
			{
				lightAction = a;
				actionName = n;
				break;
			}
		}
		if (!lightAction)
		{
			Print(string.Format("[RP_Dispatch] No action prefixed 'siren_lights' on vehicle %1.", unit.m_Vehicle), LogLevel.WARNING);
			return;
		}
		bool currentlyOn = actionName.Contains("_State_Off");
		if (currentlyOn == desiredOn)
			return;
		ScriptedUserAction scripted = ScriptedUserAction.Cast(lightAction);
		if (!scripted)
		{
			Print(string.Format("[RP_Dispatch] siren_lights on %1 isn't ScriptedUserAction (class=%2).", unit.m_Vehicle, lightAction.Type()), LogLevel.WARNING);
			return;
		}
		IEntity user = unit.GetCrewLeaderEntity();
		if (!user)
			user = unit.m_Vehicle;
		scripted.PerformAction(unit.m_Vehicle, user);
		Print(string.Format("[RP_Dispatch] %1#%2 siren_lights -> %3", unit.m_sTypeTag, unit.m_iId, desiredOn), LogLevel.NORMAL);
	}

	// Turns on the vehicle's main headlights only when the sun is set
	// (TimeAndWeatherManagerEntity.IsSunSet covers dusk through dawn).
	// Doesn't turn them off — letting them stay on through DISMOUNTING /
	// LOITERING / BOARDING_TO_RETURN matches how a real police car would
	// idle on-scene with its lights on.
	protected void SetVehicleHeadlightsIfDark(RP_DispatchedUnit unit)
	{
		if (!unit || !unit.m_Vehicle)
			return;
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return;
		TimeAndWeatherManagerEntity tw = world.GetTimeAndWeatherManager();
		if (!tw)
			return;
		if (!tw.IsSunSet())
			return;  // bright enough — leave headlights alone
		BaseActionsManagerComponent actionMgr = BaseActionsManagerComponent.Cast(unit.m_Vehicle.FindComponent(BaseActionsManagerComponent));
		if (!actionMgr)
			return;
		array<BaseUserAction> actions = {};
		actionMgr.GetActionsList(actions);
		SCR_LightsPresenceUserAction headlights = null;
		string actionName;
		foreach (BaseUserAction a : actions)
		{
			SCR_LightsPresenceUserAction cast = SCR_LightsPresenceUserAction.Cast(a);
			if (cast)
			{
				headlights = cast;
				actionName = a.GetActionName();
				break;
			}
		}
		if (!headlights)
		{
			Print(string.Format("[RP_Dispatch] No SCR_LightsPresenceUserAction on vehicle %1.", unit.m_Vehicle), LogLevel.WARNING);
			return;
		}
		// Same suffix-as-state-of-truth pattern used for siren_lights.
		bool currentlyOn = actionName.Contains("_State_Off");
		if (currentlyOn)
			return;
		IEntity user = unit.GetCrewLeaderEntity();
		if (!user)
			user = unit.m_Vehicle;
		headlights.PerformAction(unit.m_Vehicle, user);
		Print(string.Format("[RP_Dispatch] %1#%2 headlights ON (sun is set)", unit.m_sTypeTag, unit.m_iId), LogLevel.NORMAL);
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

	// --- Persistence exclusion (mirrors RP_TrafficLoopComponent) ----------
	// Stops the engine savegame from tracking a dispatched entity and its
	// whole child hierarchy, and releases any data a prior session already
	// saved for it. No-op when no save system is running (GetInstance() is
	// null in workbench / save-less servers). Recursion reaches supply-storage
	// child entities so excluded cruisers don't leave floating supplies.
	protected void ExcludeFromPersistence(IEntity entity)
	{
		if (!entity)
			return;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence)
			return;
		ExcludeSubtree(persistence, entity);
	}

	protected void ExcludeSubtree(PersistenceSystem persistence, IEntity entity)
	{
		if (!entity)
			return;
		persistence.StopTracking(entity);
		IEntity child = entity.GetChildren();
		while (child)
		{
			ExcludeSubtree(persistence, child);
			child = child.GetSibling();
		}
	}

	// Crew characters spawn deferred and aren't children of the group
	// container, so excluding the group never reaches them. Wired to the
	// group's delayed-spawn invoker; idempotent, so also safe to call once
	// inline for any member already present. Signature matches
	// ScriptInvokerAIGroup.
	protected void ExcludeCrewMembers(SCR_AIGroup crew)
	{
		if (!crew)
			return;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence)
			return;
		array<AIAgent> agents = {};
		crew.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;
			IEntity character = agent.GetControlledEntity();
			if (character)
				ExcludeSubtree(persistence, character);
		}
	}

	// Spawns the prefab using the reference entity's full world transform
	// (position + rotation), so the spawned entity inherits the marker's
	// facing direction.
	protected IEntity SpawnEntityAtTransform(ResourceName prefab, IEntity refEntity)
	{
		if (prefab.IsEmpty() || !refEntity)
			return null;
		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		refEntity.GetTransform(params.Transform);
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
