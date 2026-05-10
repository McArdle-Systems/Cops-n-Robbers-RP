/**
 * RP_DriverComplianceComponent
 *
 * Tag-and-policy component that marks an AI group as a yield candidate
 * and decides whether the group complies with an emergency vehicle
 * pull-over request. Attached to the **SCR_AIGroup prefab**.
 *
 * Two distinct concerns, both naturally group-scoped:
 *   - **Compliance decision** ("does this group yield?") — one answer
 *     per group. Group is the policy unit; a future flee/pursuit
 *     decision belongs here, not on individual drivers.
 *   - **Target selection** ("which driver pulls over?") — the manager
 *     identifies the specific driver whose vehicle is in the cop's
 *     bubble and the group routes the pull-over waypoint to that
 *     driver. Multi-vehicle convoys yield only the affected driver,
 *     not the whole convoy.
 *
 * RP_EmergencyYieldComponent finds this by walking the seated driver
 * character → AIControlComponent → AIAgent → parent SCR_AIGroup.
 * Groups without this component are treated as not-applicable (player
 * drivers, dispatched units that shouldn't yield, etc.). Groups
 * returning false from WillComplyWithPullOver are AI-but-non-compliant
 * — currently logged and ignored, future hook for flee / pursuit logic.
 *
 * Compliance is stubbed always-true for Phase 1. Real logic plugs in
 * here later: wanted-status check, panic state, scripted-flee flag,
 * etc.
 */

[ComponentEditorProps(category: "RP/Traffic", description: "Marks an AI group as a yield candidate. Attach to the SCR_AIGroup prefab. Stub returns always-comply; future flee logic plugs in here.")]
class RP_DriverComplianceComponentClass : ScriptComponentClass
{
}

class RP_DriverComplianceComponent : ScriptComponent
{
	// Server-only registry. The yield manager iterates this and asks
	// each group "do you contain the driver of this candidate vehicle?"
	// — bypasses AIAgent.GetParentGroup quirks where the parent
	// relationship doesn't reliably point at the SCR_AIGroup we expect.
	protected static ref array<RP_DriverComplianceComponent> s_aInstances = {};

	static array<RP_DriverComplianceComponent> GetInstances() { return s_aInstances; }

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
			return;
		s_aInstances.Insert(this);
		Print(string.Format("[RP_Yield] RP_DriverComplianceComponent registered on group %1 (registry size %2).", owner, s_aInstances.Count()), LogLevel.NORMAL);
	}

	override void OnDelete(IEntity owner)
	{
		int idx = s_aInstances.Find(this);
		if (idx >= 0)
			s_aInstances.Remove(idx);
		super.OnDelete(owner);
	}

	bool WillComplyWithPullOver()
	{
		return true;
	}
}
