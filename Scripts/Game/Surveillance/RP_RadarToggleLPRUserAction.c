/**
 * RP_RadarToggleLPRUserAction
 *
 * On-prop toggle: enables/disables the radar's plate-reader (LPR)
 * detection channel. When off, watched plates no longer trigger a lock
 * or alert; overspeed detection is unaffected. Server-authoritative -
 * routed through the relay.
 */
class RP_RadarToggleLPRUserAction : RP_RadarUserActionBase
{
	override bool GetActionNameScript(out string outName)
	{
		outName = "Toggle Plate Reader";
		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		IEntity copCar = FindRadarVehicle(pOwnerEntity);
		if (!copCar)
		{
			Print("[RP_RadarAction] ToggleLPR: no radar vehicle found from owner.", LogLevel.WARNING);
			return;
		}
		RP_PlayerRpcRelayComponent relay = GetRelay();
		if (!relay)
		{
			Print("[RP_RadarAction] ToggleLPR: no local RP_PlayerRpcRelayComponent - toggle skipped.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarToggleLPR(copCar);
	}
}
