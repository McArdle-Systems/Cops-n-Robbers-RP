/**
 * RP_RadarSignalAdjustUserAction
 *
 * Thin subclasses of the vanilla signal increase/decrease knob actions that
 * append the signal's current value to the action label, so a seated cop can
 * read where the setting sits (e.g. "Distance +: 50 m") without a separate
 * readout. GetCurrentValue() reads the replicated signal, and the
 * interaction UI re-queries the name each frame, so the number tracks live.
 *
 * The behaviour (hold-to-ramp, clamp to Min/Max value) is entirely inherited
 * — these only override the display name.
 */
class RP_RadarSignalLabel
{
	// Builds "<label>: <value> <unit>" from the action's UIInfo name, the
	// current (rounded) signal value, and a unit suffix. Falls back to just
	// "<value> <unit>" if the action has no UIInfo name.
	static string Build(UIInfo info, float value, string unit)
	{
		int v = (int)Math.Round(value);
		string label;
		if (info)
			label = info.GetName();
		if (label.IsEmpty())
			return string.Format("%1 %2", v, unit);
		return string.Format("%1: %2 %3", label, v, unit);
	}
}

// Applies one discrete step to the action's signal. The native
// SCR_ScriptedSignalUserAction*crease classes only ramp the value from
// PerformContinuousAction (hold-to-ramp); their PerformAction is a no-op, so a
// single in-vehicle press moved nothing. We instead apply a fixed step per
// press through SetSignalValue (the native sync-aware setter), clamped to the
// action's configured Min/Max. signedStep is +step for increase, -step for
// decrease.
class RP_RadarSignalStep
{
	static void Apply(ScriptedSignalUserAction act, float signedStep)
	{
		float next = Math.Clamp(act.GetCurrentValue() + signedStep, act.GetMinimumValue(), act.GetMaximumValue());
		act.SetSignalValue(next);
	}
}

class RP_RadarSignalIncreaseUserAction : SCR_ScriptedSignalUserActionIncrease
{
	[Attribute(defvalue: "", desc: "Unit suffix shown after the live value in the action label, e.g. 'm' or 'km/h'.")]
	protected string m_sValueUnit;

	[Attribute(defvalue: "5", desc: "Amount the signal increases per press.")]
	protected float m_fStepPerPress;

	override bool GetActionNameScript(out string outName)
	{
		outName = RP_RadarSignalLabel.Build(GetUIInfo(), GetCurrentValue(), m_sValueUnit);
		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		RP_RadarSignalStep.Apply(this, m_fStepPerPress);
	}
}

class RP_RadarSignalDecreaseUserAction : SCR_ScriptedSignalUserActionDecrease
{
	[Attribute(defvalue: "", desc: "Unit suffix shown after the live value in the action label, e.g. 'm' or 'km/h'.")]
	protected string m_sValueUnit;

	[Attribute(defvalue: "5", desc: "Amount the signal decreases per press.")]
	protected float m_fStepPerPress;

	override bool GetActionNameScript(out string outName)
	{
		outName = RP_RadarSignalLabel.Build(GetUIInfo(), GetCurrentValue(), m_sValueUnit);
		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		RP_RadarSignalStep.Apply(this, -m_fStepPerPress);
	}
}
