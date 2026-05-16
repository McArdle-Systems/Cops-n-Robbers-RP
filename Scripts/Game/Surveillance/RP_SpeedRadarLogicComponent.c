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

	[Attribute(defvalue: "SOUND_RADAR_BEEPING", desc: "Sound event triggered on NORMAL -> FLASHING. Empty = silent. Played 3D positional from the SpeedRadar prop's RP_CopAudioComponent on every client.")]
	protected string m_sLockSoundEvent;

	// ----------------------------------------------------------------------
	// Server-only runtime
	// ----------------------------------------------------------------------

	protected bool m_bActive;
	protected ERP_RadarVisualState m_eState = ERP_RadarVisualState.OFF;

	// Speed-lock fields. Same semantics as the old HUD state machine but
	// the locked vehicle is held as a strong ref since the server already
	// has the authoritative entity.
	protected IEntity m_LockedVehicle;
	protected float m_fTriggerSpeedKmh;
	protected float m_fPeakSpeedKmh;
	protected string m_sLockedPlate;
	protected float m_fFlashEndTime;

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
	protected bool m_bSnapPlateFlagged;

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
		PublishSnapshot(ERP_RadarVisualState.OFF, 0, "—", false, false);
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
			PublishSnapshot(ERP_RadarVisualState.SCANNING, 0, "—", false, false);
			GetGame().GetCallqueue().CallLater(TickServer, (int)(m_fScanIntervalSeconds * 1000), true);
			TickServer();
		}
		else
		{
			GetGame().GetCallqueue().Remove(TickServer);
			ResetLockState();
			m_eState = ERP_RadarVisualState.OFF;
			PublishSnapshot(ERP_RadarVisualState.OFF, 0, "—", false, false);
		}
	}

	// ----------------------------------------------------------------------
	// Local-replica readers for the HUD
	// ----------------------------------------------------------------------

	ERP_RadarVisualState GetSnapshotState()  { return m_eSnapState; }
	float                GetSnapshotSpeed()  { return m_fSnapSpeedKmh; }
	string               GetSnapshotPlate()  { return m_sSnapPlate; }
	bool                 GetSnapshotHasTarget() { return m_bSnapHasTarget; }
	bool                 GetSnapshotPlateFlagged() { return m_bSnapPlateFlagged; }

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
		if (target)
		{
			currentSpeedKmh = GetVehicleSpeedKmh(target);
			currentPlate = GetVehiclePlate(target);
		}

		float now = GetWorldTimeSeconds();

		// What we'll publish at end of tick. Snapshot is the single
		// per-tick broadcast that carries state + display fields, so
		// late-joining clients self-heal within one tick interval.
		float pubSpeed = 0;
		string pubPlate = "—";
		bool pubHasTarget = false;
		bool pubPlateFlagged = false;

		switch (m_eState)
		{
			case ERP_RadarVisualState.SCANNING:
			{
				if (target && currentSpeedKmh > m_fSpeedLimitKmh)
				{
					m_LockedVehicle = target;
					m_fTriggerSpeedKmh = currentSpeedKmh;
					m_fPeakSpeedKmh = currentSpeedKmh;
					m_sLockedPlate = currentPlate;
					m_fFlashEndTime = now + m_fFlashDurationSec;
					m_eState = ERP_RadarVisualState.FLASHING;
					pubSpeed = m_fTriggerSpeedKmh;
					pubPlate = m_sLockedPlate;
					pubHasTarget = true;
					pubPlateFlagged = true;
					if (!m_sLockSoundEvent.IsEmpty())
					{
						bool hostHasPC = GetGame().GetPlayerController() != null;
						Print(string.Format("[RP_SpeedRadarLogic] %1 Lock fired (NORMAL->FLASHING). plate=%2 speed=%3 km/h event='%4' broadcasting RPC, serverSelfLoopback=%5 (skipped on dedi).",
							RoleTag(), m_sLockedPlate, m_fTriggerSpeedKmh, m_sLockSoundEvent, hostHasPC));
						// Broadcast to every client; each one plays the
						// beep locally through its own SpeedRadar prop's
						// SoundComponent.
						Rpc(RpcDo_LockSound);
						// Server-self loopback for listen-server hosts —
						// broadcast doesn't deliver back to the sender,
						// so without this the workbench host would hear
						// nothing. Skipped on a dedicated server, which
						// has no PlayerController (and no audio output
						// anyway — SoundComponents aren't realized on a
						// headless server).
						if (hostHasPC)
							PlayLockSoundLocal();
					}
					break;
				}
				if (target)
				{
					pubSpeed = currentSpeedKmh;
					pubPlate = currentPlate;
					pubHasTarget = true;
					pubPlateFlagged = IsPlateFlagged(currentPlate);
				}
				break;
			}

			case ERP_RadarVisualState.FLASHING:
			{
				// Track peak silently while the locked vehicle is in cone.
				if (m_LockedVehicle && IsVehicleInCone(copCar, m_LockedVehicle))
				{
					float lockedSpeed = GetVehicleSpeedKmh(m_LockedVehicle);
					if (lockedSpeed > m_fPeakSpeedKmh)
						m_fPeakSpeedKmh = lockedSpeed;
				}
				if (now >= m_fFlashEndTime)
				{
					m_eState = ERP_RadarVisualState.LOCKED;
					pubSpeed = m_fPeakSpeedKmh;
				}
				else
				{
					// Still flashing — speed text frozen at trigger value.
					pubSpeed = m_fTriggerSpeedKmh;
				}
				pubPlate = m_sLockedPlate;
				pubHasTarget = true;
				pubPlateFlagged = true;
				break;
			}

			case ERP_RadarVisualState.LOCKED:
			{
				if (m_LockedVehicle && IsVehicleInCone(copCar, m_LockedVehicle))
				{
					float lockedSpeed = GetVehicleSpeedKmh(m_LockedVehicle);
					if (lockedSpeed > m_fPeakSpeedKmh)
						m_fPeakSpeedKmh = lockedSpeed;
				}
				pubSpeed = m_fPeakSpeedKmh;
				pubPlate = m_sLockedPlate;
				pubHasTarget = true;
				pubPlateFlagged = true;
				break;
			}

			default:
				break;
		}

		PublishSnapshot(m_eState, pubSpeed, pubPlate, pubHasTarget, pubPlateFlagged);
	}

	protected void ResetLockState()
	{
		m_LockedVehicle = null;
		m_fTriggerSpeedKmh = 0;
		m_fPeakSpeedKmh = 0;
		m_sLockedPlate = "";
		m_fFlashEndTime = 0;
	}

	// ----------------------------------------------------------------------
	// Broadcast — per-tick snapshot (state + display fields)
	// ----------------------------------------------------------------------
	//
	// Single broadcast that carries everything a client needs. Sent every
	// tick while active, so a late-joining client receives a complete
	// snapshot within one tick interval and the radar self-heals.

	protected void PublishSnapshot(ERP_RadarVisualState state, float speed, string plate, bool hasTarget, bool plateFlagged)
	{
		// Apply locally on server first — Broadcast does not loop back to
		// the sender, so the host has to drive its own visuals here.
		ApplySnapshotLocal(state, speed, plate, hasTarget, plateFlagged);
		Rpc(RpcDo_Snapshot, state, speed, plate, hasTarget, plateFlagged);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Snapshot(ERP_RadarVisualState state, float speed, string plate, bool hasTarget, bool plateFlagged)
	{
		ApplySnapshotLocal(state, speed, plate, hasTarget, plateFlagged);
	}

	protected void ApplySnapshotLocal(ERP_RadarVisualState state, float speed, string plate, bool hasTarget, bool plateFlagged)
	{
		m_eSnapState = state;
		m_fSnapSpeedKmh = speed;
		m_sSnapPlate = plate;
		m_bSnapHasTarget = hasTarget;
		m_bSnapPlateFlagged = plateFlagged;
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

	// Plays the lock beep from the SpeedRadar prop's RP_CopAudioComponent.
	// Invoked by RpcDo_LockSound on remote clients; the server-self call
	// happens directly in TickServer's lock-trigger branch since
	// Rpc(RplRcver.Broadcast) does not loop back to the sender.
	protected void PlayLockSoundLocal()
	{
		string role = RoleTag();
		if (m_sLockSoundEvent.IsEmpty())
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlayLockSoundLocal: m_sLockSoundEvent is empty — skipped (configured silent).", role));
			return;
		}
		IEntity copCar = GetOwner();
		if (!copCar)
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 PlayLockSoundLocal: GetOwner()==null — radar beep skipped.", role), LogLevel.WARNING);
			return;
		}
		Print(string.Format("[RP_SpeedRadarLogic] %1 PlayLockSoundLocal: entering. owner=%2 event='%3' — searching for RP_CopAudioComponent.",
			role, GetOwnerPlate(copCar), m_sLockSoundEvent));
		IEntity audioHost;
		RP_CopAudioComponent sc = FindCopAudio(copCar, audioHost);
		if (!sc)
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 No RP_CopAudioComponent found on cop car or its slot/child entities — radar beep skipped.", role), LogLevel.WARNING);
			return;
		}
		AudioHandle h = sc.SoundEvent(m_sLockSoundEvent);
		// AudioHandle.Invalid means the engine could not resolve the event
		// (e.g. .acp bank not loaded on this peer, event name typo, or a
		// malformed .acp). Either log path is informational once we're
		// past the discovery phase — the radar is otherwise self-healing.
		if (h == AudioHandle.Invalid)
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 SoundEvent('%2') returned AudioHandle.Invalid — event not resolved. No audio will play.",
				role, m_sLockSoundEvent), LogLevel.WARNING);
		}
		else
		{
			Print(string.Format("[RP_SpeedRadarLogic] %1 SoundEvent('%2') issued — handle=%3 (valid).",
				role, m_sLockSoundEvent, h));
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
		// POC stub — same as the old HUD path. Wire to a real watchlist
		// later (server-side, since this lives here now).
		//
		// Future enhancement: filter out PD plates here (and in the cone
		// scan in FindVehicleInCone). Today the cone scan excludes the
		// cop's own car but not other cop cars in the registry, so a
		// partner unit speeding past will lock and display its PD plate.
		// Acceptable for now; revisit if it produces false positives once
		// the watchlist is real.
		return false;
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
