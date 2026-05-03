/**
 * RP_AIMovePOCComponent
 *
 * Phase 0a AI movement POC. Attach to the GameMode entity in a test world.
 *
 * Spawns one infantry group and one crewed vehicle group on game start, then
 * exposes two DiagMenu actions ("Move Infantry To Me" / "Move Vehicle To Me")
 * that issue a Move waypoint to the player's current position.
 *
 * Server-authoritative. AI lives only on the server; clients see replicated
 * entities through the standard SCR_AIGroup replication path.
 */

[ComponentEditorProps(category: "RP/POC", description: "AI 'move to me' proving ground. Attach to GameMode entity.")]
class RP_AIMovePOCComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_AIMovePOCComponent : SCR_BaseGameModeComponent
{
	// --- Configuration ---

	[Attribute(desc: "Infantry group prefab to spawn for the foot squad test.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sInfantryGroupPrefab;

	[Attribute(desc: "Drivable vehicle prefab.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sVehiclePrefab;

	[Attribute(desc: "AI group prefab that will board the vehicle (1-2 AI is enough for POC).", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sVehicleCrewGroupPrefab;

	[Attribute(desc: "AIWaypoint_Move prefab (engine default in Prefabs/AI/Waypoints/).", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sMoveWaypointPrefab;

	[Attribute(desc: "AIWaypoint_GetIn prefab (engine default in Prefabs/AI/Waypoints/).", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sGetInWaypointPrefab;

	[Attribute(defvalue: "80", desc: "Distance in meters from the player at which to spawn AI on game start.")]
	protected float m_fSpawnDistance;

	[Attribute(defvalue: "10", desc: "Seconds to wait after first player joins before spawning POC AI.")]
	protected float m_fSpawnDelaySeconds;

	// --- Runtime State ---

	protected SCR_AIGroup m_InfantryGroup;
	protected SCR_AIGroup m_VehicleCrewGroup;
	protected IEntity m_VehicleEntity;
	protected bool m_bSpawned;
	protected bool m_bDiagRegistered;

	// DiagMenu IDs — must be unique across all addons. 0xCAFE prefix to avoid
	// collisions with engine + community mods.
	protected const int DIAG_MENU_ROOT = 0xCAFE0000;
	protected const int DIAG_SPAWN_AI = 0xCAFE0001;
	protected const int DIAG_MOVE_INFANTRY = 0xCAFE0002;
	protected const int DIAG_MOVE_VEHICLE = 0xCAFE0003;
	protected const int DIAG_DESPAWN_AI = 0xCAFE0004;

	// ----------------------------------------------------------------------
	// Lifecycle
	// ----------------------------------------------------------------------

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!GetGame().InPlayMode())
			return;

		RegisterDiagMenu();

		// Defer spawn until at least one player exists; check in a loop because
		// in singleplayer / hosted MP the local player may not exist on PostInit.
		GetGame().GetCallqueue().CallLater(TrySpawnInitial, 1000, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TrySpawnInitial);
		UnregisterDiagMenu();
		DespawnAll();
		super.OnDelete(owner);
	}

	// ----------------------------------------------------------------------
	// Initial spawn
	// ----------------------------------------------------------------------

	protected void TrySpawnInitial()
	{
		if (m_bSpawned)
		{
			GetGame().GetCallqueue().Remove(TrySpawnInitial);
			return;
		}

		// Server only — AI authority lives on the server.
		if (!Replication.IsServer())
		{
			GetGame().GetCallqueue().Remove(TrySpawnInitial);
			return;
		}

		IEntity player = GetAnyPlayerEntity();
		if (!player)
			return;

		GetGame().GetCallqueue().Remove(TrySpawnInitial);

		// Optional small delay so the world settles after player spawn.
		GetGame().GetCallqueue().CallLater(SpawnInitial, m_fSpawnDelaySeconds * 1000, false, player);
	}

	protected void SpawnInitial(IEntity referencePlayer)
	{
		if (m_bSpawned)
			return;
		if (!referencePlayer)
			referencePlayer = GetAnyPlayerEntity();
		if (!referencePlayer)
			return;

		vector playerPos = referencePlayer.GetOrigin();
		vector infantrySpawn = OffsetGroundPos(playerPos, m_fSpawnDistance, 0);
		vector vehicleSpawn = OffsetGroundPos(playerPos, m_fSpawnDistance, 120); // 120deg around

		m_InfantryGroup = SpawnGroup(m_sInfantryGroupPrefab, infantrySpawn);
		if (!m_InfantryGroup)
			Print("[RP_AIMovePOC] Failed to spawn infantry group (check m_sInfantryGroupPrefab attribute).", LogLevel.WARNING);

		// Vehicle: spawn the vehicle entity, then a crew group, then issue a
		// GetIn waypoint so the crew boards.
		m_VehicleEntity = SpawnEntityAt(m_sVehiclePrefab, vehicleSpawn);
		if (!m_VehicleEntity)
			Print("[RP_AIMovePOC] Failed to spawn vehicle entity (check m_sVehiclePrefab attribute).", LogLevel.WARNING);

		vector crewSpawn = OffsetGroundPos(vehicleSpawn, 5, 0);
		m_VehicleCrewGroup = SpawnGroup(m_sVehicleCrewGroupPrefab, crewSpawn);
		if (!m_VehicleCrewGroup)
			Print("[RP_AIMovePOC] Failed to spawn vehicle crew group (check m_sVehicleCrewGroupPrefab attribute).", LogLevel.WARNING);

		if (m_VehicleCrewGroup && m_VehicleEntity)
			IssueGetInWaypoint(m_VehicleCrewGroup, m_VehicleEntity);

		m_bSpawned = true;
		Print(string.Format("[RP_AIMovePOC] Spawned. Infantry=%1 Vehicle=%2 Crew=%3", m_InfantryGroup, m_VehicleEntity, m_VehicleCrewGroup), LogLevel.NORMAL);
	}

	// ----------------------------------------------------------------------
	// Move-to-player commands (called from DiagMenu on the local machine)
	// ----------------------------------------------------------------------

	void CmdMoveInfantryToPlayer()
	{
		IEntity player = GetLocalPlayerEntity();
		if (!player)
			return;
		// Route through RPC so the server (which owns the AI) does the work.
		Rpc(RpcAsk_MoveInfantry, player.GetOrigin());
	}

	void CmdMoveVehicleToPlayer()
	{
		IEntity player = GetLocalPlayerEntity();
		if (!player)
			return;
		Rpc(RpcAsk_MoveVehicle, player.GetOrigin());
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MoveInfantry(vector targetPos)
	{
		if (!m_InfantryGroup)
			return;
		IssueMoveWaypoint(m_InfantryGroup, targetPos);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MoveVehicle(vector targetPos)
	{
		if (!m_VehicleCrewGroup)
			return;
		IssueMoveWaypoint(m_VehicleCrewGroup, targetPos);
	}

	// ----------------------------------------------------------------------
	// Waypoint helpers (server only)
	// ----------------------------------------------------------------------

	protected void IssueMoveWaypoint(SCR_AIGroup group, vector targetPos)
	{
		if (!group)
			return;
		AIWaypoint wp = SpawnWaypoint(m_sMoveWaypointPrefab, targetPos);
		if (!wp)
		{
			Print("[RP_AIMovePOC] Move waypoint prefab missing or failed to spawn.", LogLevel.ERROR);
			return;
		}
		ClearGroupWaypoints(group);
		group.AddWaypoint(wp);
	}

	protected void IssueGetInWaypoint(SCR_AIGroup group, IEntity vehicle)
	{
		if (!group || !vehicle)
			return;
		AIWaypoint wp = SpawnWaypoint(m_sGetInWaypointPrefab, vehicle.GetOrigin());
		if (!wp)
		{
			Print("[RP_AIMovePOC] GetIn waypoint prefab missing.", LogLevel.ERROR);
			return;
		}
		SCR_BoardingEntityWaypoint getIn = SCR_BoardingEntityWaypoint.Cast(wp);
		if (getIn)
			getIn.SetEntity(vehicle);
		ClearGroupWaypoints(group);
		group.AddWaypoint(wp);
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

	protected void ClearGroupWaypoints(SCR_AIGroup group)
	{
		array<AIWaypoint> existing = {};
		group.GetWaypoints(existing);
		foreach (AIWaypoint wp : existing)
		{
			if (wp)
				group.RemoveWaypoint(wp);
		}
	}

	// ----------------------------------------------------------------------
	// Spawn helpers
	// ----------------------------------------------------------------------

	protected SCR_AIGroup SpawnGroup(ResourceName prefab, vector pos)
	{
		IEntity ent = SpawnEntityAt(prefab, pos);
		if (!ent)
			return null;
		SCR_AIGroup group = SCR_AIGroup.Cast(ent);
		return group;
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

	protected vector OffsetGroundPos(vector origin, float distance, float bearingDeg)
	{
		float rad = bearingDeg * Math.DEG2RAD;
		vector offset = Vector(Math.Sin(rad) * distance, 0, Math.Cos(rad) * distance);
		vector pos = origin + offset;
		// Drop to ground via SCR_WorldTools helper, available in the SCR_ scripts module.
		float surfaceY = SCR_TerrainHelper.GetTerrainY(pos, GetGame().GetWorld());
		pos[1] = surfaceY;
		return pos;
	}

	protected IEntity GetLocalPlayerEntity()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;
		return pc.GetControlledEntity();
	}

	protected IEntity GetAnyPlayerEntity()
	{
		// Find first live player entity via the player manager.
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return null;
		array<int> ids = {};
		pm.GetPlayers(ids);
		foreach (int id : ids)
		{
			IEntity ent = pm.GetPlayerControlledEntity(id);
			if (ent)
				return ent;
		}
		// Fallback: in workbench Play mode, the local player controller is set
		// even if PlayerManager hasn't registered them yet.
		return GetLocalPlayerEntity();
	}

	protected void DespawnAll()
	{
		if (m_InfantryGroup)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_InfantryGroup);
			m_InfantryGroup = null;
		}
		if (m_VehicleCrewGroup)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_VehicleCrewGroup);
			m_VehicleCrewGroup = null;
		}
		if (m_VehicleEntity)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_VehicleEntity);
			m_VehicleEntity = null;
		}
		m_bSpawned = false;
	}

	// ----------------------------------------------------------------------
	// DiagMenu integration
	// ----------------------------------------------------------------------

	protected void RegisterDiagMenu()
	{
		if (m_bDiagRegistered)
			return;

		DiagMenu.RegisterMenu(DIAG_MENU_ROOT, "RP > AI POC", "");
		DiagMenu.RegisterBool(DIAG_SPAWN_AI, "", "Spawn POC AI now", "RP > AI POC");
		DiagMenu.RegisterBool(DIAG_MOVE_INFANTRY, "", "Move Infantry To Me", "RP > AI POC");
		DiagMenu.RegisterBool(DIAG_MOVE_VEHICLE, "", "Move Vehicle To Me", "RP > AI POC");
		DiagMenu.RegisterBool(DIAG_DESPAWN_AI, "", "Despawn POC AI", "RP > AI POC");

		// Poll the menu — DiagMenu doesn't expose action callbacks directly.
		GetGame().GetCallqueue().CallLater(PollDiagMenu, 100, true);
		m_bDiagRegistered = true;
	}

	protected void UnregisterDiagMenu()
	{
		if (!m_bDiagRegistered)
			return;
		GetGame().GetCallqueue().Remove(PollDiagMenu);
		// DiagMenu does not currently expose Unregister; the entries persist for
		// the session. This is acceptable for a POC.
		m_bDiagRegistered = false;
	}

	protected void PollDiagMenu()
	{
		if (DiagMenu.GetBool(DIAG_SPAWN_AI))
		{
			DiagMenu.SetValue(DIAG_SPAWN_AI, 0);
			IEntity player = GetLocalPlayerEntity();
			if (player)
			{
				Rpc(RpcAsk_RespawnAt, player.GetOrigin());
			}
		}
		if (DiagMenu.GetBool(DIAG_MOVE_INFANTRY))
		{
			DiagMenu.SetValue(DIAG_MOVE_INFANTRY, 0);
			CmdMoveInfantryToPlayer();
		}
		if (DiagMenu.GetBool(DIAG_MOVE_VEHICLE))
		{
			DiagMenu.SetValue(DIAG_MOVE_VEHICLE, 0);
			CmdMoveVehicleToPlayer();
		}
		if (DiagMenu.GetBool(DIAG_DESPAWN_AI))
		{
			DiagMenu.SetValue(DIAG_DESPAWN_AI, 0);
			Rpc(RpcAsk_DespawnAll);
		}
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RespawnAt(vector playerPos)
	{
		DespawnAll();
		// Spawn at offsets from supplied position.
		vector infantrySpawn = OffsetGroundPos(playerPos, m_fSpawnDistance, 0);
		vector vehicleSpawn = OffsetGroundPos(playerPos, m_fSpawnDistance, 120);

		m_InfantryGroup = SpawnGroup(m_sInfantryGroupPrefab, infantrySpawn);
		m_VehicleEntity = SpawnEntityAt(m_sVehiclePrefab, vehicleSpawn);
		m_VehicleCrewGroup = SpawnGroup(m_sVehicleCrewGroupPrefab, OffsetGroundPos(vehicleSpawn, 5, 0));
		if (m_VehicleCrewGroup && m_VehicleEntity)
			IssueGetInWaypoint(m_VehicleCrewGroup, m_VehicleEntity);
		m_bSpawned = true;
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DespawnAll()
	{
		DespawnAll();
	}
}
