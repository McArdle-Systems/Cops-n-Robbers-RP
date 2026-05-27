/**
 * RP_ImpoundToolComponent
 *
 * Marker component attached to the "Impound Sheet" inventory item.
 * RP_ImpoundVehicleUserAction queries the player's held gadget for
 * this component to decide whether to show the action prompt. The
 * component itself carries no behavior — its presence is the signal.
 *
 * Why a marker component instead of comparing prefab ResourceName:
 *   - Survives prefab forks: subclassed impound-sheet variants
 *     (different art, faction-flavored copies) all light up the same
 *     action without script edits.
 *   - Lets `IEntity.FindComponent` resolve in O(1) instead of
 *     reading the prefab path off the entity.
 */

[ComponentEditorProps(category: "RP/Police", description: "Marks an inventory item as the 'Impound Sheet'. Hold this item in hand to surface the Impound Vehicle action on any vehicle that carries RP_ImpoundVehicleUserAction.")]
class RP_ImpoundToolComponentClass : ScriptComponentClass
{
}

class RP_ImpoundToolComponent : ScriptComponent
{
}
