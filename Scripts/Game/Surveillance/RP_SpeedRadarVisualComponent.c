/**
 * RP_SpeedRadarVisualComponent
 *
 * Drives the physical SpeedRadar prop's visual elements (LED, screen,
 * blinky beacon) to mirror the surveillance HUD state. Attached to the
 * SpeedRadar entity inside the police vehicle.
 *
 * Driven by RP_SurveillanceHUDComponent — the HUD pushes state
 * transitions in via SetState() so the prop visuals stay in lock-step
 * with the on-screen overlay (both toggle on the same ] press).
 *
 * Visual mapping:
 *   OFF       : LED off, Screen off, blinky despawned
 *   SCANNING  : LED on (steady), Screen off, blinky despawned
 *   FLASHING  : LED on, Screen off, blinky pulsing
 *   LOCKED    : LED on, Screen on, blinky solid
 *
 * Architecture:
 *   - LED and Screen are child entities under the radar prefab, each
 *     with its own MeshObject containing a single material slot. We
 *     swap that material at runtime via SCR_Global.SetMaterial(child,
 *     mat, false). Per-slot material swap on the parent radar's
 *     multi-slot MeshObject is not exposed in the public script API,
 *     which is why those elements live as separate child entities.
 *   - Blinky beacon is a PointLight prefab spawned/despawned per state,
 *     since it's a real light contribution rather than a material
 *     state.
 */

enum ERP_RadarVisualState
{
	OFF,
	SCANNING,
	FLASHING,
	LOCKED,
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Visual driver for the SpeedRadar prop. Swaps materials on the LED and Screen child entities and spawns a blinky beacon child light per HUD state. Attach to the SpeedRadar entity (usually nested as a child of a police vehicle).")]
class RP_SpeedRadarVisualComponentClass : ScriptComponentClass
{
}

class RP_SpeedRadarVisualComponent : ScriptComponent
{
	[Attribute(desc: "LED child mesh (.xob). Used to identify the LED child by walking children and matching their MeshObject resource — robust against runtime naming differences.", UIWidgets.ResourcePickerThumbnail, params: "xob")]
	protected ResourceName m_sLEDMeshXob;

	[Attribute(desc: "Material applied to the LED while a target is being tracked under the limit (red).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sLEDOnMaterial;

	[Attribute(desc: "Material applied to the LED while the HUD is up but no target is in the cone (green idle indicator).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sLEDIdleMaterial;

	[Attribute(desc: "Material applied to the LED while the HUD is off (dark/unlit variant).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sLEDOffMaterial;

	[Attribute(desc: "Screen child mesh (.xob). Used to identify the Screen child via MeshObject resource match.", UIWidgets.ResourcePickerThumbnail, params: "xob")]
	protected ResourceName m_sScreenMeshXob;

	[Attribute(desc: "Material applied to the Screen child during LOCKED state (lit, e.g. red).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sScreenOnMaterial;

	[Attribute(desc: "Material applied to the Screen child while not LOCKED (dark/unlit variant).", UIWidgets.ResourcePickerThumbnail, params: "emat")]
	protected ResourceName m_sScreenOffMaterial;

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

	[Attribute(defvalue: "{8C0F3E120000A000}UI/RP_SpeedRadarDisplay.layout", desc: "Layout containing an RTTextureWidget whose contents are rendered onto the screen child's material via $rendertarget.", UIWidgets.ResourcePickerThumbnail, params: "layout")]
	protected ResourceName m_sDisplayLayoutPath;

	protected ERP_RadarVisualState m_eState = ERP_RadarVisualState.OFF;
	protected IEntity m_LEDChild;
	protected IEntity m_ScreenChild;
	protected IEntity m_BlinkyLight;

	// Render-target widget tree: the RTTextureWidget at the root of the
	// layout becomes the entity's $rendertarget once SetRenderTarget()
	// is called — its TextWidget child is what actually renders onto
	// the screen child's material.
	protected Widget m_wDisplayRoot;
	protected RTTextureWidget m_wRTSurface;
	protected TextWidget m_wSpeedText;
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
		DestroyDisplay();
		StopLEDBlink();
		super.OnDelete(owner);
	}

	// Pushed by the HUD each Tick — value (or "—") to render on the
	// physical screen surface. The text is drawn whenever the radar is
	// not OFF; Color is left to the layout's default styling.
	void SetSpeedText(string text)
	{
		m_sSpeedText = text;
		if (m_wSpeedText)
			m_wSpeedText.SetText(m_sSpeedText);
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

	// Exposed so the HUD component can grab the screen child directly
	// and bind its own RTTextureWidget to it via SetRenderTarget.
	IEntity GetScreenChild()
	{
		if (!m_bChildrenResolved)
			ResolveChildren();
		return m_ScreenChild;
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
		if (!m_sScreenMeshXob.IsEmpty())
			m_ScreenChild = FindChildByMesh(owner, m_sScreenMeshXob);
		m_bChildrenResolved = true;
		if (!m_LEDChild)
			Print("[RP_SpeedRadarVisual] LED child not found under radar — LED swap will be skipped.", LogLevel.WARNING);
		if (!m_ScreenChild)
			Print("[RP_SpeedRadarVisual] Screen child not found under radar — Screen swap will be skipped.", LogLevel.WARNING);
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

	protected void ApplyDisplayForState()
	{
		if (m_eState == ERP_RadarVisualState.OFF)
		{
			DestroyDisplay();
			return;
		}
		EnsureDisplaySpawned();
	}

	// Builds the RT widget tree once and binds the RTTextureWidget to
	// the screen child via SetRenderTarget. Once bound, anything drawn
	// inside the RT widget (the TextWidget) is exposed to the screen
	// child's material as $rendertarget. The screen .emat must be
	// authored in Workbench to reference $rendertarget on its emissive
	// or color map for the text to be visible on the surface.
	protected void EnsureDisplaySpawned()
	{
		if (m_wDisplayRoot)
			return;
		if (m_sDisplayLayoutPath.IsEmpty() || !m_ScreenChild)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;
		m_wDisplayRoot = ws.CreateWidgets(m_sDisplayLayoutPath);
		if (!m_wDisplayRoot)
		{
			Print(string.Format("[RP_SpeedRadarVisual] Layout failed to load: %1", m_sDisplayLayoutPath), LogLevel.ERROR);
			return;
		}

		Widget rt = m_wDisplayRoot.FindAnyWidget("RTSurface");
		m_wRTSurface = RTTextureWidget.Cast(rt);
		if (!m_wRTSurface)
		{
			Print("[RP_SpeedRadarVisual] RTSurface widget not found — display will be inert.", LogLevel.ERROR);
			return;
		}
		// Bind the RT widget's contents as $rendertarget on the screen
		// child entity. The screen .emat references it via BCRMap so
		// the rendered widget appears as the surface's base color.
		m_wRTSurface.SetRenderTarget(m_ScreenChild);

		Widget t = m_wDisplayRoot.FindAnyWidget("SpeedText");
		m_wSpeedText = TextWidget.Cast(t);
		if (m_wSpeedText)
			m_wSpeedText.SetText(m_sSpeedText);

		// Force a refresh so the RT widget actually renders its children
		// to the texture. Without this, the texture can stay empty even
		// after SetRenderTarget binds it to the entity.
		m_wDisplayRoot.Update();
		m_wRTSurface.Update();
	}

	protected void DestroyDisplay()
	{
		if (m_wRTSurface && m_ScreenChild)
			m_wRTSurface.RemoveRenderTarget(m_ScreenChild);
		m_wRTSurface = null;
		if (!m_wDisplayRoot)
			return;
		m_wDisplayRoot.RemoveFromHierarchy();
		m_wDisplayRoot = null;
		m_wSpeedText = null;
	}

	protected void ApplyMaterialForState()
	{
		// Screen — solid on/off per HUD state. The radar's "lit" look comes
		// purely from the screen material's emissive in this baseline.
		ResourceName screenMat;
		if (m_eState == ERP_RadarVisualState.OFF)
			screenMat = m_sScreenOffMaterial;
		else
			screenMat = m_sScreenOnMaterial;
		ApplyMaterial(m_ScreenChild, screenMat);

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
