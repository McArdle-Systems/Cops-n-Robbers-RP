/**
 * RP_RadarTogglePowerUserAction
 *
 * Power button: toggles the radar unit's power (server-routed) and mirrors
 * the seated cop's HUD overlay - opens on power-on, closes on power-off.
 * On-foot officers / non-cops still toggle the unit's power but don't get
 * the cop overlay.
 *
 * Toggle direction is read from the radar's replicated snapshot state
 * (OFF == powered down), so a press flips whatever the unit currently
 * shows.
 */
class RP_RadarTogglePowerUserAction : RP_RadarUserActionBase
{
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		IEntity copCar = FindRadarVehicle(pOwnerEntity);
		if (!copCar)
		{
			Print("[RP_RadarAction] TogglePower: no radar vehicle found from owner.", LogLevel.WARNING);
			return;
		}
		RP_PlayerRpcRelayComponent relay = GetRelay();
		if (!relay)
		{
			Print("[RP_RadarAction] TogglePower: no local RP_PlayerRpcRelayComponent - toggle skipped.", LogLevel.WARNING);
			return;
		}

		// Flip the current (replicated) power state. A non-OFF snapshot
		// state means the unit is running.
		bool isOn = false;
		RP_SpeedRadarLogicComponent logic = RP_SpeedRadarLogicComponent.FindOnVehicle(copCar);
		if (logic)
			isOn = logic.GetSnapshotState() != ERP_RadarVisualState.OFF;
		bool wantOn = !isOn;

		relay.RequestRadarPower(copCar, wantOn);

		// Mirror the cop overlay locally (no-op for on-foot / non-cop).
		RP_SurveillanceHUDComponent hud = RP_SurveillanceHUDComponent.GetInstance();
		if (hud)
			hud.NotifyPowerToggledFromAction(wantOn);
	}
}
