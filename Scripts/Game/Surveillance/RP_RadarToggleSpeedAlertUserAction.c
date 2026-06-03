/**
 * RP_RadarToggleSpeedAlertUserAction
 *
 * On-prop toggle: enables/disables the radar's speed-alert detection channel.
 * When off, overspeed targets no longer trigger a lock or alert; plate-reader
 * detection and the live speed readout are unaffected. Server-authoritative -
 * routed through the relay. Sibling of RP_RadarToggleLPRUserAction.
 */
class RP_RadarToggleSpeedAlertUserAction : RP_RadarUserActionBase
{
	override bool GetActionNameScript(out string outName)
	{
		// Label shows the state a press will switch the speed alert TO.
		// Enabled => next press disables, so show "Speed Alert: OFF". Reads
		// the replicated snapshot flag so it's live on every client.
		bool isOn = false;
		RP_SpeedRadarLogicComponent logic = GetRadarLogic();
		if (logic)
			isOn = logic.GetSnapshotSpeedAlertEnabled();
		if (isOn)
			outName = "Speed Alert: OFF";
		else
			outName = "Speed Alert: ON";
		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		IEntity copCar = FindRadarVehicle(pOwnerEntity);
		if (!copCar)
		{
			Print("[RP_RadarAction] ToggleSpeedAlert: no radar vehicle found from owner.", LogLevel.WARNING);
			return;
		}
		RP_PlayerRpcRelayComponent relay = GetRelay();
		if (!relay)
		{
			Print("[RP_RadarAction] ToggleSpeedAlert: no local RP_PlayerRpcRelayComponent - toggle skipped.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarToggleSpeedAlert(copCar);
	}
}
