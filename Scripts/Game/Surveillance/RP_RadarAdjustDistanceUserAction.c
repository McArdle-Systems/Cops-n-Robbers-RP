/**
 * RP_RadarAdjustDistanceUserAction
 *
 * On-prop knob action: adds m_fDeltaMeters to the radar's cone range.
 * Drop it twice on the Distance point - once with a positive delta
 * (increase) and once with a negative delta (decrease). The logic
 * component clamps the result to its configured min/max.
 */
class RP_RadarAdjustDistanceUserAction : RP_RadarUserActionBase
{
	[Attribute(defvalue: "5.0", desc: "Metres added to the cone range per press. Use a negative value for the 'decrease' knob.")]
	protected float m_fDeltaMeters;

	// Live label: "Distance +: 50 m". Reads the replicated cone-range signal
	// through the logic component (server-authored, so every client sees the
	// current value). Falls back to the static label until the logic resolves.
	override bool GetActionNameScript(out string outName)
	{
		string sign;
		if (m_fDeltaMeters >= 0)
			sign = "+";
		else
			sign = "-";

		RP_SpeedRadarLogicComponent logic = GetRadarLogic();
		if (logic)
			outName = string.Format("Distance %1: %2 m", sign, (int)Math.Round(logic.GetConeRange()));
		else
			outName = string.Format("Distance %1", sign);

		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		IEntity copCar = FindRadarVehicle(pOwnerEntity);
		if (!copCar)
		{
			Print("[RP_RadarAction] AdjustDistance: no radar vehicle found from owner.", LogLevel.WARNING);
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
				Print("[RP_RadarAction] AdjustDistance: no local RP_PlayerRpcRelayComponent - change skipped.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarAdjustDistance(copCar, m_fDeltaMeters);
	}
}
