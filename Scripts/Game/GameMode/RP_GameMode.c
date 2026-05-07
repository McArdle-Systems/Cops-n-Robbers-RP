class RP_GameModeClass : SCR_BaseGameModeClass
{
}

class RP_GameMode : SCR_BaseGameMode
{
	override void OnPlayerSpawned(int playerId, IEntity controlledEntity)
	{
		super.OnPlayerSpawned(playerId, controlledEntity);
		SetupQuickSlots(controlledEntity);
	}

	protected void SetupQuickSlots(IEntity controlledEntity)
	{
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(controlledEntity);
		if (!character) return;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory) return;

		SCR_CharacterInventoryStorageComponent charStorage = SCR_CharacterInventoryStorageComponent.Cast(character.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!charStorage) return;

		RP_PrefabSuffixPredicate predicate = new RP_PrefabSuffixPredicate();
		predicate.prefabName = "ACE_Captives_ZipCuffs.et";
		IEntity cuffs = inventory.FindItem(predicate, EStoragePurpose.PURPOSE_DEPOSIT);
		if (cuffs)
			charStorage.StoreItemToQuickSlot(cuffs, 9, true);
	}
}

// Inventory predicate that matches when the item's prefab path ends with
// the configured prefabName — needed because GetPrefabName() returns the
// full GUID-prefixed path while we search by file name.
class RP_PrefabSuffixPredicate : SCR_PrefabNamePredicate
{
	override protected bool IsMatch(BaseInventoryStorageComponent storage, IEntity item, array<GenericComponent> queriedComponents, array<BaseItemAttributeData> queriedAttributes)
	{
		bool match = super.IsMatch(storage, item, queriedComponents, queriedAttributes);
		if (!match)
		{
			EntityPrefabData pd = item.GetPrefabData();
			if (pd)
				match = pd.GetPrefabName().EndsWith(prefabName);
		}
		return match;
	}
}
