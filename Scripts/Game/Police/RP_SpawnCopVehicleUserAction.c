/**
 * RP_SpawnCopVehicleUserAction
 *
 * UserActionContext action that asks the nearest RP_CopVehicleSpawnerComponent
 * (walking up the entity hierarchy from the action's owner) to dispense a
 * vehicle.
 *
 * The spawner component does the server-authoritative work; this action is
 * just the interaction surface. CanBePerformedScript surfaces faction and
 * occupancy refusals to the client UI so the prompt explains itself.
 */

class RP_SpawnCopVehicleUserAction : ScriptedUserAction
{
	override bool CanBePerformedScript(IEntity user)
	{
		RP_CopVehicleSpawnerComponent spawner = FindSpawner(GetOwner());
		if (!spawner)
			return true;

		if (!spawner.IsAllowedFaction(user))
		{
			SetCannotPerformReason("Police only.");
			return false;
		}

		if (!spawner.IsPadClear())
		{
			SetCannotPerformReason("Spawn pad is occupied.");
			return false;
		}

		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		RP_CopVehicleSpawnerComponent spawner = FindSpawner(pOwnerEntity);
		if (!spawner)
		{
			Print("[RP_SpawnCopVehicle] No RP_CopVehicleSpawnerComponent found on owner or ancestors.", LogLevel.WARNING);
			return;
		}
		spawner.RequestSpawn(pUserEntity);
	}

	protected RP_CopVehicleSpawnerComponent FindSpawner(IEntity start)
	{
		IEntity current = start;
		while (current)
		{
			RP_CopVehicleSpawnerComponent comp = RP_CopVehicleSpawnerComponent.Cast(current.FindComponent(RP_CopVehicleSpawnerComponent));
			if (comp)
				return comp;
			current = current.GetParent();
		}
		return null;
	}
}
