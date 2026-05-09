/**
 * RP_SurveillanceHUDComponent
 *
 * Toggleable Active Surveillance overlay (LPR + radar). Top-center,
 * non-modal — does not lock the cursor or block input. Two fields
 * (SPEED, PLATE) and a top LOCK indicator that surfaces only while a
 * lock is active.
 *
 * Speed lock state machine (all state resets on every HUD open):
 *   NORMAL   : both fields track the currently scanned target. Labels
 *              and values white. PLATE colors red iff watchlist flag
 *              (always false in current POC).
 *   FLASHING : entered the moment the target's speed crosses
 *              m_fSpeedLimitKmh. SPEED VALUE freezes at the trigger
 *              speed, PLATE freezes at the trigger vehicle's plate,
 *              both fields colored red, AND the entire field row
 *              blinks (rapid off / longer on) for m_fFlashDurationSec
 *              to grab attention. Peak speed for the locked vehicle
 *              is tracked silently across this window.
 *   LOCKED   : snap-to-peak. SPEED VALUE shows the highest speed
 *              observed for the locked vehicle and only ever rises.
 *              PLATE stays at that vehicle's plate. Both fields red,
 *              steady. LOCK indicator visible up top. Radar only
 *              updates while the locked vehicle is in the cone — out
 *              of cone = no peak update, back in cone = tracking
 *              resumes. Only way to release the lock is to close +
 *              reopen the HUD.
 *
 * Gating: only available while seated in a vehicle that carries
 * RP_PoliceVehicleComponent. Pressing the toggle outside a police
 * vehicle is a no-op; if the player exits a police vehicle while the
 * HUD is up, the HUD auto-hides and tears down.
 *
 * Detection: forward cone from the cop car's transform. Vehicles
 * within m_fConeRangeMeters and m_fConeHalfAngleDeg of forward are
 * candidates; the closest is the active target.
 *
 * POC stubs: GetVehiclePlate uses the entity's workbench name as a
 * stand-in plate; IsPlateFlagged always returns false. Wire these up
 * once a real plate registry lands.
 *
 * Toggled by a Bohemia input action (default RP_ToggleSurveillance,
 * KC_RBRACKET — "]"). Action defined in chimeraInputCommon.conf and
 * surfaced under the CNR keybinding category.
 */

enum ERP_SpeedState
{
	NORMAL,
	FLASHING,
	LOCKED,
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Tag — marks a vehicle as a police cruiser. Surveillance HUD only activates while seated in a tagged vehicle.")]
class RP_PoliceVehicleComponentClass : ScriptComponentClass
{
}

class RP_PoliceVehicleComponent : ScriptComponent
{
}

[ComponentEditorProps(category: "RP/Audio", description: "Cop-equipment audio bank holder. Attach to a police vehicle and configure with the equipment .acp (radar, MDT, etc.). The surveillance HUD plays through this on LOCK.")]
class RP_CopAudioComponentClass : SoundComponentClass
{
}

class RP_CopAudioComponent : SoundComponent
{
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Toggleable surveillance HUD overlay. Attach to the GameMode entity.")]
class RP_SurveillanceHUDComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_SurveillanceHUDComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "RP_ToggleSurveillance", desc: "Input action that toggles the HUD.")]
	protected string m_sToggleAction;

	[Attribute(defvalue: "{AB95DA9834D81521}UI/RP_SurveillanceHUD.layout", desc: "Layout for the HUD overlay.", UIWidgets.ResourcePickerThumbnail, params: "layout")]
	protected ResourceName m_sLayoutPath;

	[Attribute(defvalue: "22.5", desc: "Cone half-angle in degrees from the cop car's forward direction. Total cone width is 2x this.")]
	protected float m_fConeHalfAngleDeg;

	[Attribute(defvalue: "50.0", desc: "Cone range in meters.")]
	protected float m_fConeRangeMeters;

	[Attribute(defvalue: "0.25", desc: "Scan interval in seconds. Drives the cone scan, the in-cop-vehicle gate check, and the speed-lock state machine.")]
	protected float m_fScanIntervalSeconds;

	[Attribute(defvalue: "50.0", desc: "Speed limit (km/h). At or below = white, above = triggers FLASHING.")]
	protected float m_fSpeedLimitKmh;

	[Attribute(defvalue: "1.5", desc: "Duration in seconds of the warning flash before LOCK engages.")]
	protected float m_fFlashDurationSec;

	[Attribute(defvalue: "0.06", desc: "Flash 'off' phase duration in seconds (rapid).")]
	protected float m_fFlashOffSec;

	[Attribute(defvalue: "0.24", desc: "Flash 'on' phase duration in seconds (longer steady).")]
	protected float m_fFlashOnSec;

	[Attribute(defvalue: "SOUND_RADAR_BEEPING", desc: "Sound event triggered the moment LOCK engages. Empty = silent. Length is controlled on the audio side (BankWorks event) since the script API has no StopSound.")]
	protected string m_sLockSoundEvent;

	[Attribute(defvalue: "radar_speed_kmh", desc: "Vehicle signal name updated each Tick with the current radar reading. Bind this same name in the radar screen's AG0_MFDTextConfig.SignalName so the MFD framework substitutes it into the FormatString.")]
	protected string m_sSpeedSignalName;

	protected Widget m_wRoot;
	protected bool m_bVisible;
	protected bool m_bActionRegistered;
	protected ref array<IEntity> m_aQueryResults = {};

	// Speed-lock state. Reset on every Show().
	protected ERP_SpeedState m_eSpeedState;
	protected IEntity m_LockedVehicle;
	protected float m_fTriggerSpeedKmh;
	protected float m_fPeakSpeedKmh;
	protected string m_sLockedPlate;
	protected float m_fFlashEndTime;

	// Flash-animation runtime
	protected bool m_bFlashTickRunning;
	protected bool m_bFlashOn;
	protected float m_fFlashNextToggle;

	// Cached radar visual driver — discovered once per Show() by walking
	// the cop car's children. Cleared on Hide() so a different cop car
	// re-resolves on next open.
	protected RP_SpeedRadarVisualComponent m_RadarVisual;

	// Cached signal manager + signal index for the cop vehicle's speed
	// readout. The radar screen's AG0_MFDTextConfig binds the same
	// signal name so the framework substitutes the value into its
	// FormatString. Cleared on Hide() so re-Show on a different cop car
	// re-resolves.
	protected SignalsManagerComponent m_SignalsMgr;
	protected int m_iSpeedSignalIdx = -1;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		GetGame().GetCallqueue().CallLater(TryClientInit, 250, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryClientInit);
		GetGame().GetCallqueue().Remove(Tick);
		StopFlashTick();
		UnregisterAction();
		DestroyWidget();
		super.OnDelete(owner);
	}

	protected void TryClientInit()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;
		GetGame().GetCallqueue().Remove(TryClientInit);
		RegisterAction();
	}

	protected void RegisterAction()
	{
		if (m_bActionRegistered)
			return;
		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;
		input.AddActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleAction);
		m_bActionRegistered = true;
		Print(string.Format("[RP_Surveillance] HUD toggle bound to action '%1'", m_sToggleAction), LogLevel.NORMAL);
	}

	protected void UnregisterAction()
	{
		if (!m_bActionRegistered)
			return;
		InputManager input = GetGame().GetInputManager();
		if (input)
			input.RemoveActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleAction);
		m_bActionRegistered = false;
	}

	protected void OnToggleAction()
	{
		if (m_bVisible)
		{
			Hide();
			DestroyWidget();
			return;
		}
		if (!GetPlayerCopVehicle())
		{
			Print("[RP_Surveillance] Toggle ignored — not seated in a police vehicle.", LogLevel.NORMAL);
			return;
		}
		Show();
	}

	protected void Show()
	{
		if (!m_wRoot)
		{
			WorkspaceWidget ws = GetGame().GetWorkspace();
			if (!ws)
				return;
			m_wRoot = ws.CreateWidgets(m_sLayoutPath);
			if (!m_wRoot)
			{
				Print(string.Format("[RP_Surveillance] Layout failed to load: %1", m_sLayoutPath), LogLevel.ERROR);
				return;
			}
			SetText("LockIndicator", "LOCK");
			SetText("SpeedLabel", "SPEED");
			SetText("PlateLabel", "PLATE");
			// Lock indicator is permanently red; visibility is what we toggle.
			SetWidgetColor("LockIndicator", 0xFFFF0000);
		}
		ResetLockState();
		m_wRoot.SetVisible(true);
		m_bVisible = true;
		IEntity copCar = GetPlayerCopVehicle();
		m_RadarVisual = FindRadarVisual(copCar);
		EnsureSpeedSignal(copCar);
		PushRadarState(ERP_RadarVisualState.SCANNING);
		PushRadarSpeedText("—");
		PushSpeedSignal(0);
		GetGame().GetCallqueue().CallLater(Tick, (int)(m_fScanIntervalSeconds * 1000), true);
		Tick();
	}

	protected void Hide()
	{
		if (m_wRoot)
			m_wRoot.SetVisible(false);
		GetGame().GetCallqueue().Remove(Tick);
		StopFlashTick();
		// Zero the signal so the screen reads 0 km/h while powered off.
		PushSpeedSignal(0);
		PushRadarState(ERP_RadarVisualState.OFF);
		m_RadarVisual = null;
		m_SignalsMgr = null;
		m_iSpeedSignalIdx = -1;
		m_bVisible = false;
	}

	protected void DestroyWidget()
	{
		if (!m_wRoot)
			return;
		m_wRoot.RemoveFromHierarchy();
		m_wRoot = null;
	}

	// Resets the speed-lock state machine and the visible HUD back to
	// defaults. Called on every Show() so close+reopen is a clean slate.
	protected void ResetLockState()
	{
		m_eSpeedState = ERP_SpeedState.NORMAL;
		m_LockedVehicle = null;
		m_fTriggerSpeedKmh = 0;
		m_fPeakSpeedKmh = 0;
		m_sLockedPlate = "";
		m_fFlashEndTime = 0;
		StopFlashTick();
		SetText("SpeedValue", "—");
		SetText("PlateValue", "—");
		SetFieldRed("Speed", false);
		SetFieldRed("Plate", false);
		SetFieldVisible("Speed", true);
		SetFieldVisible("Plate", true);
		SetWidgetVisible("LockIndicator", false);
	}

	protected void Tick()
	{
		IEntity copCar = GetPlayerCopVehicle();
		if (!copCar)
		{
			// Player exited the cop vehicle — auto hide & remove.
			Hide();
			DestroyWidget();
			return;
		}

		IEntity target = FindVehicleInCone(copCar);
		float currentSpeedKmh = 0;
		string currentPlate = "—";
		if (target)
		{
			currentSpeedKmh = GetVehicleSpeedKmh(target);
			currentPlate = GetVehiclePlate(target);

			// Promote into the highlight registry — refreshes lifetime so the
			// light stays attached while the vehicle remains in the cone.
			RP_VehicleHighlightComponent hl = RP_VehicleHighlightComponent.GetInstance();
			if (hl)
				hl.Highlight(target);
		}

		float now = GetWorldTimeSeconds();

		switch (m_eSpeedState)
		{
			case ERP_SpeedState.NORMAL:
			{
				if (target && IsSpeedOverLimit(currentSpeedKmh))
				{
					// Trigger — freeze display at trigger speed/plate, lock to
					// this specific vehicle, kick off flash.
					m_LockedVehicle = target;
					m_fTriggerSpeedKmh = currentSpeedKmh;
					m_fPeakSpeedKmh = currentSpeedKmh;
					m_sLockedPlate = currentPlate;
					m_fFlashEndTime = now + m_fFlashDurationSec;
					m_eSpeedState = ERP_SpeedState.FLASHING;
					string trigText = string.Format("%1 km/h", Math.Round(m_fTriggerSpeedKmh));
					SetText("SpeedValue", trigText);
					SetText("PlateValue", m_sLockedPlate);
					SetFieldRed("Speed", true);
					SetFieldRed("Plate", true);
					StartFlashTick();
					PlayLockSound(copCar);
					PushRadarState(ERP_RadarVisualState.FLASHING);
					PushRadarSpeedText(trigText);
					PushSpeedSignal(m_fTriggerSpeedKmh);
					break;
				}
				if (target)
				{
					string curText = string.Format("%1 km/h", Math.Round(currentSpeedKmh));
					SetText("SpeedValue", curText);
					SetText("PlateValue", currentPlate);
					SetFieldRed("Speed", false);
					SetFieldRed("Plate", IsPlateFlagged(currentPlate));
					PushRadarSpeedText(curText);
					PushSpeedSignal(currentSpeedKmh);
				}
				else
				{
					SetText("SpeedValue", "—");
					SetText("PlateValue", "—");
					SetFieldRed("Speed", false);
					SetFieldRed("Plate", false);
					PushRadarSpeedText("—");
					PushSpeedSignal(0);
				}
				PushRadarHasTarget(target != null);
				break;
			}

			case ERP_SpeedState.FLASHING:
			{
				// Track peak silently — but only when the LOCKED vehicle is in
				// the cone. Out of cone = no update; back in cone resumes.
				if (m_LockedVehicle && IsVehicleInCone(copCar, m_LockedVehicle))
				{
					float lockedSpeed = GetVehicleSpeedKmh(m_LockedVehicle);
					if (lockedSpeed > m_fPeakSpeedKmh)
						m_fPeakSpeedKmh = lockedSpeed;
				}
				if (now >= m_fFlashEndTime)
				{
					// Snap to peak, lock engaged.
					m_eSpeedState = ERP_SpeedState.LOCKED;
					StopFlashTick();
					SetFieldVisible("Speed", true);
					SetFieldVisible("Plate", true);
					string peakText = string.Format("%1 km/h", Math.Round(m_fPeakSpeedKmh));
					SetText("SpeedValue", peakText);
					SetWidgetVisible("LockIndicator", true);
					PushRadarState(ERP_RadarVisualState.LOCKED);
					PushRadarSpeedText(peakText);
					PushSpeedSignal(m_fPeakSpeedKmh);
				}
				break;
			}

			case ERP_SpeedState.LOCKED:
			{
				// Speed only rises, and only when the locked vehicle is back
				// in the cone. Plate stays frozen at the original violator.
				if (m_LockedVehicle && IsVehicleInCone(copCar, m_LockedVehicle))
				{
					float lockedSpeed = GetVehicleSpeedKmh(m_LockedVehicle);
					if (lockedSpeed > m_fPeakSpeedKmh)
					{
						m_fPeakSpeedKmh = lockedSpeed;
						string newPeakText = string.Format("%1 km/h", Math.Round(m_fPeakSpeedKmh));
						SetText("SpeedValue", newPeakText);
						PushRadarSpeedText(newPeakText);
						PushSpeedSignal(m_fPeakSpeedKmh);
					}
				}
				break;
			}
		}
	}

	// Fast animation tick driving the field blink during FLASHING.
	// Toggles label+value visibility for both fields together on a
	// "rapid off / longer on" pattern.
	protected void StartFlashTick()
	{
		if (m_bFlashTickRunning)
			return;
		m_bFlashOn = true;
		SetFieldVisible("Speed", true);
		SetFieldVisible("Plate", true);
		m_fFlashNextToggle = GetWorldTimeSeconds() + m_fFlashOnSec;
		GetGame().GetCallqueue().CallLater(FlashTick, 30, true);
		m_bFlashTickRunning = true;
	}

	protected void StopFlashTick()
	{
		if (!m_bFlashTickRunning)
			return;
		GetGame().GetCallqueue().Remove(FlashTick);
		m_bFlashTickRunning = false;
		SetFieldVisible("Speed", true);
		SetFieldVisible("Plate", true);
	}

	protected void FlashTick()
	{
		float now = GetWorldTimeSeconds();
		if (now < m_fFlashNextToggle)
			return;
		m_bFlashOn = !m_bFlashOn;
		SetFieldVisible("Speed", m_bFlashOn);
		SetFieldVisible("Plate", m_bFlashOn);
		if (m_bFlashOn)
			m_fFlashNextToggle = now + m_fFlashOnSec;
		else
			m_fFlashNextToggle = now + m_fFlashOffSec;
	}

	// Walks up the parent chain from the player's controlled entity until
	// it hits a Vehicle. If that vehicle has RP_PoliceVehicleComponent,
	// returns it; otherwise returns null. Returns null if the player is on
	// foot or in a non-police vehicle.
	protected IEntity GetPlayerCopVehicle()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;
		IEntity player = pc.GetControlledEntity();
		if (!player)
			return null;
		IEntity p = player.GetParent();
		while (p)
		{
			if (Vehicle.Cast(p))
			{
				if (p.FindComponent(RP_PoliceVehicleComponent))
					return p;
				return null;
			}
			p = p.GetParent();
		}
		return null;
	}

	protected IEntity FindVehicleInCone(IEntity copCar)
	{
		vector tm[4];
		copCar.GetTransform(tm);
		vector originPos = tm[3];
		vector originForward = tm[2];

		m_aQueryResults.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(
			originPos, m_fConeRangeMeters,
			QueryCollect, null,
			EQueryEntitiesFlags.DYNAMIC);

		float halfAngleCos = Math.Cos(m_fConeHalfAngleDeg * Math.DEG2RAD);
		IEntity best;
		float bestDist = float.MAX;

		foreach (IEntity ent : m_aQueryResults)
		{
			if (ent == copCar)
				continue;
			if (!Vehicle.Cast(ent))
				continue;

			vector toEnt = ent.GetOrigin() - originPos;
			float dist = toEnt.Length();
			if (dist < 0.5 || dist > m_fConeRangeMeters)
				continue;

			vector dir = toEnt * (1.0 / dist);
			float dotVal = vector.Dot(dir, originForward);
			if (dotVal < halfAngleCos)
				continue;

			if (dist < bestDist)
			{
				bestDist = dist;
				best = ent;
			}
		}
		return best;
	}

	protected bool QueryCollect(IEntity entity)
	{
		m_aQueryResults.Insert(entity);
		return true;
	}

	// Cone test for a single specific vehicle (vs FindVehicleInCone, which
	// returns whichever is closest). Used by the LOCKED state to gate peak
	// updates on the locked vehicle's presence in the cone.
	protected bool IsVehicleInCone(IEntity copCar, IEntity vehicle)
	{
		if (!copCar || !vehicle)
			return false;
		vector tm[4];
		copCar.GetTransform(tm);
		vector toEnt = vehicle.GetOrigin() - tm[3];
		float dist = toEnt.Length();
		if (dist < 0.5 || dist > m_fConeRangeMeters)
			return false;
		vector dir = toEnt * (1.0 / dist);
		float halfAngleCos = Math.Cos(m_fConeHalfAngleDeg * Math.DEG2RAD);
		return vector.Dot(dir, tm[2]) >= halfAngleCos;
	}

	protected float GetVehicleSpeedKmh(IEntity vehicle)
	{
		Physics phys = vehicle.GetPhysics();
		if (!phys)
			return 0;
		return phys.GetVelocity().Length() * 3.6;
	}

	// Plays the lock sound event through the radar's RP_CopAudioComponent
	// (which loads the equipment .acp). The audio component now lives on
	// the SpeedRadar prop entity rather than the cop car itself, so we
	// walk the cop car's child hierarchy to find it. HUD-only / local —
	// no RPC.
	protected void PlayLockSound(IEntity copCar)
	{
		if (m_sLockSoundEvent.IsEmpty() || !copCar)
			return;
		IEntity audioHost;
		RP_CopAudioComponent sc = FindCopAudio(copCar, audioHost);
		if (!sc)
		{
			Print("[RP_Surveillance] No RP_CopAudioComponent found on cop car or its children — radar beep skipped.", LogLevel.WARNING);
			return;
		}
		vector tm[4];
		audioHost.GetTransform(tm);
		sc.SoundEventTransform(m_sLockSoundEvent, tm);
	}

	// Walks the cop car's hierarchy and returns the first
	// RP_CopAudioComponent it finds, along with the entity hosting it
	// (so the sound emits from that entity's transform).
	protected RP_CopAudioComponent FindCopAudio(IEntity root, out IEntity host)
	{
		RP_CopAudioComponent sc = RP_CopAudioComponent.Cast(root.FindComponent(RP_CopAudioComponent));
		if (sc)
		{
			host = root;
			return sc;
		}
		IEntity c = root.GetChildren();
		while (c)
		{
			sc = RP_CopAudioComponent.Cast(c.FindComponent(RP_CopAudioComponent));
			if (sc)
			{
				host = c;
				return sc;
			}
			sc = FindCopAudio(c, host);
			if (sc)
				return sc;
			c = c.GetSibling();
		}
		host = null;
		return null;
	}

	// POC stub: stand-in plate from the entity's workbench name.
	// Replace with a real plate registry once we add one.
	protected string GetVehiclePlate(IEntity vehicle)
	{
		string name = vehicle.GetName();
		if (name.IsEmpty())
			return "—";
		return name;
	}

	protected bool IsSpeedOverLimit(float speedKmh)
	{
		return speedKmh > m_fSpeedLimitKmh;
	}

	// POC stub: hardcoded watchlist (currently empty).
	// Wire this up to a real registry / RPC when ready.
	protected bool IsPlateFlagged(string plate)
	{
		return false;
	}

	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;
		return world.GetWorldTime() / 1000.0;
	}

	protected void SetText(string widgetName, string text)
	{
		if (!m_wRoot)
			return;
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (!w)
			return;
		TextWidget tw = TextWidget.Cast(w);
		if (tw)
			tw.SetText(text);
	}

	// Colors a (label + value) pair red on violation, white otherwise.
	// fieldName is "Speed" or "Plate"; resolves to <fieldName>Label and
	// <fieldName>Value.
	protected void SetFieldRed(string fieldName, bool isRed)
	{
		int color;
		if (isRed)
			color = 0xFFFF0000;
		else
			color = 0xFFFFFFFF;
		SetWidgetColor(fieldName + "Label", color);
		SetWidgetColor(fieldName + "Value", color);
	}

	// Toggles label+value visibility for a field together — used by the
	// flash blink so the entire field winks in unison.
	protected void SetFieldVisible(string fieldName, bool visible)
	{
		SetWidgetVisible(fieldName + "Label", visible);
		SetWidgetVisible(fieldName + "Value", visible);
	}

	protected void SetWidgetVisible(string widgetName, bool visible)
	{
		if (!m_wRoot)
			return;
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (w)
			w.SetVisible(visible);
	}

	// 0xAARRGGBB packed.
	protected void SetWidgetColor(string widgetName, int color)
	{
		if (!m_wRoot)
			return;
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (!w)
			return;
		TextWidget tw = TextWidget.Cast(w);
		if (tw)
			tw.SetColorInt(color);
	}

	// Walks the cop car's child hierarchy to find the SpeedRadar prop's
	// visual driver. The radar is nested in the police vehicle prefab
	// (typically via a SlotManagerComponent slot), so the component lives
	// somewhere in the parent's child chain.
	protected RP_SpeedRadarVisualComponent FindRadarVisual(IEntity copCar)
	{
		if (!copCar)
			return null;
		IEntity child = copCar.GetChildren();
		while (child)
		{
			RP_SpeedRadarVisualComponent v = RP_SpeedRadarVisualComponent.Cast(child.FindComponent(RP_SpeedRadarVisualComponent));
			if (v)
				return v;
			RP_SpeedRadarVisualComponent inner = FindRadarVisualRecursive(child);
			if (inner)
				return inner;
			child = child.GetSibling();
		}
		return null;
	}

	protected RP_SpeedRadarVisualComponent FindRadarVisualRecursive(IEntity entity)
	{
		IEntity child = entity.GetChildren();
		while (child)
		{
			RP_SpeedRadarVisualComponent v = RP_SpeedRadarVisualComponent.Cast(child.FindComponent(RP_SpeedRadarVisualComponent));
			if (v)
				return v;
			RP_SpeedRadarVisualComponent inner = FindRadarVisualRecursive(child);
			if (inner)
				return inner;
			child = child.GetSibling();
		}
		return null;
	}

	protected void PushRadarState(ERP_RadarVisualState state)
	{
		if (!m_RadarVisual)
			return;
		m_RadarVisual.SetState(state);
	}

	protected void PushRadarSpeedText(string text)
	{
		if (m_RadarVisual)
			m_RadarVisual.SetSpeedText(text);
	}

	// Resolves the cop vehicle's SignalsManagerComponent and registers
	// (or finds) the speed signal index. Idempotent — re-invoking after
	// resolution is a no-op. Cleared on Hide() so re-Show on a different
	// cop car re-resolves.
	protected void EnsureSpeedSignal(IEntity copCar)
	{
		if (m_iSpeedSignalIdx >= 0 && m_SignalsMgr)
			return;
		if (!copCar)
			return;
		m_SignalsMgr = SignalsManagerComponent.Cast(copCar.FindComponent(SignalsManagerComponent));
		if (!m_SignalsMgr)
		{
			Print(string.Format("[RP_Surveillance] Cop car has no SignalsManagerComponent — radar screen text will not update."), LogLevel.WARNING);
			return;
		}
		// MP variant — AG0 MFD framework reads via AddOrFindMPSignal so the
		// value replicates to passenger clients. AddOrFindSignal returns an
		// index into a separate local-only pool that the framework does
		// not see, so writes there silently do nothing.
		// Args: (name, valueThreshold, blendSpeed (0 = instant), initialValue)
		m_iSpeedSignalIdx = m_SignalsMgr.AddOrFindMPSignal(m_sSpeedSignalName, 0.5, 0, 0);
	}

	protected void PushSpeedSignal(float kmh)
	{
		if (!m_SignalsMgr || m_iSpeedSignalIdx < 0)
			return;
		m_SignalsMgr.SetSignalValue(m_iSpeedSignalIdx, kmh);
	}

	protected void PushRadarHasTarget(bool hasTarget)
	{
		if (m_RadarVisual)
			m_RadarVisual.SetHasTarget(hasTarget);
	}
}
