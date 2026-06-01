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
			Print("[RP_RadarAction] AdjustTargetSpeed: no local RP_PlayerRpcRelayComponent - change skipped.", LogLevel.WARNING);
			return;
		}
		relay.RequestRadarAdjustSpeed(copCar, m_fDeltaKmh);
	}
}
