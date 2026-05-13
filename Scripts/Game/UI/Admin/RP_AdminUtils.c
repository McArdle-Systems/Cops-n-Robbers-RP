/**
 * RP_AdminUtils
 *
 * Two-tier admin check:
 *   - IsLocalAdmin(): for client-side UI gating (hide buttons). Treats
 *     the local server-authority as admin so workbench play and listen
 *     hosts work without a server config admins entry.
 *   - IsPlayerAdmin(playerId): for SERVER-side re-checks in RPC handlers.
 *     Reads SCR_PlayerListedAdminManagerComponent, which is fed by the
 *     `admins` array in serverConfig.json. Never trust the client gate;
 *     always re-check on the server before applying.
 */
class RP_AdminUtils
{
	static bool IsLocalAdmin()
	{
		// Workbench play / listen-server host: short-circuit so we don't
		// need a serverConfig admins entry to test locally.
		if (Replication.IsServer())
			return true;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;
		return IsPlayerAdmin(pc.GetPlayerId());
	}

	static bool IsPlayerAdmin(int playerId)
	{
		if (playerId <= 0)
			return false;

		// Listen-server / workbench host shortcut. On the server side
		// of an RPC, the local PlayerController only exists when the
		// server process is *also* a player (workbench play, listen
		// host); on a dedicated server it's null. So if a local PC
		// exists and its playerId matches the sender, the sender is
		// the host — trivially admin, even without a serverConfig
		// admins entry. Guarded by Replication.IsServer() so a remote
		// client calling this function can't claim admin by passing
		// their own id (local PC.GetPlayerId() == them would otherwise
		// be a free admin grant).
		if (Replication.IsServer())
		{
			PlayerController localPc = GetGame().GetPlayerController();
			if (localPc && localPc.GetPlayerId() == playerId)
				return true;
		}

		SCR_PlayerListedAdminManagerComponent mgr = SCR_PlayerListedAdminManagerComponent.GetInstance();
		if (!mgr)
			return false;
		return mgr.IsPlayerOnAdminList(playerId);
	}
}
