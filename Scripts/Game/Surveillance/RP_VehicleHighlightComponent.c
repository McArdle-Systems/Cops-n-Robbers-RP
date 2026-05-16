/**
 * RP_VehicleHighlightComponent
 *
 * Attaches a "highlight" light prefab to a target vehicle for a
 * configurable lifetime. Each call to Highlight() either spawns the
 * light + parents it to the vehicle (first time) or refreshes the
 * expiry timer (subsequent calls). When the timer runs out without
 * a refresh, the light despawns.
 *
 * Designed to be polled at scan interval by RP_SurveillanceHUDComponent
 * — every tick that the cone scan finds a target, we Highlight() it.
 * When the target leaves the cone, Highlight() stops being called and
 * the light fades out after m_fLifetimeSec seconds.
 *
 * Limitation: Reforger's public script API doesn't expose runtime
 * intensity control on lights, so the despawn is a snap-off rather
 * than a smooth alpha fade. To get smooth fade, swap to a prefab
 * whose own animation handles flicker/pulse, or build a custom
 * Enfusion shader.
 *
 * Visibility: a worldspace light obeys depth — it does NOT render the
 * vehicle through walls. Through-cover visibility requires a
 * depth-test-disabled shader (separate work item).
 */

class RP_HighlightedVehicle
{
	IEntity m_Light;
	float m_fExpiryTime;
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Manager — attaches a highlight light prefab to detected vehicles. Attach to the GameMode entity.")]
class RP_VehicleHighlightComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_VehicleHighlightComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "8.0", desc: "Highlight lifetime in seconds. Refreshed by every Highlight() call so a vehicle stays lit while it remains a scan target. Set to 0 to disable expiry — lights persist until the entry is cleared on component shutdown.")]
	protected float m_fLifetimeSec;

	[Attribute(desc: "Light prefab attached to the highlighted vehicle. Pick anything with a visible PointLight / Flare / etc.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sLightPrefab;

	[Attribute(defvalue: "0 1.5 0", desc: "Local-space offset from vehicle origin where the light is mounted (typically above the roof).")]
	protected vector m_vLightOffset;

	protected static RP_VehicleHighlightComponent s_Instance;
	protected ref map<IEntity, ref RP_HighlightedVehicle> m_mHighlighted = new map<IEntity, ref RP_HighlightedVehicle>();
	protected bool m_bTickRunning;

	static RP_VehicleHighlightComponent GetInstance() { return s_Instance; }

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
	}

	override void OnDelete(IEntity owner)
	{
		StopTick();
		ClearAll();
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	void Highlight(IEntity vehicle)
	{
		if (!vehicle)
			return;
		float now = GetWorldTimeSeconds();
		RP_HighlightedVehicle existing = m_mHighlighted.Get(vehicle);
		if (existing)
		{
			existing.m_fExpiryTime = now + m_fLifetimeSec;
			return;
		}
		if (m_sLightPrefab.IsEmpty())
		{
			Print("[RP_Highlight] No light prefab configured (m_sLightPrefab) — highlight skipped.", LogLevel.WARNING);
			return;
		}
		Print(string.Format("[RP_Highlight] New highlight target: %1 at %2", vehicle, vehicle.GetOrigin()), LogLevel.NORMAL);
		IEntity lightEnt = SpawnLight(vehicle);
		if (!lightEnt)
			return;
		RP_HighlightedVehicle hv = new RP_HighlightedVehicle();
		hv.m_Light = lightEnt;
		hv.m_fExpiryTime = now + m_fLifetimeSec;
		m_mHighlighted.Set(vehicle, hv);
		EnsureTick();
	}

	protected IEntity SpawnLight(IEntity vehicle)
	{
		Resource res = Resource.Load(m_sLightPrefab);
		if (!res || !res.IsValid())
		{
			Print(string.Format("[RP_Highlight] Resource.Load failed for prefab %1", m_sLightPrefab), LogLevel.ERROR);
			return null;
		}
		vector tm[4];
		vehicle.GetTransform(tm);
		// Apply the local-space offset using the vehicle's basis vectors
		// so the light mounts above the vehicle even if it's rotated.
		vector worldPos = tm[3]
			+ tm[0] * m_vLightOffset[0]
			+ tm[1] * m_vLightOffset[1]
			+ tm[2] * m_vLightOffset[2];
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[0] = tm[0];
		params.Transform[1] = tm[1];
		params.Transform[2] = tm[2];
		params.Transform[3] = worldPos;
		IEntity light = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!light)
		{
			Print(string.Format("[RP_Highlight] SpawnEntityPrefab returned null for %1", m_sLightPrefab), LogLevel.ERROR);
			return null;
		}
		Print(string.Format("[RP_Highlight] Spawned light %1 at %2 (vehicle origin %3)", light, worldPos, tm[3]), LogLevel.NORMAL);
		// AddChild doesn't reliably propagate transform updates for this prefab
		// (the LightSourceVisualization tree has its own spawn semantics), so
		// we re-anchor the light's transform to the vehicle each Tick instead.
		return light;
	}

	protected void EnsureTick()
	{
		if (m_bTickRunning)
			return;
		// 20Hz — fast enough for the light to visually follow a moving
		// vehicle without obvious lag, light enough on CPU even with
		// many highlights live.
		GetGame().GetCallqueue().CallLater(Tick, 50, true);
		m_bTickRunning = true;
	}

	protected void StopTick()
	{
		if (!m_bTickRunning)
			return;
		GetGame().GetCallqueue().Remove(Tick);
		m_bTickRunning = false;
	}

	protected void Tick()
	{
		float now = GetWorldTimeSeconds();
		array<IEntity> toRemove = {};
		foreach (IEntity veh, RP_HighlightedVehicle hv : m_mHighlighted)
		{
			bool expired = m_fLifetimeSec > 0 && now >= hv.m_fExpiryTime;
			if (!veh || !hv || expired)
			{
				DespawnLight(hv);
				toRemove.Insert(veh);
				continue;
			}
			if (hv.m_Light)
			{
				vector tm[4];
				veh.GetTransform(tm);
				tm[3] = tm[3]
					+ tm[0] * m_vLightOffset[0]
					+ tm[1] * m_vLightOffset[1]
					+ tm[2] * m_vLightOffset[2];
				hv.m_Light.SetTransform(tm);
			}
		}
		foreach (IEntity veh : toRemove)
			m_mHighlighted.Remove(veh);
		if (m_mHighlighted.IsEmpty())
			StopTick();
	}

	protected void ClearAll()
	{
		foreach (IEntity veh, RP_HighlightedVehicle hv : m_mHighlighted)
			DespawnLight(hv);
		m_mHighlighted.Clear();
	}

	protected void DespawnLight(RP_HighlightedVehicle hv)
	{
		if (!hv || !hv.m_Light)
			return;
		SCR_EntityHelper.DeleteEntityAndChildren(hv.m_Light);
		hv.m_Light = null;
	}

	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;
		return world.GetWorldTime() / 1000.0;
	}
}
