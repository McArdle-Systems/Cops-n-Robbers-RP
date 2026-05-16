/**
 * RP_PlayerInventorySetupComponent
 *
 * Generic per-character inventory setup that runs on the owning client
 * once the controlled character + replicated inventory are both ready.
 * Attach to any player character prefab and configure its m_aQuickSlotPins
 * to pin specific loadout items to specific quick-slot indices on spawn.
 *
 * Why client-side: SCR's quickslot array has a client-side init pass
 * (m_aQuickSlotsInitalized flag, GetDefaultQuickSlot lookup) that
 * overwrites server-set entries the moment the local client first
 * touches the slot. On a listen-server (workbench) the same process is
 * both, so a server-only write sticks — but on dedicated the client
 * pass clobbers it. Items targeting a nested storage (e.g. cuffs into
 * Vest/Equip.et) are also not guaranteed to be deposited by the moment
 * OnPlayerSpawned fires server-side.
 *
 * The 500ms poll waits for two preconditions per tick:
 *   1. PlayerController.GetControlledEntity() == this character (filters
 *      out dedi-server instances and other clients' remote-proxy copies)
 *   2. each configured item is findable in the inventory (filters out
 *      the loadout-not-deposited-yet race)
 *
 * Pins not satisfied within ~10s are dropped — either the item never
 * spawned (misconfigured pin) or this isn't the owning client.
 */

[BaseContainerProps()]
class RP_QuickSlotPin
{
	[Attribute(desc: "Prefab file name (suffix match, e.g. \"ACE_Captives_ZipCuffs.et\") of the item to pin.")]
	string m_sItemPrefabSuffix;

	[Attribute(defvalue: "-1", desc: "Quick slot index (0-based) to pin the item into.")]
	int m_iQuickSlotIndex;
}

[ComponentEditorProps(category: "RP/Character", description: "Pins configured inventory items to specific quick slots once the owning client and the items are both ready. Attach to player character prefabs.")]
class RP_PlayerInventorySetupComponentClass : ScriptComponentClass
{
}

class RP_PlayerInventorySetupComponent : ScriptComponent
{
	[Attribute(desc: "Items to pin to specific quick slots on spawn.")]
	protected ref array<ref RP_QuickSlotPin> m_aQuickSlotPins;

	protected int m_iAttempts;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		if (!m_aQuickSlotPins || m_aQuickSlotPins.IsEmpty())
			return;
		GetGame().GetCallqueue().CallLater(TryPinAll, 500, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryPinAll);
		super.OnDelete(owner);
	}

	protected void TryPinAll()
	{
		m_iAttempts++;
		if (m_iAttempts > 20)
		{
			// ~10s elapsed. Either we're a dedi-server instance, a
			// remote-proxy on another client, or a configured item never
			// appeared. Stop polling either way.
			GetGame().GetCallqueue().Remove(TryPinAll);
			return;
		}

		IEntity owner = GetOwner();
		if (!owner)
			return;

		// Owning-client gate: skip dedi-server (no PC) and remote-proxy
		// instances (PC controls a different character).
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc || pc.GetControlledEntity() != owner)
			return;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(owner.FindComponent(SCR_InventoryStorageManagerComponent));
		SCR_CharacterInventoryStorageComponent charStorage = SCR_CharacterInventoryStorageComponent.Cast(owner.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!inventory || !charStorage)
		{
			GetGame().GetCallqueue().Remove(TryPinAll);
			return;
		}

		bool allPinned = true;
		foreach (RP_QuickSlotPin pin : m_aQuickSlotPins)
		{
			if (!pin || pin.m_sItemPrefabSuffix.IsEmpty() || pin.m_iQuickSlotIndex < 0)
				continue;

			RP_PrefabSuffixPredicate predicate = new RP_PrefabSuffixPredicate();
			predicate.prefabName = pin.m_sItemPrefabSuffix;
			IEntity item = inventory.FindItem(predicate, EStoragePurpose.PURPOSE_DEPOSIT);
			if (!item)
			{
				allPinned = false;
				continue; // keep polling — loadout may still be depositing
			}

			// Skip if already in the configured slot (avoids redundant
			// writes on listen-server where server + client are the same
			// instance and the slot may already be set).
			if (charStorage.GetItemFromQuickSlot(pin.m_iQuickSlotIndex) == item)
				continue;

			charStorage.StoreItemToQuickSlot(item, pin.m_iQuickSlotIndex, true);
		}

		if (allPinned)
			GetGame().GetCallqueue().Remove(TryPinAll);
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
