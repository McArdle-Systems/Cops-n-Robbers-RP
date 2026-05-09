/**
 * RP_SpeedRadarVisualComponent
 *
 * Drives the physical SpeedRadar prop's visual elements (LED + blinky
 * beacon) and toggles power on the AG0 MFD slot that drives the screen.
 *
 * The screen is no longer a child of this radar entity — it lives as a
 * sibling slot on the parent vehicle so the AG0 MFD framework's parent
 * walk (Screen → Vehicle) finds the AG0_MFDManagerComponent in one hop
 * instead of going through this radar prop's slot indirection.
 *
 * Driven by RP_SurveillanceHUDComponent — the HUD pushes state via
 * SetState() so the prop visuals stay in lock-step with the on-screen
 * overlay.
 *
 * Visual mapping:
 *   OFF       : LED off, Screen MFD off, blinky despawned
 *   SCANNING  : LED on (steady, green idle / red on target), Screen MFD on, blinky despawned
 *   FLASHING  : LED rapid blink, Screen MFD on, blinky pulsing
 *   LOCKED    : LED rapid blink, Screen MFD on, blinky solid
 *
 * MFD control:
 *   Screen on/off is a single TogglePowerAction call on slot 0 of the
 *   parent vehicle's AG0_MFDManagerComponent. Idempotent — only flips
 *   when the desired state differs from IsMFDOn(0). Server-authoritative;
 *   replication carries state to all clients.
 */

enum ERP_RadarVisualState
{
	OFF,
	SCANNING,
	FLASHING,
	LOCKED,
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Visual driver for the SpeedRadar prop. Swaps LED material per state, spawns/despawns the blinky beacon, and toggles the parent vehicle's AG0 MFD slot 0 on/off in lock-step with the surveillance HUD.")]
class RP_SpeedRadarVisualComponentClass : ScriptComponentClass
{
}

class RP_SpeedRadarVisualComponent : ScriptComponent
{
	[Attribute(desc: "LED child mesh (.xob). Used to identify the LED child by walking children and matching their MeshObject resource.", UIWidgets.ResourcePickerThumbnail, params: "xob")]
	protected ResourceName m_sLEDMeshXob;

	[Attribute(desc: "Material applied to the LED while a target is being tracked under the limit (red).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sLEDOnMaterial;

	[Attribute(desc: "Material applied to the LED while the HUD is up but no target is in the cone (green idle indicator).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sLEDIdleMaterial;

	[Attribute(desc: "Material applied to the LED while the HUD is off (dark/unlit variant).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sLEDOffMaterial;

	[Attribute(desc: "Light prefab spawned as the blinky beacon (pulses during FLASHING, solid during LOCKED).", UIWidgets.ResourcePickerThumbnail, params: "et")]
	protected ResourceName m_sBlinkyPrefab;

	[Attribute(defvalue: "0 0.15 0", desc: "Local-space offset of the blinky beacon from the radar origin.")]
	protected vector m_vBlinkyOffset;

	[Attribute(defvalue: "0.06", desc: "Blinky 'off' phase in seconds (rapid).")]
	protected float m_fBlinkyOffSec;

	[Attribute(defvalue: "0.18", desc: "Blinky 'on' phase in seconds.")]
	protected float m_fBlinkyOnSec;

	[Attribute(defvalue: "0.08", desc: "LED rapid-blink 'on' phase during FLASHING/LOCKED (sec).")]
	protected float m_fLEDFastOnSec;

	[Attribute(defvalue: "0.08", desc: "LED rapid-blink 'off' phase during FLASHING/LOCKED (sec).")]
	protected float m_fLEDFastOffSec;

	[Attribute(defvalue: "0", desc: "MFD slot index on the parent vehicle to toggle for this radar's screen (0 = first registered slot).")]
	protected int m_iMFDSlotIndex;

	protected ERP_RadarVisualState m_eState = ERP_RadarVisualState.OFF;
	protected IEntity m_LEDChild;
	protected IEntity m_BlinkyLight;

	protected string m_sSpeedText = "—";

	protected bool m_bChildrenResolved;
	protected bool m_bBlinkyTickRunning;
	protected bool m_bBlinkyOn;
	protected float m_fBlinkyNextToggle;

	// LED blink runtime — only used during FLASHING/LOCKED. SCANNING is
	// solid (green if no target, red if tracking). OFF is dark.
	protected bool m_bLEDTickRunning;
	protected bool m_bLEDPhaseOn;
	protected float m_fLEDOnSec;
	protected float m_fLEDOffSec;
	protected float m_fLEDNextToggle;

	// Whether the cone scan currently has a target. Pushed by the HUD
	// each Tick. Decides green-vs-red during SCANNING.
	protected bool m_bHasTarget;

	// Cached parent-vehicle AG0 MFD manager. Resolved lazily on first
	// state transition (the radar is spawned as a slot child, so the
	// vehicle is GetOwner().GetParent() at that point).
	protected AG0_MFDManagerComponent m_MFDManager;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		// Defer child resolution one frame — children may not yet be
		// hooked into the hierarchy at component init time.
		GetGame().GetCallqueue().CallLater(ResolveChildren, 0, false);
	}

	override void OnDelete(IEntity owner)
	{
		StopBlinkyTick();
		DespawnBlinky();
		StopLEDBlink();
		super.OnDelete(owner);
	}

	// Pushed by the HUD each Tick — value (or "—") to render on the
	// physical screen surface. Currently a no-op stub: the AG0 MFD
	// framework owns the screen contents via its layout/page/text-widget
	// config. Wire this through a framework data feed (signals or a
	// custom MFD instance hook) once that integration is in.
	void SetSpeedText(string text)
	{
		m_sSpeedText = text;
	}

	void SetState(ERP_RadarVisualState state)
	{
		if (state == m_eState)
			return;
		m_eState = state;
		ApplyState();
	}

	ERP_RadarVisualState GetState()
	{
		return m_eState;
	}

	// Pushed each Tick by the HUD. True when a vehicle is in the cone.
	// Drives the LED's green-vs-red color during SCANNING.
	void SetHasTarget(bool hasTarget)
	{
		if (m_bHasTarget == hasTarget)
			return;
		m_bHasTarget = hasTarget;
		// Only re-apply if we're actually in a state where the flag
		// changes the LED visual (SCANNING). FLASHING/LOCKED ignore it.
		if (m_eState == ERP_RadarVisualState.SCANNING)
			ApplyMaterialForState();
	}

	protected void ResolveChildren()
	{
		if (m_bChildrenResolved)
			return;
		IEntity owner = GetOwner();
		if (!owner)
			return;
		if (!m_sLEDMeshXob.IsEmpty())
			m_LEDChild = FindChildByMesh(owner, m_sLEDMeshXob);
		m_bChildrenResolved = true;
		if (!m_LEDChild)
			Print("[RP_SpeedRadarVisual] LED child not found under radar — LED swap will be skipped.", LogLevel.WARNING);
		// Apply initial OFF visuals so radar isn't lit before HUD opens.
		ApplyMaterialForState();
	}

	// Walks the radar's direct children and returns the first whose
	// VObject (mesh resource) matches the requested .xob. Robust
	// against runtime naming — slot-spawned entities may not preserve
	// the slot name as their entity name, so we match on geometry
	// instead.
	protected IEntity FindChildByMesh(IEntity parent, ResourceName meshXob)
	{
		IEntity c = parent.GetChildren();
		while (c)
		{
			VObject vobj = c.GetVObject();
			if (vobj && vobj.GetResourceName() == meshXob)
				return c;
			c = c.GetSibling();
		}
		return null;
	}

	protected void ApplyState()
	{
		if (!m_bChildrenResolved)
			ResolveChildren();
		ApplyMaterialForState();
		ApplyBlinkyForState();
		ApplyDisplayForState();
	}

	// Toggles the parent vehicle's AG0 MFD slot to match the current
	// radar state — on for any non-OFF state, off for OFF. Idempotent
	// (compares wanted vs. IsMFDOn before flipping). Server-only;
	// replication carries state to clients.
	protected void ApplyDisplayForState()
	{
		AG0_MFDManagerComponent mgr = FindMFDManager();
		if (!mgr)
			return;
		bool wantOn = (m_eState != ERP_RadarVisualState.OFF);
		bool isOn = mgr.IsMFDOn(m_iMFDSlotIndex);
		if (wantOn == isOn)
			return;
		if (!Replication.IsServer())
			return;
		mgr.TogglePowerAction(m_iMFDSlotIndex);
	}

	protected AG0_MFDManagerComponent FindMFDManager()
	{
		if (m_MFDManager)
			return m_MFDManager;
		IEntity owner = GetOwner();
		if (!owner)
			return null;
		IEntity current = owner.GetParent();
		while (current)
		{
			AG0_MFDManagerComponent mgr = AG0_MFDManagerComponent.Cast(current.FindComponent(AG0_MFDManagerComponent));
			if (mgr)
			{
				m_MFDManager = mgr;
				return mgr;
			}
			current = current.GetParent();
		}
		return null;
	}

	protected void ApplyMaterialForState()
	{
		// LED — state-driven behavior:
		//   OFF                       -> dark, no blink
		//   SCANNING + no target      -> solid green (idle)
		//   SCANNING + tracking       -> solid red
		//   FLASHING / LOCKED         -> rapid red blink (over-limit)
		switch (m_eState)
		{
			case ERP_RadarVisualState.OFF:
				StopLEDBlink();
				ApplyMaterial(m_LEDChild, m_sLEDOffMaterial);
				break;
			case ERP_RadarVisualState.SCANNING:
				StopLEDBlink();
				if (m_bHasTarget)
					ApplyMaterial(m_LEDChild, m_sLEDOnMaterial);
				else
					ApplyMaterial(m_LEDChild, m_sLEDIdleMaterial);
				break;
			case ERP_RadarVisualState.FLASHING:
			case ERP_RadarVisualState.LOCKED:
				StartLEDBlink(m_fLEDFastOnSec, m_fLEDFastOffSec);
				break;
		}
	}

	// Idempotent: if already running, just updates the rate so a state
	// transition (slow -> fast) takes effect on the next phase toggle
	// without restarting the cycle.
	protected void StartLEDBlink(float onSec, float offSec)
	{
		m_fLEDOnSec = onSec;
		m_fLEDOffSec = offSec;
		if (m_bLEDTickRunning)
			return;
		m_bLEDPhaseOn = true;
		ApplyMaterial(m_LEDChild, m_sLEDOnMaterial);
		m_fLEDNextToggle = GetWorldTimeSeconds() + m_fLEDOnSec;
		GetGame().GetCallqueue().CallLater(LEDTick, 30, true);
		m_bLEDTickRunning = true;
	}

	protected void StopLEDBlink()
	{
		if (!m_bLEDTickRunning)
			return;
		GetGame().GetCallqueue().Remove(LEDTick);
		m_bLEDTickRunning = false;
	}

	protected void LEDTick()
	{
		float now = GetWorldTimeSeconds();
		if (now < m_fLEDNextToggle)
			return;
		m_bLEDPhaseOn = !m_bLEDPhaseOn;
		if (m_bLEDPhaseOn)
		{
			ApplyMaterial(m_LEDChild, m_sLEDOnMaterial);
			m_fLEDNextToggle = now + m_fLEDOnSec;
		}
		else
		{
			ApplyMaterial(m_LEDChild, m_sLEDOffMaterial);
			m_fLEDNextToggle = now + m_fLEDOffSec;
		}
	}

	protected void ApplyMaterial(IEntity ent, ResourceName mat)
	{
		if (!ent || mat.IsEmpty())
			return;
		SCR_Global.SetMaterial(ent, mat, false);
	}

	protected void ApplyBlinkyForState()
	{
		switch (m_eState)
		{
			case ERP_RadarVisualState.OFF:
			case ERP_RadarVisualState.SCANNING:
				StopBlinkyTick();
				DespawnBlinky();
				break;

			case ERP_RadarVisualState.FLASHING:
				EnsureBlinkySpawned();
				StartBlinkyTick();
				break;

			case ERP_RadarVisualState.LOCKED:
				EnsureBlinkySpawned();
				StopBlinkyTick();
				SetEntityVisible(m_BlinkyLight, true);
				break;
		}
	}

	protected void EnsureBlinkySpawned()
	{
		if (m_BlinkyLight)
			return;
		if (m_sBlinkyPrefab.IsEmpty())
			return;
		Resource res = Resource.Load(m_sBlinkyPrefab);
		if (!res || !res.IsValid())
		{
			Print(string.Format("[RP_SpeedRadarVisual] Resource.Load failed: %1", m_sBlinkyPrefab), LogLevel.ERROR);
			return;
		}
		IEntity owner = GetOwner();
		if (!owner)
			return;
		vector tm[4];
		owner.GetTransform(tm);
		vector worldPos = tm[3]
			+ tm[0] * m_vBlinkyOffset[0]
			+ tm[1] * m_vBlinkyOffset[1]
			+ tm[2] * m_vBlinkyOffset[2];
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[0] = tm[0];
		params.Transform[1] = tm[1];
		params.Transform[2] = tm[2];
		params.Transform[3] = worldPos;
		IEntity light = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!light)
			return;
		owner.AddChild(light, -1);
		m_BlinkyLight = light;
	}

	protected void DespawnBlinky()
	{
		if (!m_BlinkyLight)
			return;
		SCR_EntityHelper.DeleteEntityAndChildren(m_BlinkyLight);
		m_BlinkyLight = null;
	}

	protected void StartBlinkyTick()
	{
		if (m_bBlinkyTickRunning)
			return;
		m_bBlinkyOn = true;
		SetEntityVisible(m_BlinkyLight, true);
		m_fBlinkyNextToggle = GetWorldTimeSeconds() + m_fBlinkyOnSec;
		GetGame().GetCallqueue().CallLater(BlinkyTick, 30, true);
		m_bBlinkyTickRunning = true;
	}

	protected void StopBlinkyTick()
	{
		if (!m_bBlinkyTickRunning)
			return;
		GetGame().GetCallqueue().Remove(BlinkyTick);
		m_bBlinkyTickRunning = false;
	}

	protected void BlinkyTick()
	{
		if (!m_BlinkyLight)
			return;
		float now = GetWorldTimeSeconds();
		if (now < m_fBlinkyNextToggle)
			return;
		m_bBlinkyOn = !m_bBlinkyOn;
		SetEntityVisible(m_BlinkyLight, m_bBlinkyOn);
		if (m_bBlinkyOn)
			m_fBlinkyNextToggle = now + m_fBlinkyOnSec;
		else
			m_fBlinkyNextToggle = now + m_fBlinkyOffSec;
	}

	protected void SetEntityVisible(IEntity ent, bool visible)
	{
		if (!ent)
			return;
		if (visible)
			ent.SetFlags(EntityFlags.VISIBLE, true);
		else
			ent.ClearFlags(EntityFlags.VISIBLE, true);
	}

	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;
		return world.GetWorldTime() / 1000.0;
	}
}
