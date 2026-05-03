/**
 * RP_DispatchGroupDefinition
 *
 * One entry in the dispatch manager's group catalogue. Defines a
 * dispatchable type (e.g. HMMWV / Police / EMS), what to spawn, where
 * to spawn it, and the soft cap on concurrent units.
 */
[BaseContainerProps()]
class RP_DispatchGroupDefinition
{
	[Attribute(desc: "Type tag for this group (e.g. HMMWV, Police, EMS). The HUD button references this.")]
	string m_sTypeTag;

	[Attribute(desc: "AI crew group prefab.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	ResourceName m_sCrewGroupPrefab;

	[Attribute(desc: "Vehicle prefab the crew drives.", UIWidgets.ResourcePickerThumbnail, params: "et")]
	ResourceName m_sVehiclePrefab;

	[Attribute(desc: "Spawn point name. Must match a RP_DispatchSpawnPointComponent.m_sName placed in the world.")]
	string m_sSpawnPointName;

	[Attribute(defvalue: "2", desc: "Maximum concurrent units of this type.")]
	int m_iMaxSpawned;

	[Attribute(defvalue: "30", desc: "Seconds the crew loiters at the target before returning.")]
	float m_fLoiterSeconds;

	[Attribute(defvalue: "30", desc: "Distance from target at which the vehicle stops and crew dismounts.")]
	float m_fDismountDistanceMeters;

	[Attribute(defvalue: "5", desc: "On-foot approach radius — crew transitions to loiter once within this distance of the target.")]
	float m_fApproachRadiusMeters;

	[Attribute(defvalue: "10", desc: "Seconds allowed for crew to board a vehicle (timer-based; we don't poll vehicle occupancy).")]
	float m_fBoardingTimeSeconds;

	[Attribute(defvalue: "5", desc: "Seconds allowed for crew to dismount before they start moving on foot.")]
	float m_fDismountTimeSeconds;

	[Attribute(defvalue: "5", desc: "Minimum seconds to spend driving before dismount can trigger. Prevents instant-dismount when spawn is already close to the target.")]
	float m_fMinDriveSeconds;

	[Attribute(defvalue: "60", desc: "Seconds idle at the spawn point before the unit is despawned for cleanup.")]
	float m_fCleanupTimeoutSeconds;
}
