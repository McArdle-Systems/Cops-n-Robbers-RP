/**
 * RP_SpeedRadarLogicComponent
 *
 * Server-authoritative radar driver. Lives on the cop vehicle.
 *
 * Replaces the old client-side state machine that ran inside
 * RP_SurveillanceHUDComponent. The HUD now opens an overlay and reads
 * snapshot fields from the local replica of this component; the cop's
 * machine does not run detection, audio, or visual decisions.
 *
 * What runs where:
 *   Server:
 *     - Cone scan (QueryEntitiesBySphere on authoritative entity state)
 *     - Speed reading from server-side physics
 *     - Plate lookup (RP_TrafficLoopComponent registry)
 *     - State machine (NORMAL/FLASHING/LOCKED via ERP_RadarVisualState)
 *     - Decides the speed value clients should display, but does NOT
 *       write the MFD signal directly — see the note in
 *       ApplySnapshotLocal.
 *     - Broadcasts: per-tick snapshot, one-shot lock sound
 *
 *   Every client (including the host):
 *     - Receives RpcDo_Snapshot  -> applies state to the local
 *                                   RP_SpeedRadarVisualComponent, caches
 *                                   speed/plate for the HUD, AND writes
 *                                   the radar_speed_kmh MP signal on
 *                                   the local SignalsManagerComponent
 *                                   (the cop driver has simulation
 *                                   authority — a server-only write
 *                                   would be overwritten). Sent every
 *                                   tick, so a late-joining client
 *                                   self-heals within ~250 ms.
 *     - Receives RpcDo_LockSound -> plays radar beep through the prop's
 *                                   RP_CopAudioComponent (3D positional).
 *                                   Server-self also runs the local
 *                                   handler since Broadcast does not
 *                                   loop back to the sender.
 *
 * Activation:
 *   SetActive(true/false) is called server-side, from
 *   RP_PlayerRpcRelayComponent.ApplyRadarPower (which is already on the
 *   server-routed toggle path for the MFD screen). One call point keeps
 *   screen + logic in lockstep.
 */

[ComponentEditorProps(category: "RP/Surveillance", description: "Server-authoritative radar logic. Attach to the cop vehicle (sibling of the SpeedRadar prop slot). Drives detection, state machine, signal output, and broadcast of visual/audio events.")]
class RP_SpeedRadarLogicComponentClass : ScriptComponentClass
{
}

class RP_SpeedRadarLogicComponent : ScriptComponent
{
	[Attribute(defvalue: "22.5", desc: "Cone half-angle in degrees from the cop car's forward direction. Total cone width is 2x this.")]
	protected float m_fConeHalfAngleDeg;

	[Attribute(defvalue: "50.0", desc: "Cone range in meters.")]
	protected float m_fConeRangeMeters;

	[Attribute(defvalue: "0.25", desc: "Scan interval in seconds (server tick). Drives the cone scan and state machine.")]
	protected float m_fScanIntervalSeconds;

	[Attribute(defvalue: "50.0", desc: "Speed limit (km/h). At or below = white, above = triggers FLASHING.")]
	protected float m_fSpeedLimitKmh;

	[Attribute(defvalue: "1.5", desc: "Duration in seconds of the warning flash before LOCK engages.")]
	protected float m_fFlashDurationSec;

	[Attribute(defvalue: "radar_speed_kmh", desc: "Vehicle signal name updated each tick with the current radar reading. Bind this same name in the radar screen's AG0_MFDTextConfig.SignalName so the MFD framework substitutes it into the FormatString. Uses AddOrFindMPSignal so the value replicates to passenger clients.")]
	protected string m_sSpeedSignalName;

	[Attribute(defvalue: "SOUND_RADAR_BEEPING", desc: "Sound event for the speed-lock alert. Fires once when the target first crosses the speed limit (including delayed firing if the target was already plate-locked and only later started speeding). Empty = silent. Played 3D positional from the SpeedRadar prop's RP_CopAudioComponent on every client.")]
	protected string m_sLockSoundEvent;

	[Attribute(defvalue: "SOUND_RADAR_LPR_HIT", desc: "Sound event for the LPR / watchlist hit. Fires once when the target is first recognized as a watched plate. Independent of the speed lock — when a watched plate is also speeding, both sounds trigger on the same tick and play in parallel. Empty = silent. Played 3D positional from the SpeedRadar prop's RP_CopAudioComponent on every client.")]
	protected string m_sLPRHitSoundEvent;

	// Lock-reason bitmask. Used in both the per-lock 'has-fired' tracking
	// (server-side) and the snapshot payload (server → all clients).
	// Lets the HUD distinguish overspeed-only, LPR-only, and both, and
	// guards the per-lock one-shot semantics on the sound triggers.
	const int LOCK_REASON_NONE  = 0;
	const int LOCK_REASON_SPEED = 1;
	const int LOCK_REASON_PLATE = 2;

	// ----------------------------------------------------------------------
	// Server-only runtime
	// ----------------------------------------------------------------------

	protected bool m_bActive;
	protected ERP_RadarVisualState m_eState = ERP_RadarVisualState.OFF;

	// Lock fields. Same semantics as the old HUD state machine but the
	// locked vehicle is held as a strong ref since the server already
	// has the authoritative entity. Now multi-reason: the lock can be
	// triggered by speed, by plate, or by both — m_iLockReasonFired
	// tracks which alert sounds have already fired this lock so each
	// fires at most once per lock cycle.
	protected IEntity m_LockedVehicle;
	protected float m_fTriggerSpeedKmh;
	protected float m_fPeakSpeedKmh;
	// Last live speed read from physics while the locked vehicle was in
	// the cone. Used as the display value for plate-only locks where the
	// speed channel is in "instant" mode (must track downward too, per
	// gameplay). Holds the last reading when the target leaves the cone
	// so the display doesn't snap to zero. Distinct from m_fPeakSpeedKmh,
	// which only ever moves up and exists for the speed-lock display.
	protected float m_fLastLiveSpeedKmh;
	protected string m_sLockedPlate;
	protected float m_fFlashEndTime;
	protected int m_iLockReasonFired;

	protected ref array<IEntity> m_aQueryResults = {};

	// MFD speed signal. Server resolves once SetActive(true) lands, pushes
	// each tick. The vehicle's SignalsManagerComponent is the host.
	protected SignalsManagerComponent m_SignalsMgr;
	protected int m_iSpeedSignalIdx = -1;

	// ----------------------------------------------------------------------
	// Client-cached snapshot (broadcast each tick from server)
	// ----------------------------------------------------------------------
	//
	// Readable from any client (server included). The HUD reads these
	// instead of running its own scan. Initial values are the "no target"
	// resting display, so a freshly-opened HUD reads sane defaults before
	// the first snapshot lands.

	protected float m_fSnapSpeedKmh;
	protected string m_sSnapPlate = "—";
	protected bool m_bSnapHasTarget;
	protected ERP_RadarVisualState m_eSnapState = ERP_RadarVisualState.OFF;
	// Lock-reason bitmask of currently-active alert reasons. The HUD
	// reads this to drive the SPEEDING / WATCHLIST badges and the
	// red-text fields independently.
	protected int m_iSnapLockReason;

	// ----------------------------------------------------------------------
	// Lifecycle
	// ----------------------------------------------------------------------

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		// Defer one frame so the cop car's SignalsManagerComponent and AG0
		// MFD slot init have settled. Then publish a resting OFF snapshot
		// from the server so the speed signal exists in the MP pool with
		// value 0. Without this, the radar screen shows "inf km/h" between
		// car spawn and the first time a cop opens the radar HUD — AG0
		// renders FormatString-substituted "%1" as "inf" when the signal
		// is unbound (see reforger_ag0_mfd_recipe).
		GetGame().GetCallqueue().CallLater(PublishInitialSnapshot, 0, false);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TickServer);
		super.OnDelete(owner);
	}

	protected void PublishInitialSnapshot()
	{
		if (!Replication.IsServer())
			return;
		PublishSnapshot(ERP_RadarVisualState.OFF, 0, "—", false, LOCK_REASON_NONE);
	}

	// ----------------------------------------------------------------------
	// Activation (server entry, called from RP_PlayerRpcRelayComponent)
	// ----------------------------------------------------------------------

	void SetActive(bool wantOn)
	{
		if (!Replication.IsServer())
			return;
		if (m_bActive == wantOn)
			return;
		m_bActive = wantOn;
		if (wantOn)
		{
			ResetLockState();
			m_eState = ERP_RadarVisualState.SCANNING;
			PublishSnapshot(ERP_RadarVisualState.SCANNING, 0, "—", false, LOCK_REASON_NONE);
			GetGame().GetCallqueue().CallLater(TickServer, (int)(m_fScanIntervalSeconds * 1000), true);
			TickServer();
		}
		else
		{
			GetGame().GetCallqueue().Remove(TickServer);
			ResetLockState();
			m_eState = ERP_RadarVisualState.OFF;
			PublishSnapshot(ERP_RadarVisualState.OFF, 0, "—", false, LOCK_REASON_NONE);
		}
	}

	// ----------------------------------------------------------------------
	// Local-replica readers for the HUD
	// ----------------------------------------------------------------------

	ERP_RadarVisualState GetSnapshotState()  { return m_eSnapState; }
	float                GetSnapshotSpeed()  { return m_fSnapSpeedKmh; }
	string               GetSnapshotPlate()  { return m_sSnapPlate; }
	bool                 GetSnapshotHasTarget() { return m_bSnapHasTarget; }
	int                  GetSnapshotLockReason() { return m_iSnapLockReason; }
	bool                 GetSnapshotIsSpeeding() { return (m_iSnapLockReason & LOCK_REASON_SPEED) != 0; }
	bool                 GetSnapshotIsWatchHit() { return (m_iSnapLockReason & LOCK_REASON_PLATE) != 0; }
	// Back-compat: any locked reason on the plate is the "plate is
	// flagged for display" signal for callers that haven't migrated to
	// GetSnapshotIsWatchHit().
	bool                 GetSnapshotPlateFlagged() { return GetSnapshotIsWatchHit(); }

	static RP_SpeedRadarLogicComponent FindOnVehicle(IEntity copCar)
	{
		if (!copCar)
			return null;
		return RP_SpeedRadarLogicComponent.Cast(copCar.FindComponent(RP_SpeedRadarLogicComponent));
	}

	// ----------------------------------------------------------------------
	// Server tick
	// ----------------------------------------------------------------------

	protected void TickServer()
	{
		if (!m_bActive)
			return;
		IEntity copCar = GetOwner();
		if (!copCar)
			return;

		IEntity target = FindVehicleInCone(copCar);
		float currentSpeedKmh = 0;
		string currentPlate = "—";
		bool currentPlateWatched = false;
		if (target)
		{
			currentSpeedKmh = GetVehicleSpeedKmh(target);
			currentPlate = GetVehiclePlate(target);
			currentPlateWatched = IsPlateFlagged(currentPlate);
		}

		float now = GetWorldTimeSeconds();

		// What we'll publish at end of tick. Snapshot is the single
		// per-tick broadcast that carries state + display fields, so
		// late-joining clients self-heal within one tick interval.
		float pubSpeed = 0;
		string pubPlate = "—";
		bool pubHasTarget = false;
		int pubLockReason = LOCK_REASON_NONE;

		switch (m_eState)
		{
			case ERP_RadarVisualState.SCANNING:
			{
				bool speedHit = target && currentSpeedKmh > m_fSpeedLimitKmh;
				bool plateHit = target && currentPlateWatched;
				if (speedHit || plateHit)
				{
					m_LockedVehicle = target;
					// Plate-only triggers don't have a meaningful "trigger
					// speed" to freeze on, so use the current speed (often
					// well under the limit). During FLASHING/LOCKED the
					// display picks instant vs peak per lock reason —
					// see UpdateLockedSpeedTracking.
					m_fTriggerSpeedKmh = currentSpeedKmh;
					m_fPeakSpeedKmh = currentSpeedKmh;
					m_fLastLiveSpeedKmh = currentSpeedKmh;
					m_sLockedPlate = currentPlate;
					m_fFlashEndTime = now + m_fFlashDurationSec;
					m_eState = ERP_RadarVisualState.FLASHING;

					// Per-lock fired-flag bitmask. Set each bit only as we
					// actually emit that sound — a plate-only lock that
					// later starts speeding (mid-FLASHING/LOCKED) will set
					// the SPEED bit and fire the speed sound below.
					m_iLockReasonFired = LOCK_REASON_NONE;
					if (speedHit)
					{
						m_iLockReasonFired = m_iLockReasonFired | LOCK_REASON_SPEED;
						FireSpeedSound();
					}
					if (plateHit)
					{
						m_iLockReasonFired = m_iLockReasonFired | LOCK_REASON_PLATE;
						FireLPRSound();
					}

					pubSpeed = m_fTriggerSpeedKmh;
					pubPlate = m_sLockedPlate;
					pubHasTarget = true;
					pubLockReason = m_iLockReasonFired;
					break;
				}
				if (target)
				{
					pubSpeed = currentSpeedKmh;
					pubPlate = currentPlate;
					pubHasTarget = true;
					// No lock yet — HUD treats this as plain SCANNING.
				}
				break;
			}

			case ERP_RadarVisualState.FLASHING:
			{
				UpdateLockedSpeedTracking(copCar);
				bool speedBitSet = (m_iLockReasonFired & LOCK_REASON_SPEED) != 0;
				if (now >= m_fFlashEndTime)
				{
					m_eState = ERP_RadarVisualState.LOCKED;
					// First LOCKED tick — speed mode picks its display
					// value below.
				}
				if (m_eState == ERP_RadarVisualState.LOCKED)
				{
					// Peak-tracking for speed-locked targets, instant
					// (current live speed) for plate-only locks.
					if (speedBitSet)
						pubSpeed = m_fPeakSpeedKmh;
					else
						pubSpeed = m_fLastLiveSpeedKmh;
				}
				else if (speedBitSet)
				{
					// Still FLASHING and speed channel is the trigger:
					// freeze on the trigger speed for the "we caught you
					// going X" UX (unchanged from pre-LPR behavior).
					pubSpeed = m_fTriggerSpeedKmh;
				}
				else
				{
					// Plate-only FLASHING: stay in instant mode even
					// during the flash, so the display can move down if
					// the target is slowing.
					pubSpeed = m_fLastLiveSpeedKmh;
				}
				pubPlate = m_sLockedPlate;
				pubHasTarget = true;
				pubLockReason = m_iLockReasonFired;
				break;
			}

			case ERP_RadarVisualState.LOCKED:
			{
				UpdateLockedSpeedTracking(copCar);
				bool speedBitSet = (m_iLockReasonFired & LOCK_REASON_SPEED) != 0;
				if (speedBitSet)
					pubSpeed = m_fPeakSpeedKmh;
				else
					pubSpeed = m_fLastLiveSpeedKmh;
				pubPlate = m_sLockedPlate;
				pubHasTarget = true;
				pubLockReason = m_iLockReasonFired;
				break;
			}

			default:
				break;
		}

		PublishSnapshot(m_eState, pubSpeed, pubPlate, pubHasTarget, pubLockReason);
	}

	// Per-tick during FLASHING/LOCKED: read the live speed of the locked
	// vehicle (while still in cone), update the appropriate tracker for
	// the current display mode (instant vs peak), and check for the
	// delayed speed-trigger on plate-only locks. When the speed bit
	// transitions from off to on, restarts the peak from the live speed
	// so any prior instant-mode reading doesn't bleed into the peak
	// display. Held when out of cone — m_fLastLiveSpeedKmh keeps its
	// previous value so the display doesn't snap to zero.
	protected void UpdateLockedSpeedTracking(IEntity copCar)
	{
		if (!m_LockedVehicle || !IsVehicleInCone(copCar, m_LockedVehicle))
			return;
		float liveSpeed = GetVehicleSpeedKmh(m_LockedVehicle);
		m_fLastLiveSpeedKmh = liveSpeed;

		bool wasSpeedBit = (m_iLockReasonFired & LOCK_REASON_SPEED) != 0;
		MaybeFireDelayedSpeedAlert(liveSpeed);
		bool nowSpeedBit = (m_iLockReasonFired & LOCK_REASON_SPEED) != 0;

		if (nowSpeedBit && !wasSpeedBit)
		{
			// Just transitioned plate-only -> dual-trigger. Anchor the
			// peak at the trigger speed so the LOCKED display reads
			// "we caught you at this speed" cleanly.
			m_fPeakSpeedKmh = liveSpeed;
		}
		else if (nowSpeedBit && liveSpeed > m_fPeakSpeedKmh)
		{
			m_fPeakSpeedKmh = liveSpeed;
		}
	}

	// Fires the speed-lock sound if the locked target has crossed the
	// limit and we haven't already fired the speed alert this lock.
	// Lets a plate-triggered lock pick up a "they're speeding too" alert
	// for the "cool factor" double-trigger. One-shot per lock cycle.
	protected void MaybeFireDelayedSpeedAlert(float lockedSpeed)
	{
		if ((m_iLockReasonFired & LOCK_REASON_SPEED) != 0)
			return;
		if (lockedSpeed <= m_fSpeedLimitKmh)
			return;
		m_iLockReasonFired = m_iLockReasonFired | LOCK_REASON_SPEED;
		FireSpeedSound();
	}

	// Broadcast the speed-alert beep (existing m_sLockSoundEvent).
	protected void FireSpeedSound()
	{
		if (m_sLockSoundEvent.IsEmpty())
			return;
		bool hostHasPC = GetGame().GetPlayerController() != null;
		Print(string.Format("[RP_SpeedRadarLogic] %1 Speed alert fired. plate=%2 speed=%3 km/h event='%4' broadcasting RPC, serverSelfLoopback=%5.",
			RoleTag(), m_sLockedPlate, m_fPeakSpeedKmh, m_sLockSoundEvent, hostHasPC));
		Rpc(RpcDo_LockSound);
		if (hostHasPC)
			PlayLockSoundLocal();
	}

	// Broadcast the LPR / watchlist-hit alert (m_sLPRHitSoundEvent).
	protected void FireLPRSound()
	{
		if (m_sLPRHitSoundEvent.IsEmpty())
			return;
		bool hostHasPC = GetGame().GetPlayerController() != null;
		Print(string.Format("[RP_SpeedRadarLogic] %1 LPR alert fired. plate=%2 event='%3' broadcasting RPC, serverSelfLoopback=%4.",
			RoleTag(), m_sLockedPlate, m_sLPRHitSoundEvent, hostHasPC));
		Rpc(RpcDo_LPRSound);
		if (hostHasPC)
			PlayLPRSoundLocal();
	}

	protected void ResetLockState()
	{
		m_LockedVehicle = null;
		m_fTriggerSpeedKmh = 0;
		m_fPeakSpeedKmh = 0;
		m_fLastLiveSpeedKmh = 0;
		m_sLockedPlate = "";
		m_fFlashEndTime = 0;
		m_iLockReasonFired = LOCK_REASON_NONE;
	}

	// ----------------------------------------------------------------------
	// Broadcast — per-tick snapshot (state + display fields)
	// ----------------------------------------------------------------------
	//
	// Single broadcast that carries everything a client needs. Sent every
	// tick while active, so a late-joining client receives a complete
	// snapshot within one tick interval and the radar self-heals.

	protected void PublishSnapshot(ERP_RadarVisualState state, float speed, string plate, bool hasTarget, int lockReason)
	{
		// Apply locally on server first — Broadcast does not loop back to
		// the sender, so the host has to drive its own visuals here.
		ApplySnapshotLocal(state, speed, plate, hasTarget, lockReason);
		Rpc(RpcDo_Snapshot, state, speed, plate, hasTarget, lockReason);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Snapshot(ERP_RadarVisualState state, float speed, string plate, bool hasTarget, int lockReason)
	{
		ApplySnapshotLocal(state, speed, plate, hasTarget, lockReason);
	}

	protected void ApplySnapshotLocal(ERP_RadarVisualState state, float speed, string plate, bool hasTarget, int lockReason)
	{
		m_eSnapState = state;
		m_fSnapSpeedKmh = speed;
		m_sSnapPlate = plate;
		m_bSnapHasTarget = hasTarget;
		m_iSnapLockReason = lockReason;
		RP_SpeedRadarVisualComponent visual = FindVisualOnSelf();
		if (visual)
		{
			visual.SetState(state);
			visual.SetHasTarget(hasTarget);
		}
		// The signal has to be written client-side. The cop is the
		// driver, which gives their client simulation authority over the
		// vehicle's SignalsManagerComponent — a server-side write gets
		// overwritten by the authoritative client's next sync. Having
		// every client write its local copy from the snapshot RPC means
		// the cop (the authority) is one of those writers, so their
		// authoritative value matches everyone else's.
		EnsureSpeedSignal();
		SetSpeedSignal(speed);
	}

	// ----------------------------------------------------------------------
	// Broadcast — one-shot lock sound
	// ----------------------------------------------------------------------

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_LockSound()
	{
		Print(string.Format("[RP_SpeedRadarLogic] %1 RpcDo_LockSound received — invoking PlayLockSoundLocal.", RoleTag()));
		PlayLockSoundLocal();
	}

	// Plays the speed-lock beep on the local peer. Invoked by RpcDo_
	// LockSound on remote clients; the server-self call happens directly
	// from FireSpeedSound since Rpc(RplRcver.Broadcast) does not loop
	// back to the sender. Thin wrapper around PlaySoundLocal so the LPR
	// path can share the same lookup/log/diagnostic flow.
	protected void PlayLockSoundLocal()
	{
		PlaySoundLocal(m_sLockSoundEvent, "speed");
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_LPRSound()
	{
		Print(string.Format("[RP_SpeedRadarLogic] %1 RpcDo_LPRSound received — invoking PlayLPRSoundLocal.", RoleTag()));
		PlayLPRSoundLocal();
	}

	// Plays the LPR / watchlist-hit alert on the local peer. Mirror of
	// PlayLockSoundLocal for the new plate-match sound channel.
	protected void PlayLPRSoundLocal()
	{
		PlaySoundLocal(m_sLPRHitSoundEvent, "LPR");
	}

	// Shared body for radar alert sounds. eventName is the .acp event
	// string; channelTag is a short label for diagnostic logs ("speed"
	// / "LPR") so the message lines stay distinguishable.
	protected void PlaySoundLocal(string eventName, string channelTag)
	{
		string role = RoleTag();
		if (eventName.IsEmpty())
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlaySoundLocal(%2): event name empty — skipped (configured silent).", role, channelTag));
			return;
		}
		IEntity copCar = GetOwner();
		if (!copCar)
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlaySoundLocal(%2): GetOwner()==null — radar sound skipped.", role, channelTag), LogLevel.WARNING);
			return;
		}
		Print(string.Format("[RP_SpeedRadarLogic] %1 PlaySoundLocal(%2): entering. owner=%3 event='%4' — searching for RP_CopAudioComponent.",
			role, channelTag, GetOwnerPlate(copCar), eventName));
		IEntity audioHost;
		RP_CopAudioComponent sc = FindCopAudio(copCar, audioHost);
		if (!sc)
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlaySoundLocal(%2): no RP_CopAudioComponent on cop car or its slot/child entities — sound skipped.", role, channelTag), LogLevel.WARNING);
			return;
		}
		AudioHandle h = sc.SoundEvent(eventName);
		// AudioHandle.Invalid means the engine could not resolve the event
		// (e.g. .acp bank not loaded on this peer, event name typo, or a
		// malformed .acp). The LPR sound name in particular is fresh —
		// expect this warning until the .acp adds SOUND_RADAR_LPR_HIT.
		if (h == AudioHandle.Invalid)
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlaySoundLocal(%2): SoundEvent('%3') returned AudioHandle.Invalid — event not resolved. No audio will play.",
				role, channelTag, eventName), LogLevel.WARNING);
		}
		else
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlaySoundLocal(%2): SoundEvent('%3') issued — handle=%4 (valid).",
				role, channelTag, eventName, h));
		}
	}

	// Looks up the vehicle's plate in the shared registry. Cop cars are
	// registered as "PD_Car_N" by RP_CopVehicleSpawnerComponent; civs as
	// "<faction>_Car_N" by the traffic loop. Falls back to "<unregistered>"
	// for anything not in the registry (e.g. a manually-placed vehicle).
	protected string GetOwnerPlate(IEntity ent)
	{
		if (!ent)
			return "<null>";
		RP_TrafficLoopComponent traffic = RP_TrafficLoopComponent.GetInstance();
		if (traffic)
		{
			string plate = traffic.GetVehiclePlate(ent);
			if (!plate.IsEmpty())
				return plate;
		}
		return "<unregistered>";
	}

	// Compact "[server-dedi]" / "[server-host]" / "[client]" tag for log
	// lines. Lets us tell at a glance which peer produced a diagnostic.
	protected string RoleTag()
	{
		bool isServer = Replication.IsServer();
		bool hasPC = GetGame().GetPlayerController() != null;
		if (isServer && !hasPC)
			return "[server-dedi]";
		if (isServer && hasPC)
			return "[server-host]";
		return "[client]";
	}

	// Walks an entity, its plain children, and its SlotManagerComponent
	// slots looking for an RP_CopAudioComponent. Both attachment styles
	// recurse so the lookup hits child entities mounted via either path.
	protected RP_CopAudioComponent FindCopAudio(IEntity root, out IEntity host)
	{
		if (!root)
		{
			host = null;
			return null;
		}
		RP_CopAudioComponent sc = RP_CopAudioComponent.Cast(root.FindComponent(RP_CopAudioComponent));
		if (sc)
		{
			host = root;
			return sc;
		}
		IEntity c = root.GetChildren();
		while (c)
		{
			sc = FindCopAudio(c, host);
			if (sc)
				return sc;
			c = c.GetSibling();
		}
		SlotManagerComponent slotMgr = SlotManagerComponent.Cast(root.FindComponent(SlotManagerComponent));
		if (slotMgr)
		{
			array<EntitySlotInfo> slotInfos = {};
			slotMgr.GetSlotInfos(slotInfos);
			foreach (EntitySlotInfo info : slotInfos)
			{
				if (!info)
					continue;
				IEntity slotEnt = info.GetAttachedEntity();
				if (!slotEnt)
					continue;
				sc = FindCopAudio(slotEnt, host);
				if (sc)
					return sc;
			}
		}
		host = null;
		return null;
	}

	// ----------------------------------------------------------------------
	// Detection helpers (server-side; would also work on clients but we
	// don't need them there)
	// ----------------------------------------------------------------------

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

	protected string GetVehiclePlate(IEntity vehicle)
	{
		RP_TrafficLoopComponent mgr = RP_TrafficLoopComponent.GetInstance();
		if (mgr)
		{
			string plate = mgr.GetVehiclePlate(vehicle);
			if (!plate.IsEmpty())
				return plate;
		}
		string name = vehicle.GetName();
		if (name.IsEmpty())
			return "—";
		return name;
	}

	protected bool IsPlateFlagged(string plate)
	{
		// Server-side lookup against the live watchlist. The watchlist
		// component is server-only and lives on the GameMode entity —
		// see RP_PlateWatchlistComponent.
		//
		// Future filter: cop plates (PD_*) probably shouldn't be flag-
		// match-eligible since the cone scan can still pick up partner
		// units. Today the watchlist's auto-budget pulls from active
		// traffic-loop spawns (civilian plates only), so cop plates are
		// never seeded; an admin could still add one explicitly in
		// Slice 2. Revisit if that becomes a real concern.
		RP_PlateWatchlistComponent watchlist = RP_PlateWatchlistComponent.GetInstance();
		if (!watchlist)
			return false;
		return watchlist.IsWatched(plate);
	}

	// ----------------------------------------------------------------------
	// Signal (MFD speed text)
	// ----------------------------------------------------------------------

	protected void EnsureSpeedSignal()
	{
		if (m_iSpeedSignalIdx >= 0 && m_SignalsMgr)
			return;
		IEntity copCar = GetOwner();
		if (!copCar)
			return;
		m_SignalsMgr = SignalsManagerComponent.Cast(copCar.FindComponent(SignalsManagerComponent));
		if (!m_SignalsMgr)
		{
			Print("[RP_SpeedRadarLogic] Cop car has no SignalsManagerComponent — radar screen text will not update.", LogLevel.WARNING);
			return;
		}
		// blendSpeed: engine rejects 0 ("instant") and forces 1, which is
		// slow enough to visibly lag the 4 Hz scan as speed changes by
		// tens of km/h per tick. 1000 is fast enough to read as instant
		// while satisfying the engine's >0 requirement.
		m_iSpeedSignalIdx = m_SignalsMgr.AddOrFindMPSignal(m_sSpeedSignalName, 0.5, 1000, 0);
	}

	protected void SetSpeedSignal(float kmh)
	{
		if (!m_SignalsMgr || m_iSpeedSignalIdx < 0)
			return;
		m_SignalsMgr.SetSignalValue(m_iSpeedSignalIdx, kmh);
	}

	// ----------------------------------------------------------------------
	// Misc
	// ----------------------------------------------------------------------

	protected RP_SpeedRadarVisualComponent FindVisualOnSelf()
	{
		IEntity copCar = GetOwner();
		if (!copCar)
			return null;
		return FindRadarVisualInChildren(copCar);
	}

	protected RP_SpeedRadarVisualComponent FindRadarVisualInChildren(IEntity root)
	{
		RP_SpeedRadarVisualComponent v = RP_SpeedRadarVisualComponent.Cast(root.FindComponent(RP_SpeedRadarVisualComponent));
		if (v)
			return v;
		IEntity c = root.GetChildren();
		while (c)
		{
			v = RP_SpeedRadarVisualComponent.Cast(c.FindComponent(RP_SpeedRadarVisualComponent));
			if (v)
				return v;
			v = FindRadarVisualInChildren(c);
			if (v)
				return v;
			c = c.GetSibling();
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
}
