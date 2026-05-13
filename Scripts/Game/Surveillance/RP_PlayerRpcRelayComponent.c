/**
 * RP_PlayerRpcRelayComponent
 *
 * Per-player client→server RPC bridge. Lives on the player character
 * prefab so the local player owns it for replication purposes. Use
 * this whenever a client UI needs to ask the server to do something
 * that affects shared/replicated state.
 *
 * Why this exists: on this dedicated build, [RplRpc(..., RplRcver.Server)]
 * methods declared on SCR_BaseGameModeComponent-derived classes never
 * receive their messages from connecting clients. The Rpc() call on
 * the client returns without error, but the server-side handler is
 * never invoked. (Plate broadcast / Server→Broadcast direction works
 * fine — the bug is specific to client→server on GameMode-owned
 * components, since the GameMode entity is server-owned and the
 * calling client has no send-authority through its RplComponent.)
 *
 * The character entity, by contrast, is owned by the controlling
 * client — Rpc() through a component attached to it routes reliably
 * to the server. This component is that route.
 *
 * Current methods:
 *   - RequestRadarPower(copCar, wantOn): drives the cop vehicle's
 *     AG0 MFD slot 0 on/off via TogglePowerAction. The MFD toggle is
 *     server-authoritative; AG0's promised internal client→server
 *     hop is a silent no-op in this framework version (verified via
 *     dedi log: post-Toggle IsMFDOn=0 on the calling client), so we
 *     route through here instead.
 *
 * Adding more methods: mirror the same pattern — public entry checks
 * Replication.IsServer() and either calls the Apply* helper directly
 * or Rpc's the RpcAsk_* handler. Keep the Apply* helper free of
 * "are we the server?" branches so both paths share it.
 */

[ComponentEditorProps(category: "RP/Network", description: "Per-player client→server RPC bridge. Attach to player character prefabs. Required for HUDs to drive server-authoritative actions (radar MFD power, etc.).")]
class RP_PlayerRpcRelayComponentClass : ScriptComponentClass
{
}

class RP_PlayerRpcRelayComponent : ScriptComponent
{
	[Attribute(defvalue: "0", desc: "MFD slot index on the cop vehicle to toggle for the radar screen.")]
	protected int m_iRadarMFDSlotIndex;

	// Poll counter for late-join plate sync. Caps the wait at ~10s so
	// remote-proxy instances (other players' characters on this client)
	// don't poll forever.
	protected int m_iPlateSyncAttempts;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		// Server already holds the authoritative plate registry; nothing
		// to fetch. Only client instances of this relay need to ask.
		if (Replication.IsServer())
			return;
		GetGame().GetCallqueue().CallLater(TryFirePlateSync, 500, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryFirePlateSync);
		super.OnDelete(owner);
	}

	// Fires RequestPlateSync once we've confirmed this relay is on the
	// local player's controlled character. Each connected client has
	// replicas of every other player's character with this same
	// component attached, but GetLocal() singles out the one on their
	// own controlled entity — only that instance should fire the sync.
	protected void TryFirePlateSync()
	{
		if (GetLocal() == this)
		{
			GetGame().GetCallqueue().Remove(TryFirePlateSync);
			RequestPlateSync();
			return;
		}
		m_iPlateSyncAttempts++;
		if (m_iPlateSyncAttempts > 20)
		{
			// ~10s elapsed. Either we're a remote-proxy instance (the
			// local owner's relay will fire its own sync) or the local
			// player still has not been assigned to a character. Stop
			// polling either way.
			GetGame().GetCallqueue().Remove(TryFirePlateSync);
		}
	}

	// ----------------------------------------------------------------------
	// Public entry: radar MFD power
	// ----------------------------------------------------------------------

	void RequestRadarPower(IEntity copCar, bool wantOn)
	{
		if (!copCar)
			return;
		if (Replication.IsServer())
		{
			ApplyRadarPower(copCar, wantOn);
			return;
		}
		RplComponent rpl = RplComponent.Cast(copCar.FindComponent(RplComponent));
		if (!rpl)
		{
			Print(string.Format("[RP_RpcRelay] RequestRadarPower: copCar %1 has no RplComponent — RPC skipped.", copCar), LogLevel.WARNING);
			return;
		}
		Rpc(RpcAsk_SetRadarPower, rpl.Id(), wantOn);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetRadarPower(RplId vehicleId, bool wantOn)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(vehicleId));
		if (!rpl)
		{
			Print(string.Format("[RP_RpcRelay] RpcAsk_SetRadarPower: no entity for RplId=%1 on server.", vehicleId), LogLevel.WARNING);
			return;
		}
		ApplyRadarPower(rpl.GetEntity(), wantOn);
	}

	// Drives both the MFD screen power and the server-side radar logic
	// in lockstep. Screen power is idempotent-checked against
	// IsMFDOn; logic activation is idempotent inside SetActive itself.
	protected void ApplyRadarPower(IEntity copCar, bool wantOn)
	{
		if (!copCar)
			return;
		AG0_MFDManagerComponent mgr = AG0_MFDManagerComponent.Cast(copCar.FindComponent(AG0_MFDManagerComponent));
		if (!mgr)
		{
			Print(string.Format("[RP_RpcRelay] ApplyRadarPower: cop car %1 has no AG0_MFDManagerComponent.", copCar), LogLevel.WARNING);
		}
		else if (wantOn != mgr.IsMFDOn(m_iRadarMFDSlotIndex))
		{
			mgr.TogglePowerAction(m_iRadarMFDSlotIndex);
		}

		RP_SpeedRadarLogicComponent logic = RP_SpeedRadarLogicComponent.FindOnVehicle(copCar);
		if (!logic)
		{
			Print(string.Format("[RP_RpcRelay] ApplyRadarPower: cop car %1 has no RP_SpeedRadarLogicComponent — radar will not scan.", copCar), LogLevel.WARNING);
			return;
		}
		logic.SetActive(wantOn);
	}

	// ----------------------------------------------------------------------
	// Public entry: dispatch request
	// ----------------------------------------------------------------------

	void RequestDispatch(string typeTag, vector targetPos)
	{
		if (Replication.IsServer())
		{
			ApplyDispatch(typeTag, targetPos);
			return;
		}
		Rpc(RpcAsk_Dispatch, typeTag, targetPos);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Dispatch(string typeTag, vector targetPos)
	{
		Print(string.Format("[RP_RpcRelay] RpcAsk_Dispatch: server received type=%1 pos=%2", typeTag, targetPos), LogLevel.NORMAL);
		ApplyDispatch(typeTag, targetPos);
	}

	protected void ApplyDispatch(string typeTag, vector targetPos)
	{
		RP_DispatchManagerComponent mgr = RP_DispatchManagerComponent.GetInstance();
		if (!mgr)
		{
			Print("[RP_RpcRelay] ApplyDispatch: dispatch manager not available.", LogLevel.WARNING);
			return;
		}
		mgr.Dispatch(typeTag, targetPos);
	}

	// ----------------------------------------------------------------------
	// Public entry: admin — set max active traffic
	// ----------------------------------------------------------------------

	void RequestSetMaxTraffic(int newTarget)
	{
		if (Replication.IsServer())
		{
			// Server can apply directly; still verify caller is admin (in
			// case this is a listen-server non-host calling).
			ApplySetMaxTraffic(newTarget, GetCallerPlayerId());
			return;
		}
		Rpc(RpcAsk_SetMaxTraffic, newTarget);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetMaxTraffic(int newTarget)
	{
		int senderId = GetCallerPlayerId();
		Print(string.Format("[RP_RpcRelay] RpcAsk_SetMaxTraffic: server received target=%1 from playerId=%2", newTarget, senderId), LogLevel.NORMAL);
		ApplySetMaxTraffic(newTarget, senderId);
	}

	// playerId is the original requester. Server re-checks admin before
	// applying — never trust the client's local IsLocalAdmin() gate.
	protected void ApplySetMaxTraffic(int newTarget, int senderPlayerId)
	{
		if (!RP_AdminUtils.IsPlayerAdmin(senderPlayerId))
		{
			Print(string.Format("[RP_RpcRelay] SetMaxTraffic rejected: playerId=%1 is not on the admin list.", senderPlayerId), LogLevel.WARNING);
			return;
		}
		RP_TrafficLoopComponent loop = RP_TrafficLoopComponent.GetInstance();
		if (!loop)
		{
			Print("[RP_RpcRelay] SetMaxTraffic: traffic loop component not available.", LogLevel.WARNING);
			return;
		}
		loop.SetTargetActiveCount(newTarget);
	}

	// ----------------------------------------------------------------------
	// Public entry: admin — fetch current max active traffic
	// ----------------------------------------------------------------------

	void RequestTrafficCap()
	{
		if (Replication.IsServer())
		{
			RP_TrafficLoopComponent loop = RP_TrafficLoopComponent.GetInstance();
			if (loop)
				RP_AdminPanelUI.ReceiveTrafficCap(loop.GetTargetActiveCount());
			return;
		}
		Rpc(RpcAsk_TrafficCap);
	}

	// Server side. Replies via owner-targeted RPC so only the asking
	// client gets it, not every connected client.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TrafficCap()
	{
		RP_TrafficLoopComponent loop = RP_TrafficLoopComponent.GetInstance();
		if (!loop)
		{
			Print("[RP_RpcRelay] RpcAsk_TrafficCap: traffic loop not available.", LogLevel.WARNING);
			return;
		}
		Rpc(RpcDo_TrafficCap, loop.GetTargetActiveCount());
	}

	// Client side, delivered to the player who owns this character.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_TrafficCap(int currentCap)
	{
		RP_AdminPanelUI.ReceiveTrafficCap(currentCap);
	}

	// Resolves the player who owns this character. On the server side of
	// an RPC, GetOwner() is the character entity the RPC came through; on
	// a listen-server local call it's still that same character, since
	// the relay is on the player char prefab.
	protected int GetCallerPlayerId()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return 0;
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return 0;
		return pm.GetPlayerIdFromControlledEntity(owner);
	}

	// ----------------------------------------------------------------------
	// Late-join: ask the server to re-broadcast the plate registry
	// ----------------------------------------------------------------------
	//
	// RP_TrafficLoopComponent lives on the GameMode (server-owned), so a
	// client->server RPC declared on it is silently dropped — same gotcha
	// as the radar-power toggle. Route the resync through here instead:
	// this component is on the player character (client-owned), so the
	// RplRcver.Server delivery actually fires.
	//
	// Triggered once per local-player relay (see TryFirePlateSync below).
	// On the server side the request is idempotent — re-broadcasting the
	// map just re-writes the same values on each client.

	void RequestPlateSync()
	{
		if (Replication.IsServer())
		{
			ApplyPlateSync();
			return;
		}
		Rpc(RpcAsk_SyncPlates);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SyncPlates()
	{
		ApplyPlateSync();
	}

	protected void ApplyPlateSync()
	{
		RP_TrafficLoopComponent loop = RP_TrafficLoopComponent.GetInstance();
		if (!loop)
		{
			Print("[RP_RpcRelay] ApplyPlateSync: traffic loop not available.", LogLevel.WARNING);
			return;
		}
		loop.BroadcastAllPlates();
	}

	// ----------------------------------------------------------------------
	// Lookup helper for callers (e.g. HUDs)
	// ----------------------------------------------------------------------

	// Finds the relay on the local player's controlled character. Returns
	// null if the player isn't controlling a character that has the
	// component attached (e.g. menu / spectator / non-cop loadout).
	static RP_PlayerRpcRelayComponent GetLocal()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;
		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
			return null;
		return RP_PlayerRpcRelayComponent.Cast(controlled.FindComponent(RP_PlayerRpcRelayComponent));
	}
}
