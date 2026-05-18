/**
 * RP_PlateWatchlistComponent
 *
 * Server-authoritative watchlist of flagged plate strings. Lives on the
 * GameMode entity alongside RP_TrafficLoopComponent.
 *
 * Slice 1 scope: storage, lookup, partial-match query, and an auto-
 * maintained budget tied to the live traffic pool size. The radar's
 * IsPlateFlagged() is called server-side in RP_SpeedRadarLogicComponent.
 * TickServer, and the result ("this plate is flagged") rides on the per-
 * tick snapshot broadcast that drives the surveillance HUD. Clients
 * therefore do not need a local copy of the watchlist; everything stays
 * on the server.
 *
 * Auto-budget: the watchlist is kept between 1 and ceil(5% * activeCars)
 * entries. RP_TrafficLoopComponent calls MaintainBudget(activePlates)
 * whenever the pool composition changes (admin cap change, spawn, prune)
 * — that pass drops orphan plates (whose vehicles have despawned), drops
 * randomly down to the cap if we're over, and adds random unwatched
 * plates from the active pool if we're under. With 10 cars active the
 * budget is 1; at 20 it's 1; at 21+ it grows by one per +20 cars.
 *
 * Plate matching is case-sensitive. The Reforger script string primitive
 * has no ToUpper/ToLower, plates are minted by RP_TrafficLoopComponent.
 * AllocateAndRegisterPlate in a fixed "<UPPERCASE_FACTION>_Car_<N>"
 * shape, and admin input will be validated against the live registry at
 * the relay entry, so case-sensitive is adequate.
 *
 * The seed array (m_aInitialWatchPlates) is for hand-pinning specific
 * plates during testing; the auto-budget runs in addition to it.
 */

[ComponentEditorProps(category: "RP/Surveillance", description: "Server-authoritative plate watchlist. Attach to the GameMode entity. Radar IsPlateFlagged() consults this; HUD paints matching plates red via the existing snapshot.")]
class RP_PlateWatchlistComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_PlateWatchlistComponent : SCR_BaseGameModeComponent
{
	[Attribute(desc: "Plates seeded into the watchlist at server start. For first-light / testing; admin add/remove arrives in Slice 2.")]
	protected ref array<string> m_aInitialWatchPlates;

	[Attribute(defvalue: "0.05", desc: "Fraction of active traffic to keep on the watchlist. Default 0.05 (5%). MaintainBudget computes ceil(percent * activeCount) and clamps to at least m_iMinBudget when activeCount > 0.")]
	protected float m_fBudgetPercent;

	[Attribute(defvalue: "1", desc: "Minimum watchlist size while at least one traffic vehicle is active. Floors the computed percent-of-pool, so small pools still get one watched plate. Set 0 to disable the floor (then percent fully governs).")]
	protected int m_iMinBudget;

	protected static RP_PlateWatchlistComponent s_Instance;

	// Server-only runtime set. Matching is case-sensitive — the Reforger
	// script string primitive has no ToUpper/ToLower (those live on
	// SCR_StringArray only), so a case-insensitive impl would need a
	// hand-rolled char-by-char helper. Plates are minted by
	// RP_TrafficLoopComponent.AllocateAndRegisterPlate in a fixed format
	// ("<UPPERCASE_FACTION>_Car_<N>") and admin input will be validated
	// against the live registry at the relay entry, so case-sensitive is
	// adequate. set<string> would be ideal but the script API ships
	// map<,> as the indexed container; we use a string->bool map as a
	// stand-in (value is always true).
	protected ref map<string, bool> m_mWatched = new map<string, bool>();

	static RP_PlateWatchlistComponent GetInstance() { return s_Instance; }

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
		if (!GetGame().InPlayMode())
			return;
		if (!Replication.IsServer())
			return;
		SeedFromInitialList();
	}

	override void OnDelete(IEntity owner)
	{
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	protected void SeedFromInitialList()
	{
		if (!m_aInitialWatchPlates)
			return;
		int added = 0;
		foreach (string plate : m_aInitialWatchPlates)
		{
			if (AddWatch(plate))
				added++;
		}
		if (added > 0)
			Print(string.Format("[RP_Watchlist] Seeded %1 plate(s) from m_aInitialWatchPlates.", added), LogLevel.NORMAL);
	}

	// ----------------------------------------------------------------------
	// Public API
	// ----------------------------------------------------------------------

	// Returns true if the (non-empty) plate is on the watchlist. Safe to
	// call from any peer, but only the server holds authoritative data —
	// clients will always see false. The radar consults this server-side
	// in TickServer, which is the intended call site.
	bool IsWatched(string plate)
	{
		if (plate.IsEmpty())
			return false;
		return m_mWatched.Contains(plate);
	}

	// Server-only. Adds a plate to the watchlist. Returns true if the
	// plate was newly added, false if it was already present or invalid.
	bool AddWatch(string plate)
	{
		if (!Replication.IsServer())
			return false;
		if (plate.IsEmpty())
			return false;
		if (m_mWatched.Contains(plate))
			return false;
		m_mWatched.Set(plate, true);
		Print(string.Format("[RP_Watchlist] Added '%1' (count=%2)", plate, m_mWatched.Count()), LogLevel.NORMAL);
		return true;
	}

	// Server-only. Removes a plate from the watchlist. Returns true if
	// the plate was found and removed.
	bool RemoveWatch(string plate)
	{
		if (!Replication.IsServer())
			return false;
		if (plate.IsEmpty())
			return false;
		if (!m_mWatched.Contains(plate))
			return false;
		m_mWatched.Remove(plate);
		Print(string.Format("[RP_Watchlist] Removed '%1' (count=%2)", plate, m_mWatched.Count()), LogLevel.NORMAL);
		return true;
	}

	// Server-only. Copies the current watchlist into out (sorted not
	// guaranteed). For admin-list / diagnostics.
	void GetAllWatched(out array<string> outPlates)
	{
		if (!outPlates)
			return;
		outPlates.Clear();
		foreach (string key, bool present : m_mWatched)
		{
			outPlates.Insert(key);
		}
	}

	int GetWatchedCount() { return m_mWatched.Count(); }

	// Returns every watched plate that contains `fragment` as a substring.
	// Empty fragment returns the full list (same as GetAllWatched). Used
	// by Phase 3's manual-lookup interface; the radar's per-tick hit path
	// uses the exact-match IsWatched instead.
	void MatchPartial(string fragment, out array<string> outMatches)
	{
		if (!outMatches)
			return;
		outMatches.Clear();
		foreach (string key, bool present : m_mWatched)
		{
			if (fragment.IsEmpty() || key.Contains(fragment))
				outMatches.Insert(key);
		}
	}

	// ----------------------------------------------------------------------
	// Auto-budget — keeps watchlist size proportional to live pool
	// ----------------------------------------------------------------------

	// Server-only. Called by RP_TrafficLoopComponent on pool changes.
	// Drops orphan plates (watched but no longer in the active pool),
	// then rebalances size to be in [1, ceil(5% * activeCount)].
	//
	// Notes:
	//   - At activeCount == 0, the cap is 0 (no vehicles to watch).
	//   - Plates added by SeedFromInitialList that happen to match live
	//     vehicles persist normally; orphan seed plates get culled at the
	//     first MaintainBudget call after the pool stabilizes. This is
	//     fine — the seed is just a hand-pinning convenience.
	void MaintainBudget(notnull array<string> activePlates)
	{
		if (!Replication.IsServer())
			return;

		int activeCount = activePlates.Count();
		int budget;
		if (activeCount <= 0)
			budget = 0;
		else
			budget = ComputeBudget(activeCount);

		// Step 1 — drop orphans (plates we watch but that aren't in the
		// live pool). Build the active-set keys for O(1) lookup.
		ref map<string, bool> activeSet = new map<string, bool>();
		foreach (string plate : activePlates)
		{
			if (!plate.IsEmpty())
				activeSet.Set(plate, true);
		}
		array<string> toDropOrphan = {};
		foreach (string key, bool present : m_mWatched)
		{
			if (!activeSet.Contains(key))
				toDropOrphan.Insert(key);
		}
		foreach (string key : toDropOrphan)
		{
			m_mWatched.Remove(key);
		}

		// Step 2 — cull randomly down to budget if we're over.
		while (m_mWatched.Count() > budget)
		{
			array<string> keys = {};
			foreach (string key, bool present : m_mWatched)
				keys.Insert(key);
			if (keys.IsEmpty())
				break;
			int pick = SCR_Math.RandomInt(0, keys.Count());
			m_mWatched.Remove(keys[pick]);
		}

		// Step 3 — top up randomly from unwatched active plates if under.
		if (m_mWatched.Count() < budget)
		{
			array<string> candidates = {};
			foreach (string plate : activePlates)
			{
				if (!plate.IsEmpty() && !m_mWatched.Contains(plate))
					candidates.Insert(plate);
			}
			while (m_mWatched.Count() < budget && !candidates.IsEmpty())
			{
				int pick = SCR_Math.RandomInt(0, candidates.Count());
				m_mWatched.Set(candidates[pick], true);
				candidates.Remove(pick);
			}
		}

		Print(string.Format("[RP_Watchlist] MaintainBudget: activeCount=%1 budget=%2 orphansDropped=%3 finalCount=%4",
			activeCount, budget, toDropOrphan.Count(), m_mWatched.Count()), LogLevel.NORMAL);
	}

	// ceil(activeCount * m_fBudgetPercent), floored at m_iMinBudget when
	// activeCount > 0. Defensive: a negative or zero percent collapses to
	// the floor; a negative floor is treated as 0.
	protected int ComputeBudget(int activeCount)
	{
		float pct = m_fBudgetPercent;
		if (pct < 0)
			pct = 0;
		int floorVal = m_iMinBudget;
		if (floorVal < 0)
			floorVal = 0;

		float raw = activeCount * pct;
		int b = raw;
		if (raw > b)
			b = b + 1;
		if (b < floorVal)
			b = floorVal;
		return b;
	}
}
