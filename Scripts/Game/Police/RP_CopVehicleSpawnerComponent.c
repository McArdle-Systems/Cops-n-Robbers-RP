/**
 * RP_CopVehicleSpawnerComponent
 *
 * Attach to the entity that hosts the interaction (e.g. a kiosk with an
 * ActionsManagerComponent + RP_SpawnCopVehicleUserAction). The vehicle is
 * spawned at a configurable target:
 *
 *   - If m_sSpawnPointEntityName is set, the world is queried for an entity
 *     of that name and its transform is used (position + rotation). This is
 *     the usual setup — interaction lives on one entity, spawn pad is a
 *     separate placed entity rotated to face the parking spot.
 *
 *   - Otherwise the spawn falls back to this component's owner transform
 *     plus m_vLocalSpawnOffset (local-space).
 *
 * Currently dispenses m_aVehiclePrefabs[0]. Adding a selection UI later
 * just means picking a different index — no schema change.
 *
 * Gating: if m_aAllowedFactionKeys is non-empty, only users whose
 * FactionAffiliationComponent key is in the list may use the spawner.
 *
 * Occupancy: a sphere query at the spawn position refuses dispatch if
 * another Vehicle is within m_fOccupancyRadius. Prevents stacking cars.
 *
 * Server-authoritative. RequestSpawn no-ops on clients.
 */

[BaseContainerProps()]
class RP_CopVehicleEntry
{
	[Attribute(desc: "Vehicle prefab to dispense.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	ResourceName m_sPrefab;

	[Attribute(desc: "Optional display name for a future selection UI. Unused while we always dispense entry [0].")]
	string m_sDisplayName;
}

[ComponentEditorProps(category: "RP/Police", description: "Cop vehicle spawner. Place on a marker entity at the PD motor pool; pair with RP_SpawnCopVehicleUserAction in a UserActionContext.")]
class RP_CopVehicleSpawnerComponentClass : ScriptComponentClass
{
}

class RP_CopVehicleSpawnerComponent : ScriptComponent
{
	[Attribute(desc: "Vehicle prefabs the spawner can dispense. First entry is used today; later entries are placeholders for a future selection UI. At least one entry required.")]
	protected ref array<ref RP_CopVehicleEntry> m_aVehiclePrefabs;

	[Attribute(desc: "Optional. Name of a placed entity to spawn vehicles AT. When set, the spawn uses that entity's full transform (position + rotation). Leave empty to fall back to this component's owner transform + m_vLocalSpawnOffset.")]
	protected string m_sSpawnPointEntityName;

	[Attribute(defvalue: "0 0 4", desc: "Fallback local-space offset from this component's owner when m_sSpawnPointEntityName is empty or the named entity isn't found.")]
	protected vector m_vLocalSpawnOffset;

	[Attribute(desc: "Faction keys allowed to use this spawner (e.g. 'US'). Empty = anyone can use.")]
	protected ref array<string> m_aAllowedFactionKeys;

	[Attribute(defvalue: "3.5", desc: "Sphere radius (m) checked at the spawn position. If any Vehicle is inside, the spawner refuses to dispense — prevents stacking cars on the pad.")]
	protected float m_fOccupancyRadius;

	// Scratch for QueryEntitiesBySphere callback.
	protected bool m_bOccupied;

	// True if the activating user passes the configured faction gate.
	bool IsAllowedFaction(IEntity user)
	{
		if (!m_aAllowedFactionKeys || m_aAllowedFactionKeys.IsEmpty())
			return true;
		if (!user)
			return false;

		string userKey = GetUserFactionKey(user);
		if (userKey.IsEmpty())
			return false;

		return m_aAllowedFactionKeys.Contains(userKey);
	}

	// True if the spawn pad has no Vehicle within m_fOccupancyRadius.
	bool IsPadClear()
	{
		vector spawnTm[4];
		ResolveSpawnTransform(spawnTm);
		return IsSpotClear(spawnTm[3]);
	}

	void RequestSpawn(IEntity user)
	{
		if (!Replication.IsServer())
			return;

		if (!IsAllowedFaction(user))
		{
			Print(string.Format("[RP_CopVehicleSpawner] %1 is not in an allowed faction — refusing.", user), LogLevel.NORMAL);
			return;
		}

		if (!m_aVehiclePrefabs || m_aVehiclePrefabs.IsEmpty())
		{
			Print("[RP_CopVehicleSpawner] No prefabs configured — nothing to spawn.", LogLevel.WARNING);
			return;
		}

		RP_CopVehicleEntry entry = m_aVehiclePrefabs[0];
		if (!entry || entry.m_sPrefab.IsEmpty())
		{
			Print("[RP_CopVehicleSpawner] First prefab entry is empty.", LogLevel.WARNING);
			return;
		}

		Resource res = Resource.Load(entry.m_sPrefab);
		if (!res || !res.IsValid())
		{
			Print(string.Format("[RP_CopVehicleSpawner] Failed to load prefab %1.", entry.m_sPrefab), LogLevel.ERROR);
			return;
		}

		vector spawnTm[4];
		ResolveSpawnTransform(spawnTm);

		if (!IsSpotClear(spawnTm[3]))
		{
			Print(string.Format("[RP_CopVehicleSpawner] Spawn pad at %1 is occupied — refusing.", spawnTm[3]), LogLevel.NORMAL);
			return;
		}

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[0] = spawnTm[0];
		params.Transform[1] = spawnTm[1];
		params.Transform[2] = spawnTm[2];
		params.Transform[3] = spawnTm[3];

		IEntity vehicle = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!vehicle)
		{
			Print(string.Format("[RP_CopVehicleSpawner] SpawnEntityPrefab returned null for %1.", entry.m_sPrefab), LogLevel.ERROR);
			return;
		}

		Print(string.Format("[RP_CopVehicleSpawner] Dispensed %1 for %2 at %3.", entry.m_sPrefab, user, params.Transform[3]), LogLevel.NORMAL);
	}

	// Fills outTm with the world transform we should spawn at:
	// named target entity if found, otherwise owner transform + local offset.
	protected void ResolveSpawnTransform(out vector outTm[4])
	{
		if (!m_sSpawnPointEntityName.IsEmpty())
		{
			IEntity target = GetGame().GetWorld().FindEntityByName(m_sSpawnPointEntityName);
			if (target)
			{
				target.GetTransform(outTm);
				return;
			}
			Print(string.Format("[RP_CopVehicleSpawner] Spawn point entity '%1' not found — falling back to owner + offset.", m_sSpawnPointEntityName), LogLevel.WARNING);
		}

		IEntity owner = GetOwner();
		vector ownerTm[4];
		owner.GetTransform(ownerTm);

		vector worldOffset = ownerTm[0] * m_vLocalSpawnOffset[0]
		                   + ownerTm[1] * m_vLocalSpawnOffset[1]
		                   + ownerTm[2] * m_vLocalSpawnOffset[2];

		outTm[0] = ownerTm[0];
		outTm[1] = ownerTm[1];
		outTm[2] = ownerTm[2];
		outTm[3] = ownerTm[3] + worldOffset;
	}

	protected bool IsSpotClear(vector pos)
	{
		m_bOccupied = false;
		GetGame().GetWorld().QueryEntitiesBySphere(pos, m_fOccupancyRadius, OccupancyQueryCheck, null, EQueryEntitiesFlags.DYNAMIC);
		return !m_bOccupied;
	}

	protected bool OccupancyQueryCheck(IEntity entity)
	{
		if (Vehicle.Cast(entity))
		{
			m_bOccupied = true;
			return false;
		}
		return true;
	}

	protected string GetUserFactionKey(IEntity user)
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
}
