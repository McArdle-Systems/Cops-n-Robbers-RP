/**
 * RP_RadarAdjustTargetSpeedUserAction
 *
 * On-prop knob action: adds m_fDeltaKmh to the radar's target speed (the
 * speed limit that triggers the beep / red flash / lock). Drop it twice
 * on the Target Speed point - once with a positive delta (increase) and
 * once with a negative delta (decrease). The logic component clamps the
 * result to its configured min/max.
 */
class RP_RadarAdjustTargetSpeedUserAction : RP_RadarUserActionBase
{
	[Attribute(defvalue: "5.0", desc: "km/h added to the target speed (alert threshold) per press. Use a negative value for the 'decrease' knob.")]
	protected float m_fDeltaKmh;

	// Live label: "Speed +: 40 km/h". Reads the replicated target-speed signal
	// through the logic component (server-authored, so every client sees the
	// current value). Falls back to the static label until the logic resolves.
	override bool GetActionNameScript(out string outName)
	{
		string sign;
		if (m_fDeltaKmh >= 0)
			sign = "+";
		else
			sign = "-";

		RP_SpeedRadarLogicComponent logic = GetRadarLogic();
		if (logic)
			outName = string.Format("Speed %1: %2 km/h", sign, (int)Math.Round(logic.GetSpeedLimit()));
		else
			outName = string.Format("Speed %1", sign);

		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		IEntity copCar = FindRadarVehicle(pOwnerEntity);
		if (!copCar)
		{
			Print("[RP_RadarAction] AdjustTargetSpeed: no radar vehicle found from owner.", LogLevel.WARNING);
			return;
		}
		RP_PlayerRpcRelayComponent relay = GetRelay();
		if (!relay)
		{
			// PerformAction also runs server-side on a dedicated server, where
			// there's no local player and thus no relay — expected, the
			// performing client's own run sends the RPC. Only warn when there
			// IS a local player but no relay (a real misconfiguration).
			if (GetGame().GetPlayerController())
				Print("[RP_RadarAction] AdjustTargetSpeed: no local RP_PlayerRpcRelayComponent - change skipped.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarAdjustSpeed(copCar, m_fDeltaKmh);
	}
}
