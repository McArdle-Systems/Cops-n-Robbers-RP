/**
 * RP_SurveillanceHUDComponent
 *
 * Display-only overlay for the active radar. Top-center, non-modal —
 * does not lock the cursor or block input. Two fields (SPEED, PLATE)
 * and a top LOCK indicator that surfaces only while a lock is active.
 *
 * All detection, state machine, audio, and signal output run server-
 * side on RP_SpeedRadarLogicComponent (attached to the cop vehicle).
 * The HUD's only job is to poll that component's broadcast snapshot
 * and render. It does NOT drive the radar; it shows what the radar
 * says.
 *
 * Render rules per snapshot.state:
 *   SCANNING : speed/plate track snapshot. Colors white. Plate red iff
 *              snapshot.plateFlagged. LOCK indicator hidden. No flash.
 *   FLASHING : speed/plate frozen by server at trigger values, both
 *              red, entire field row rapid-blinks (off / longer on) to
 *              grab attention. LOCK indicator hidden.
 *   LOCKED   : speed = peak (server snaps and only ever rises), plate
 *              from lock, both red, steady. LOCK indicator visible.
 *
 * Gating: only available while seated in a vehicle that carries
 * RP_PoliceVehicleComponent. Pressing the toggle outside a police
 * vehicle is a no-op; if the player exits a police vehicle while the
 * HUD is up, the HUD auto-hides and tears down.
 *
 * Toggle path: the open/close key fires RequestRadarPower(copCar, ...)
 * through RP_PlayerRpcRelayComponent, which routes server-side and
 * drives both the MFD screen power AND RP_SpeedRadarLogicComponent
 * .SetActive(...) in lockstep. So the server's logic component starts
 * scanning when the cop opens the HUD, and stops when they close it.
 *
 * Toggled by a Bohemia input action (default RP_ToggleSurveillance,
 * KC_RBRACKET — "]"). Action defined in chimeraInputCommon.conf and
 * surfaced under the CNR keybinding category.
 */

[ComponentEditorProps(category: "RP/Surveillance", description: "Tag — marks a vehicle as a police / emergency vehicle. Surveillance HUD only activates while seated in a tagged vehicle. Server-side, the component also self-registers in a static set that RP_EmergencyYieldComponent iterates each tick.")]
class RP_PoliceVehicleComponentClass : ScriptComponentClass
{
}

class RP_PoliceVehicleComponent : ScriptComponent
{
	// Server-only registry. Yield manager iterates this instead of a world
	// query each tick. Client instances of this component still exist (the
	// surveillance HUD reads them via FindComponent) but stay out of the set.
	protected static ref array<RP_PoliceVehicleComponent> s_aInstances = {};

	static array<RP_PoliceVehicleComponent> GetInstances() { return s_aInstances; }

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
		{
			Print(string.Format("[RP_Yield] RP_PoliceVehicleComponent on %1 — not server, skipping registry.", owner), LogLevel.NORMAL);
			return;
		}
		s_aInstances.Insert(this);
		Print(string.Format("[RP_Yield] Registered emergency vehicle: %1 (registry size now %2)", owner, s_aInstances.Count()), LogLevel.NORMAL);
	}

	override void OnDelete(IEntity owner)
	{
		int idx = s_aInstances.Find(this);
		if (idx >= 0)
			s_aInstances.Remove(idx);
		super.OnDelete(owner);
	}
}

[ComponentEditorProps(category: "RP/Audio", description: "Cop-equipment audio bank holder. Attach to the SpeedRadar prop with the equipment .acp (radar, MDT, etc.). RP_SpeedRadarLogicComponent broadcasts a play-event RPC on lock; each client plays the event 3D-positional through this component.")]
class RP_CopAudioComponentClass : SoundComponentClass
{
}

class RP_CopAudioComponent : SoundComponent
{
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Toggleable surveillance HUD overlay. Attach to the GameMode entity. Display-only — all detection logic lives on RP_SpeedRadarLogicComponent on the cop vehicle.")]
class RP_SurveillanceHUDComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_SurveillanceHUDComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "RP_ToggleSurveillance", desc: "Input action that toggles the HUD.")]
	protected string m_sToggleAction;

	[Attribute(defvalue: "{AB95DA9834D81521}UI/RP_SurveillanceHUD.layout", desc: "Layout for the HUD overlay.", UIWidgets.ResourcePickerThumbnail, params: "layout")]
	protected ResourceName m_sLayoutPath;

	[Attribute(defvalue: "0.25", desc: "Poll interval in seconds. Reads the latest snapshot from RP_SpeedRadarLogicComponent and re-renders.")]
	protected float m_fPollIntervalSeconds;

	[Attribute(defvalue: "0.06", desc: "Flash 'off' phase duration in seconds (rapid).")]
	protected float m_fFlashOffSec;

	[Attribute(defvalue: "0.24", desc: "Flash 'on' phase duration in seconds (longer steady).")]
	protected float m_fFlashOnSec;

	protected Widget m_wRoot;
	protected bool m_bVisible;
	protected bool m_bActionRegistered;

	// The cop car the HUD opened against — cached so Hide() can still
	// drive the toggle off even if the player has stepped out of the
	// vehicle (which is one of the Hide entry points; GetPlayerCopVehicle
	// would return null in that case).
	protected IEntity m_LastCopCar;

	// Last rendered state — used to detect transitions (e.g. SCANNING ->
	// FLASHING starts the blink tick; -> LOCKED stops it and shows the
	// LOCK indicator).
	protected ERP_RadarVisualState m_eLastState = ERP_RadarVisualState.OFF;

	// Last seen lockReason bitmask — used to fire the LPR hint popup
	// exactly once per "plate bit raised" transition (not every tick
	// while the bit stays set).
	protected int m_iLastLockReason;

	// Flash-animation runtime
	protected bool m_bFlashTickRunning;
	protected bool m_bFlashOn;
	protected float m_fFlashNextToggle;

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
			SetText("SpeedingBadge", "SPEEDING");
			SetText("WatchlistBadge", "WATCHLIST");
			SetWidgetColor("LockIndicator", 0xFFFF0000);
			SetWidgetColor("SpeedingBadge", 0xFFFF0000);
			SetWidgetColor("WatchlistBadge", 0xFFFF0000);
		}
		ResetDisplay();
		m_wRoot.SetVisible(true);
		m_bVisible = true;
		IEntity copCar = GetPlayerCopVehicle();
		m_LastCopCar = copCar;
		// Server-routed toggle. Drives both the MFD screen power AND
		// RP_SpeedRadarLogicComponent.SetActive on the cop car. The
		// logic component begins its scan tick and broadcasts state +
		// snapshot for this HUD (and visuals/audio for everyone else).
		RequestRadarPower(copCar, true);
		GetGame().GetCallqueue().CallLater(Tick, (int)(m_fPollIntervalSeconds * 1000), true);
		Tick();
	}

	protected void Hide()
	{
		if (m_wRoot)
			m_wRoot.SetVisible(false);
		GetGame().GetCallqueue().Remove(Tick);
		StopFlashTick();
		if (m_LastCopCar)
			RequestRadarPower(m_LastCopCar, false);
		m_LastCopCar = null;
		m_eLastState = ERP_RadarVisualState.OFF;
		m_bVisible = false;
	}

	protected void DestroyWidget()
	{
		if (!m_wRoot)
			return;
		m_wRoot.RemoveFromHierarchy();
		m_wRoot = null;
	}

	// Resets the visible HUD to "scanning, no target" defaults. Called on
	// every Show() so close+reopen is a clean slate before the first
	// snapshot from the server lands.
	protected void ResetDisplay()
	{
		StopFlashTick();
		SetText("SpeedValue", "—");
		SetText("PlateValue", "—");
		SetFieldRed("Speed", false);
		SetFieldRed("Plate", false);
		SetFieldVisible("Speed", true);
		SetFieldVisible("Plate", true);
		SetWidgetVisible("LockIndicator", false);
		SetWidgetVisible("SpeedingBadge", false);
		SetWidgetVisible("WatchlistBadge", false);
		m_eLastState = ERP_RadarVisualState.OFF;
		m_iLastLockReason = 0;
	}

	protected void Tick()
	{
		IEntity copCar = GetPlayerCopVehicle();
		if (!copCar)
		{
			// Player exited the cop vehicle — auto hide & tear down. Hide()
			// will fire RequestRadarPower(false) so the server stops the
			// logic component too.
			Hide();
			DestroyWidget();
			return;
		}

		RP_SpeedRadarLogicComponent logic = RP_SpeedRadarLogicComponent.FindOnVehicle(copCar);
		if (!logic)
		{
			// No logic component on this cop car — fall back to a quiet
			// "no signal" render so the HUD doesn't sit on stale state.
			SetText("SpeedValue", "—");
			SetText("PlateValue", "—");
			SetFieldRed("Speed", false);
			SetFieldRed("Plate", false);
			SetWidgetVisible("LockIndicator", false);
			return;
		}

		ERP_RadarVisualState state = logic.GetSnapshotState();
		float speed = logic.GetSnapshotSpeed();
		string plate = logic.GetSnapshotPlate();
		bool hasTarget = logic.GetSnapshotHasTarget();
		int lockReason = logic.GetSnapshotLockReason();
		bool isSpeeding = logic.GetSnapshotIsSpeeding();
		bool isWatchHit = logic.GetSnapshotIsWatchHit();

		// Transition side-effects: blink + LOCK indicator.
		if (state != m_eLastState)
		{
			switch (state)
			{
				case ERP_RadarVisualState.FLASHING:
					StartFlashTick();
					SetWidgetVisible("LockIndicator", false);
					break;
				case ERP_RadarVisualState.LOCKED:
					StopFlashTick();
					SetFieldVisible("Speed", true);
					SetFieldVisible("Plate", true);
					SetWidgetVisible("LockIndicator", true);
					break;
				default:
					StopFlashTick();
					SetWidgetVisible("LockIndicator", false);
					break;
			}
			m_eLastState = state;
		}

		// LPR hit popup. Fire exactly once per "plate bit raised"
		// transition — i.e. the watchlist bit went from 0 to 1 since
		// the last tick. This catches both initial plate-only locks
		// and speed-then-plate locks (rare but possible if the radar
		// somehow flagged a plate mid-lock).
		bool watchBitBefore = (m_iLastLockReason & RP_SpeedRadarLogicComponent.LOCK_REASON_PLATE) != 0;
		if (isWatchHit && !watchBitBefore)
			ShowLPRHitPopup(plate, logic);
		m_iLastLockReason = lockReason;

		// Per-channel badge visibility — independent of state, driven
		// purely by which lock-reason bits are set.
		SetWidgetVisible("SpeedingBadge", isSpeeding);
		SetWidgetVisible("WatchlistBadge", isWatchHit);

		// Refresh text + colors every tick from the snapshot. Server has
		// already frozen values during FLASHING/LOCKED so we don't need
		// to special-case here.
		switch (state)
		{
			case ERP_RadarVisualState.SCANNING:
				if (hasTarget)
				{
					SetText("SpeedValue", string.Format("%1 km/h", Math.Round(speed)));
					SetText("PlateValue", plate);
				}
				else
				{
					SetText("SpeedValue", "—");
					SetText("PlateValue", "—");
				}
				// In SCANNING, the lock-reason bits are always 0 (any
				// flagged plate triggers an immediate transition out).
				// Keep the field colors white.
				SetFieldRed("Speed", false);
				SetFieldRed("Plate", false);
				break;

			case ERP_RadarVisualState.FLASHING:
			case ERP_RadarVisualState.LOCKED:
				SetText("SpeedValue", string.Format("%1 km/h", Math.Round(speed)));
				SetText("PlateValue", plate);
				// Per-channel red: speed field red iff overspeed alert
				// has fired this lock, plate field red iff watchlist
				// alert has fired this lock. Both red == double trigger.
				SetFieldRed("Speed", isSpeeding);
				SetFieldRed("Plate", isWatchHit);
				break;

			default:
				// OFF — the HUD shouldn't render anything meaningful, but
				// the player still has it open. Quiet "no signal".
				SetText("SpeedValue", "—");
				SetText("PlateValue", "—");
				SetFieldRed("Speed", false);
				SetFieldRed("Plate", false);
				break;
		}
	}

	// Pops a stock SCR_HintManagerComponent custom hint announcing the
	// watchlist hit. The hint is local to the cop driver — this code
	// path only runs in the surveillance HUD overlay, which is itself
	// gated on "seated in a police vehicle" (see GetPlayerCopVehicle),
	// so passengers / non-cops never see it.
	//
	// First-light: hint surfaces only the plate. Vehicle type / color
	// would need plumbing through the snapshot (the radar holds the
	// strong-ref to the locked entity server-side; the snapshot only
	// carries the plate string today). Plate is already a strong hint —
	// "USSR_Car_3" reads as "USSR civilian car #3" — so this is useful
	// without the extra fields. Revisit when the prefab metadata is
	// worth surfacing.
	protected void ShowLPRHitPopup(string lockedPlate, RP_SpeedRadarLogicComponent logic)
	{
		string description = string.Format("Plate: %1", lockedPlate);
		// duration 0 = use default. isSilent false = play the standard
		// hint sound. The radar's own LPR alert is separately 3D-spatial
		// from the prop, so the hint chime stacks with it; if that feels
		// noisy, switch to isSilent=true.
		SCR_HintManagerComponent.ShowCustomHint(description, "WATCHLIST HIT", 0, false);
	}

	// Fast animation tick driving the field blink during FLASHING.
	// Toggles label+value visibility for both fields together on a
	// "rapid off / longer on" pattern. Purely visual — server already
	// decided that FLASHING is the right state.
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
		SetFieldVisibleWidget(fieldName + "Label", visible);
		SetFieldVisibleWidget(fieldName + "Value", visible);
	}

	protected void SetWidgetVisible(string widgetName, bool visible)
	{
		SetFieldVisibleWidget(widgetName, visible);
	}

	protected void SetFieldVisibleWidget(string widgetName, bool visible)
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
		{
			tw.SetColor(Color.FromInt(color));
			return;
		}
		ImageWidget iw = ImageWidget.Cast(w);
		if (iw)
			iw.SetColor(Color.FromInt(color));
	}

	// ----------------------------------------------------------------------
	// Toggle relay (server-routed)
	// ----------------------------------------------------------------------
	//
	// AG0_MFDManagerComponent.TogglePowerAction and RP_SpeedRadarLogic
	// .SetActive are both server-authoritative. Route the toggle through
	// RP_PlayerRpcRelayComponent on the player's character (which the
	// client owns) so the RplRpc(Server) call actually arrives. See that
	// component's header comment for the full rationale.

	protected void RequestRadarPower(IEntity copCar, bool wantOn)
	{
		if (!copCar)
			return;
		RP_PlayerRpcRelayComponent relay = RP_PlayerRpcRelayComponent.GetLocal();
		if (!relay)
		{
			Print("[RP_Surveillance] RequestRadarPower: no RP_PlayerRpcRelayComponent on local player character — radar will not power on. Attach the component to the cop character prefab.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarPower(copCar, wantOn);
	}
}
