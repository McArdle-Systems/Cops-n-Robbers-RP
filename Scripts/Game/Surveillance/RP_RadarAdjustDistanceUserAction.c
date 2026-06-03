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

	override bool GetActionNameScript(out string outName)
	{
		if (m_fDeltaMeters >= 0)
			outName = "Distance +";
		else
			outName = "Distance -";

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
			Print("[RP_RadarAction] AdjustDistance: no local RP_PlayerRpcRelayComponent - change skipped.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarAdjustDistance(copCar, m_fDeltaMeters);
	}
}
