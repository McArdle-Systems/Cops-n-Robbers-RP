/**
 * RP_TrafficLoopComponent — Phase 1 traffic POC.
 *
 * Manager component on the GameMode. World markers attach
 * RP_TrafficMarkerComponent to register themselves (vehicle / crew-spawn
 * / waypoint). After a short start delay, the manager spawns an AI crew
 * at the crew-spawn marker, boards them into the registered vehicle,
 * and assigns an AIWaypointCycle of Move waypoints at each registered
 * waypoint marker's position. The crew drives the loop indefinitely.
 *
 * Server-authoritative — AI runs only on the server.
 *
 * Marker setup:
 *   - 1 entity with type=VEHICLE      (the placed civilian vehicle)
 *   - 1 entity with type=CREW_SPAWN   (placed character-or-marker; gets
 *                                      deleted, crew prefab spawns there)
 *   - 2+ entities with type=WAYPOINT  (each with a unique m_iIndex, in
 *                                      cycle order)
 */

enum ERP_TrafficMarkerType
{
	VEHICLE,
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

[ComponentEditorProps(category: "RP/Traffic", description: "Traffic loop manager. Attach to the GameMode entity.")]
class RP_TrafficLoopComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_TrafficLoopComponent : SCR_BaseGameModeComponent
{
	[Attribute(desc: "AI crew group prefab — a 1-man civilian driver group.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sCrewGroupPrefab;

	[Attribute(defvalue: "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", desc: "Move waypoint prefab.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sMoveWaypointPrefab;

	[Attribute(defvalue: "{712F4795CF8B91C7}Prefabs/AI/Waypoints/AIWaypoint_GetIn.et", desc: "GetIn waypoint prefab.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sGetInWaypointPrefab;

	[Attribute(desc: "AIWaypointCycle prefab — pick the engine's stock cycle waypoint.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sCycleWaypointPrefab;

	[Attribute(defvalue: "2.0", desc: "Seconds to wait after game start before launching the loop. Gives marker components time to register.")]
	protected float m_fStartDelaySeconds;

	protected static RP_TrafficLoopComponent s_Instance;
	protected IEntity m_Vehicle;
	protected IEntity m_CrewSpawnMarker;
	protected ref array<IEntity> m_aWaypointMarkersByIndex = {};
	protected bool m_bStarted;

	static RP_TrafficLoopComponent GetInstance() { return s_Instance; }

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
			return;
		GetGame().GetCallqueue().CallLater(StartLoop, (int)(m_fStartDelaySeconds * 1000), false);
	}

	override void OnDelete(IEntity owner)
	{
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	void RegisterVehicle(IEntity vehicle)
	{
		m_Vehicle = vehicle;
		Print(string.Format("[RP_Traffic] Vehicle registered: %1", vehicle), LogLevel.NORMAL);
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
		if (!m_Vehicle)
		{
			Print("[RP_Traffic] No VEHICLE marker registered — loop won't start.", LogLevel.WARNING);
			return;
		}
		if (!m_CrewSpawnMarker)
		{
			Print("[RP_Traffic] No CREW_SPAWN marker registered — loop won't start.", LogLevel.WARNING);
			return;
		}
		array<IEntity> waypoints = {};
		foreach (IEntity wp : m_aWaypointMarkersByIndex)
		{
			if (wp)
				waypoints.Insert(wp);
		}
		if (waypoints.Count() < 2)
		{
			Print(string.Format("[RP_Traffic] Need at least 2 WAYPOINT markers, got %1 — loop won't start.", waypoints.Count()), LogLevel.WARNING);
			return;
		}
		m_bStarted = true;

		vector spawnPos = m_CrewSpawnMarker.GetOrigin();
		// The placed marker character is just a position reference — remove
		// it so the spawned AI driver doesn't collide with a duplicate.
		SCR_EntityHelper.DeleteEntityAndChildren(m_CrewSpawnMarker);
		m_CrewSpawnMarker = null;

		IEntity crewEnt = SpawnEntityAt(m_sCrewGroupPrefab, spawnPos);
		SCR_AIGroup crew = SCR_AIGroup.Cast(crewEnt);
		if (!crew)
		{
			Print("[RP_Traffic] Crew group failed to spawn — check m_sCrewGroupPrefab.", LogLevel.ERROR);
			return;
		}

		AIWaypoint getIn = SpawnWaypoint(m_sGetInWaypointPrefab, m_Vehicle.GetOrigin());
		if (getIn)
		{
			SCR_BoardingEntityWaypoint board = SCR_BoardingEntityWaypoint.Cast(getIn);
			if (board)
				board.SetEntity(m_Vehicle);
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
		foreach (IEntity wpMarker : waypoints)
		{
			AIWaypoint moveWp = SpawnWaypoint(m_sMoveWaypointPrefab, wpMarker.GetOrigin());
			if (moveWp)
				children.Insert(moveWp);
		}
		cycle.SetWaypoints(children);
		crew.AddWaypoint(cycleWp);

		Print(string.Format("[RP_Traffic] Loop launched with %1 waypoints.", children.Count()), LogLevel.NORMAL);
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

	protected AIWaypoint SpawnWaypoint(ResourceName prefab, vector pos)
	{
		IEntity ent = SpawnEntityAt(prefab, pos);
		if (!ent)
			return null;
		return AIWaypoint.Cast(ent);
	}
}
