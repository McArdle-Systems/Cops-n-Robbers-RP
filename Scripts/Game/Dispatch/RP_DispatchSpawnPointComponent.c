/**
 * RP_DispatchSpawnPointComponent
 *
 * Marks an entity as a spawn point for the dispatch system. Place a
 * GenericEntity in the world at the desired spawn location, attach
 * this component, and reference the entity's Workbench name from
 * RP_DispatchGroupDefinition.m_sSpawnPointName.
 *
 * The lookup key is the owning entity's name (set in Workbench). An
 * optional m_sNameOverride lets you register under a different name if
 * the entity is named for something else.
 */
[ComponentEditorProps(category: "RP/Dispatch", description: "Marks the owning entity as a named spawn point for the dispatch system.")]
class RP_DispatchSpawnPointComponentClass : ScriptComponentClass
{
}

class RP_DispatchSpawnPointComponent : ScriptComponent
{
	[Attribute(desc: "Optional override. If empty, the owning entity's Workbench name is used as the lookup key.")]
	protected string m_sName;

	protected string m_sRegisteredAs;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		// Manager may not be initialised yet; defer registration.
		GetGame().GetCallqueue().CallLater(TryRegister, 100, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryRegister);
		RP_DispatchManagerComponent mgr = RP_DispatchManagerComponent.GetInstance();
		if (mgr && !m_sRegisteredAs.IsEmpty())
			mgr.UnregisterSpawnPoint(m_sRegisteredAs);
		super.OnDelete(owner);
	}

	protected void TryRegister()
	{
		RP_DispatchManagerComponent mgr = RP_DispatchManagerComponent.GetInstance();
		if (!mgr)
			return;
		GetGame().GetCallqueue().Remove(TryRegister);

		string name = m_sName;
		if (name.IsEmpty())
			name = GetOwner().GetName();

		if (name.IsEmpty())
		{
			Print("[RP_Dispatch] Spawn point owning entity has no name and no override — can't register.", LogLevel.WARNING);
			return;
		}

		m_sRegisteredAs = name;
		mgr.RegisterSpawnPoint(name, GetOwner());
	}

	string GetRegisteredName()
	{
		return m_sRegisteredAs;
	}
}
