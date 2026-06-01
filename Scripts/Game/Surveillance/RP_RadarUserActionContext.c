/**
 * RP_RadarUserActionContext
 *
 * Thin diagnostic context for radar action points. UserActionContext itself
 * exposes mostly engine-owned discovery behavior, so this class mainly tags
 * radar contexts and gives action logs a single place to describe them.
 */
class RP_RadarUserActionContext : UserActionContext
{
	string GetRadarDebugDescription()
	{
		return string.Format("name='%1' radius=%2 origin=%3 actions=%4",
			GetContextName(),
			GetRadius(),
			GetOrigin(),
			GetActionsCount());
	}
}
