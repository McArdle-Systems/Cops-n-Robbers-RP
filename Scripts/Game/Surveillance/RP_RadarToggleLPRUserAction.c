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
		// Label shows the state a press will switch the plate reader TO.
		// Enabled => next press disables, so show "LPR: OFF". Reads the
		// replicated snapshot flag so it's live on every client.
		bool isOn = false;
		RP_SpeedRadarLogicComponent logic = GetRadarLogic();
		if (logic)
			isOn = logic.GetSnapshotPlateReaderEnabled();
		if (isOn)
			outName = "LPR Alert: OFF";
		else
			outName = "LPR Alert: ON";
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
