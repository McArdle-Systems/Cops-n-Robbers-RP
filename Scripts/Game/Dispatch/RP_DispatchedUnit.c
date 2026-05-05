/**
 * RP_DispatchedUnit
 *
 * Runtime state holder for one spawned dispatch unit (vehicle + crew).
 * The dispatch manager owns a list of these and ticks them.
 *
 * State machine:
 *   SPAWNED → BOARDING_FOR_DISPATCH → DRIVING_TO_TARGET → DISMOUNTING
 *          → APPROACHING_ON_FOOT → LOITERING → BOARDING_TO_RETURN
 *          → (if redispatch pending) DRIVING_TO_TARGET
 *          → (else) RETURNING → IDLE_AT_SPAWN → DESPAWN
 *
 * Available for new dispatch when state is SPAWNED, BOARDING_TO_RETURN,
 * RETURNING, or IDLE_AT_SPAWN. A redispatch arriving during
 * BOARDING_TO_RETURN sets m_bRedispatchPending so the boarding finishes
 * before the unit redirects to the new target.
 */

enum ERP_DispatchState
{
	SPAWNED,
	BOARDING_FOR_DISPATCH,
	DRIVING_TO_TARGET,
	DISMOUNTING,
	APPROACHING_ON_FOOT,
	LOITERING,
	BOARDING_TO_RETURN,
	RETURNING,
	IDLE_AT_SPAWN,
	DESPAWN
}

class RP_DispatchedUnit
{
	int m_iId;
	string m_sTypeTag;
	ref RP_DispatchGroupDefinition m_Def;
	SCR_AIGroup m_Crew;
	IEntity m_Vehicle;
	vector m_vSpawnPoint;
	vector m_vTarget;
	ERP_DispatchState m_eState;
	float m_fStateChangedAt;          // world-time of last transition
	bool m_bRedispatchPending;
	vector m_vPendingRedispatchTarget;
	AIWaypoint m_BoardingWaypoint;    // most-recent GetIn waypoint, kept so
	                                  // the force-board path can null it
	                                  // after CompleteAllWaypoints clears it
	bool m_bForceBoardingActive;      // true once the force-board sequence
	                                  // has fired for this state instance,
	                                  // so the failsafe doesn't re-trip
	                                  // every tick. Reset by EnterState.

	bool IsAvailable()
	{
		return m_eState == ERP_DispatchState.SPAWNED
			|| m_eState == ERP_DispatchState.BOARDING_TO_RETURN
			|| m_eState == ERP_DispatchState.RETURNING
			|| m_eState == ERP_DispatchState.IDLE_AT_SPAWN;
	}

	bool IsAlive()
	{
		return m_Crew && m_Vehicle;
	}

	vector GetCurrentPosition()
	{
		// Always prefer the crew leader's position. When mounted, the leader
		// is parented to the vehicle so this returns the vehicle position
		// implicitly. When dismounted, this tracks the crew on foot — which
		// the vehicle position can't.
		IEntity leader = GetCrewLeaderEntity();
		if (leader)
			return leader.GetOrigin();
		if (m_Vehicle)
			return m_Vehicle.GetOrigin();
		return vector.Zero;
	}

	IEntity GetCrewLeaderEntity()
	{
		if (!m_Crew)
			return null;
		array<AIAgent> agents = {};
		m_Crew.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;
			IEntity ent = agent.GetControlledEntity();
			if (ent)
				return ent;
		}
		return null;
	}

	// Returns true once at least one crew member is parented to the vehicle.
	bool IsAnyCrewInVehicle()
	{
		return GetCrewInVehicleCount() > 0;
	}

	// True only when every crew member is in the vehicle. Use this for the
	// "ready to drive" check — partial boarding may mean driver isn't in yet.
	bool IsAllCrewInVehicle()
	{
		if (!m_Crew || !m_Vehicle)
			return false;
		int total = 0;
		int inVehicle = 0;
		array<AIAgent> agents = {};
		m_Crew.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;
			IEntity ent = agent.GetControlledEntity();
			if (!ent)
				continue;
			total++;
			if (ent.GetParent() == m_Vehicle)
				inVehicle++;
		}
		return total > 0 && inVehicle == total;
	}

	int GetCrewInVehicleCount()
	{
		if (!m_Crew || !m_Vehicle)
			return 0;
		int count = 0;
		array<AIAgent> agents = {};
		m_Crew.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;
			IEntity ent = agent.GetControlledEntity();
			if (ent && ent.GetParent() == m_Vehicle)
				count++;
		}
		return count;
	}

	int GetCrewCount()
	{
		if (!m_Crew)
			return 0;
		array<AIAgent> agents = {};
		m_Crew.GetAgents(agents);
		return agents.Count();
	}
}
