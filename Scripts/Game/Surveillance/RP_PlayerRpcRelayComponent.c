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

	protected void ApplyRadarPower(IEntity copCar, bool wantOn)
	{
		if (!copCar)
			return;
		AG0_MFDManagerComponent mgr = AG0_MFDManagerComponent.Cast(copCar.FindComponent(AG0_MFDManagerComponent));
		if (!mgr)
		{
			Print(string.Format("[RP_RpcRelay] ApplyRadarPower: cop car %1 has no AG0_MFDManagerComponent.", copCar), LogLevel.WARNING);
			return;
		}
		if (wantOn == mgr.IsMFDOn(m_iRadarMFDSlotIndex))
			return;
		mgr.TogglePowerAction(m_iRadarMFDSlotIndex);
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
