/**
 * RP_ImpoundManagerComponent — server-authoritative impound lot.
 *
 * Manager component on the GameMode. Maintains the list of slots
 * registered by RP_ImpoundLotSlotComponent. Impound() teleports the
 * target vehicle to the first open slot; if every slot is taken, the
 * oldest impounded vehicle is deleted to make room (FIFO eviction —
 * the crusher void was on the table but plain delete won that
 * coin-flip for v1).
 *
 * The slot marker entities are kept alive — they're how we re-read
 * the slot transform on subsequent impoundments. The manager doesn't
 * physically clear a slot before teleport; if a player has parked
 * their car in the lot off-slot, the impounded vehicle may overlap
 * with it. Acceptable for v1.
 */

// One row in the manager's m_aSlots array.
class RP_ImpoundSlotState
{
	IEntity m_Marker;
	IEntity m_Vehicle;      // null when the slot is open
	int m_iImpoundedAtMs;   // engine time at impound, 0 when open — used as FIFO key for eviction
}

[ComponentEditorProps(category: "RP/Police", description: "Impound lot manager. Attach to the GameMode entity. Slots come from placed entities carrying RP_ImpoundLotSlotComponent.")]
class RP_ImpoundManagerComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_ImpoundManagerComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "5.0", desc: "A slot counts as occupied only while its impounded vehicle is within this many meters of the slot marker. Once a player drives the vehicle further than this, the slot recycles — without this, EvictOldest would happily delete the driven-away vehicle wherever it now is in the world.")]
	protected float m_fSlotOccupancyRadius;

	protected static RP_ImpoundManagerComponent s_Instance;
	protected ref array<ref RP_ImpoundSlotState> m_aSlots = {};

	static RP_ImpoundManagerComponent GetInstance() { return s_Instance; }

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
	}

	override void OnDelete(IEntity owner)
	{
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	// Called by RP_ImpoundLotSlotComponent on spawn. Runs on every peer
	// — clients need a populated list so HasAnySlots() can gate the
	// user action's CanBePerformedScript. Occupancy fields are only ever
	// written by the server (Impound is server-gated), so the client's
	// copy is marker-only and harmless.
	void RegisterSlot(IEntity marker)
	{
		if (!marker)
			return;
		// Defend against double-register if the slot component's
		// poll fires twice somehow.
		foreach (RP_ImpoundSlotState s : m_aSlots)
		{
			if (s && s.m_Marker == marker)
				return;
		}
		RP_ImpoundSlotState state = new RP_ImpoundSlotState();
		state.m_Marker = marker;
		m_aSlots.Insert(state);
		Print(string.Format("[RP_Impound] Slot registered at %1 (lot size now %2).", marker.GetOrigin(), m_aSlots.Count()), LogLevel.NORMAL);
	}

	bool HasAnySlots() { return !m_aSlots.IsEmpty(); }

	// Server-only. Teleports the vehicle into the first open slot, or
	// evicts (deletes) the oldest impounded vehicle if the lot is full.
	void Impound(IEntity vehicle, IEntity policePlayer)
	{
		if (!Replication.IsServer())
			return;
		if (!vehicle)
			return;
		if (m_aSlots.IsEmpty())
		{
			Print("[RP_Impound] Impound requested but no slots are registered — placeholders missing in world?", LogLevel.WARNING);
			return;
		}
		// First, recycle any slot whose vehicle has been driven off,
		// destroyed, or deleted out from under us. Without this,
		// EvictOldest would later delete a still-alive vehicle that's
		// no longer parked at the slot — potentially with a player in
		// it, miles away from the lot.
		ReclaimAbandonedSlots();

		// Reuse an existing entry if this vehicle is already impounded
		// (shouldn't normally happen, but stops the same car taking two
		// slots if the action is triggered twice in a row).
		foreach (RP_ImpoundSlotState s : m_aSlots)
		{
			if (s && s.m_Vehicle == vehicle)
			{
				ApplyToSlot(s, vehicle);
				return;
			}
		}

		RP_ImpoundSlotState slot = FindOpenSlot();
		if (!slot)
			slot = EvictOldest();
		if (!slot)
		{
			Print("[RP_Impound] No slot available — vehicle not impounded.", LogLevel.WARNING);
			return;
		}
		ApplyToSlot(slot, vehicle);
		Print(string.Format("[RP_Impound] Vehicle %1 impounded by %2.", vehicle, policePlayer), LogLevel.NORMAL);
	}

	protected RP_ImpoundSlotState FindOpenSlot()
	{
		// Walk in registration order so the lot fills predictably.
		foreach (RP_ImpoundSlotState s : m_aSlots)
		{
			if (!s)
				continue;
			if (!s.m_Vehicle)
				return s;
		}
		return null;
	}

	// Deletes the oldest impounded vehicle and returns its now-empty
	// slot. Returns null only when no slot has a live vehicle to evict
	// (caller falls back to ReapStaleSlots).
	protected RP_ImpoundSlotState EvictOldest()
	{
		RP_ImpoundSlotState oldest;
		foreach (RP_ImpoundSlotState s : m_aSlots)
		{
			if (!s || !s.m_Vehicle)
				continue;
			if (!oldest || s.m_iImpoundedAtMs < oldest.m_iImpoundedAtMs)
				oldest = s;
		}
		if (!oldest)
			return null;
		IEntity victim = oldest.m_Vehicle;
		oldest.m_Vehicle = null;
		oldest.m_iImpoundedAtMs = 0;
		if (victim)
		{
			Print(string.Format("[RP_Impound] Lot full — evicting oldest vehicle %1 (crushed).", victim), LogLevel.NORMAL);
			SCR_EntityHelper.DeleteEntityAndChildren(victim);
		}
		return oldest;
	}

	// Walks the slot list and recycles any slot whose recorded vehicle
	// isn't really sitting in the slot anymore. Two outcomes:
	//   - Driven away (outside m_fSlotOccupancyRadius): the slot is
	//     freed but the vehicle is left alone — a player who recovered
	//     their car and drove it off keeps it.
	//   - Wrecked in slot (DESTROYED damage state): the slot is freed
	//     AND the wreck is deleted, because the wreckage is still
	//     physically taking up the parking spot.
	// Anything currently parked in its slot is untouched.
	//
	// Called at the top of Impound() so FindOpenSlot / EvictOldest see
	// fresh state. Cheap — bounded by the number of slots.
	protected void ReclaimAbandonedSlots()
	{
		float thresholdSq = m_fSlotOccupancyRadius * m_fSlotOccupancyRadius;
		foreach (RP_ImpoundSlotState s : m_aSlots)
		{
			if (!s || !s.m_Marker)
				continue;
			if (!s.m_Vehicle)
			{
				s.m_iImpoundedAtMs = 0;
				continue;
			}
			float distSq = vector.DistanceSq(s.m_Vehicle.GetOrigin(), s.m_Marker.GetOrigin());
			bool driftedOff = distSq > thresholdSq;
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.Cast(s.m_Vehicle.FindComponent(SCR_DamageManagerComponent));
			bool wrecked = dmg && dmg.GetState() == EDamageState.DESTROYED;
			if (!driftedOff && !wrecked)
				continue;
			if (wrecked)
			{
				Print(string.Format("[RP_Impound] Slot at %1 reclaimed — deleting wreck %2.", s.m_Marker.GetOrigin(), s.m_Vehicle), LogLevel.NORMAL);
				SCR_EntityHelper.DeleteEntityAndChildren(s.m_Vehicle);
			}
			else
			{
				Print(string.Format("[RP_Impound] Slot at %1 freed — vehicle %2 was driven off; leaving it alone.", s.m_Marker.GetOrigin(), s.m_Vehicle), LogLevel.NORMAL);
			}
			s.m_Vehicle = null;
			s.m_iImpoundedAtMs = 0;
		}
	}

	// Teleports the vehicle to the slot's transform and records occupancy
	// + timestamp. Marker transform is the source of truth — re-read each
	// impoundment so a moved marker (e.g. world edit between sessions) is
	// picked up.
	//
	// Use Vehicle.Teleport(matrix) — plain IEntity.SetWorldTransform moves
	// only the entity transform; the physics rigid body keeps its old
	// position and the move doesn't replicate to clients. On dedi this
	// looks like nothing happened from the player's POV even though the
	// server log claims success.
	protected void ApplyToSlot(RP_ImpoundSlotState slot, IEntity vehicle)
	{
		if (!slot || !slot.m_Marker || !vehicle)
			return;
		vector tm[4];
		slot.m_Marker.GetWorldTransform(tm);
		Vehicle veh = Vehicle.Cast(vehicle);
		if (veh)
			veh.Teleport(tm);
		else
			vehicle.SetWorldTransform(tm);
		slot.m_Vehicle = vehicle;
		slot.m_iImpoundedAtMs = GetEngineTimeMs();
	}

	protected int GetEngineTimeMs()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return 0;
		return world.GetWorldTime();
	}
}
