/**
 * RP_SurveillanceHUDComponent
 *
 * Toggleable Active Surveillance overlay (LPR + radar). Top-left,
 * non-modal — does not lock the cursor or block input. Two fields
 * (SPEED, PLATE), each with a status dot. Dot is green when the
 * scanned vehicle's reading is OK (within speed limit / plate not
 * flagged) and red otherwise. When no target is in the cone, both
 * fields show "—" and the dots stay green.
 *
 * Gating: only available while seated in a vehicle that carries
 * RP_PoliceVehicleComponent. Pressing the toggle outside a police
 * vehicle is a no-op; if the player exits a police vehicle while the
 * HUD is up, the HUD auto-hides and tears down.
 *
 * Detection: forward cone from the cop car's transform. Vehicles
 * within m_fConeRangeMeters and m_fConeHalfAngleDeg of forward are
 * candidates; the closest is the active target.
 *
 * POC stubs: GetVehiclePlate uses the entity's workbench name as a
 * stand-in plate; IsPlateFlagged always returns false. Wire these up
 * once a real plate registry lands.
 *
 * Toggled by a Bohemia input action (default RP_ToggleSurveillance,
 * KC_RBRACKET — "]"). Action defined in chimeraInputCommon.conf and
 * surfaced under the CNR keybinding category.
 */

[ComponentEditorProps(category: "RP/Surveillance", description: "Tag — marks a vehicle as a police cruiser. Surveillance HUD only activates while seated in a tagged vehicle.")]
class RP_PoliceVehicleComponentClass : ScriptComponentClass
{
}

class RP_PoliceVehicleComponent : ScriptComponent
{
}

[ComponentEditorProps(category: "RP/Surveillance", description: "Toggleable surveillance HUD overlay. Attach to the GameMode entity.")]
class RP_SurveillanceHUDComponentClass : SCR_BaseGameModeComponentClass
{
}

class RP_SurveillanceHUDComponent : SCR_BaseGameModeComponent
{
	[Attribute(defvalue: "RP_ToggleSurveillance", desc: "Input action that toggles the HUD.")]
	protected string m_sToggleAction;

	[Attribute(defvalue: "{AB95DA9834D81521}UI/RP_SurveillanceHUD.layout", desc: "Layout for the HUD overlay.", UIWidgets.ResourcePickerThumbnail, params: "layout")]
	protected ResourceName m_sLayoutPath;

	[Attribute(defvalue: "22.5", desc: "Cone half-angle in degrees from the cop car's forward direction. Total cone width is 2x this.")]
	protected float m_fConeHalfAngleDeg;

	[Attribute(defvalue: "50.0", desc: "Cone range in meters.")]
	protected float m_fConeRangeMeters;

	[Attribute(defvalue: "0.25", desc: "Scan interval in seconds. Drives both the cone scan and the in-cop-vehicle gate check.")]
	protected float m_fScanIntervalSeconds;

	[Attribute(defvalue: "50.0", desc: "Speed limit (km/h). At or below = green dot, above = red.")]
	protected float m_fSpeedLimitKmh;

	protected Widget m_wRoot;
	protected bool m_bVisible;
	protected bool m_bActionRegistered;
	protected ref array<IEntity> m_aQueryResults = {};

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame().InPlayMode())
			return;
		GetGame().GetCallqueue().CallLater(TryClientInit, 250, true);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(TryClientInit);
		GetGame().GetCallqueue().Remove(Tick);
		UnregisterAction();
		DestroyWidget();
		super.OnDelete(owner);
	}

	protected void TryClientInit()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;
		GetGame().GetCallqueue().Remove(TryClientInit);
		RegisterAction();
	}

	protected void RegisterAction()
	{
		if (m_bActionRegistered)
			return;
		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;
		input.AddActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleAction);
		m_bActionRegistered = true;
		Print(string.Format("[RP_Surveillance] HUD toggle bound to action '%1'", m_sToggleAction), LogLevel.NORMAL);
	}

	protected void UnregisterAction()
	{
		if (!m_bActionRegistered)
			return;
		InputManager input = GetGame().GetInputManager();
		if (input)
			input.RemoveActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleAction);
		m_bActionRegistered = false;
	}

	protected void OnToggleAction()
	{
		if (m_bVisible)
		{
			Hide();
			DestroyWidget();
			return;
		}
		if (!GetPlayerCopVehicle())
		{
			Print("[RP_Surveillance] Toggle ignored — not seated in a police vehicle.", LogLevel.NORMAL);
			return;
		}
		Show();
	}

	protected void Show()
	{
		if (!m_wRoot)
		{
			WorkspaceWidget ws = GetGame().GetWorkspace();
			if (!ws)
				return;
			m_wRoot = ws.CreateWidgets(m_sLayoutPath);
			if (!m_wRoot)
			{
				Print(string.Format("[RP_Surveillance] Layout failed to load: %1", m_sLayoutPath), LogLevel.ERROR);
				return;
			}
			SetText("SpeedLabel", "SPEED");
			SetText("PlateLabel", "PLATE");
			SetText("SpeedDot", "■");
			SetText("PlateDot", "■");
			// Explicit initial color so the dots are visibly green even
			// before the first Tick() resolves a target.
			SetDotColor("SpeedDot", true);
			SetDotColor("PlateDot", true);
		}
		m_wRoot.SetVisible(true);
		m_bVisible = true;
		GetGame().GetCallqueue().CallLater(Tick, (int)(m_fScanIntervalSeconds * 1000), true);
		Tick();
	}

	protected void Hide()
	{
		if (m_wRoot)
			m_wRoot.SetVisible(false);
		GetGame().GetCallqueue().Remove(Tick);
		m_bVisible = false;
	}

	protected void DestroyWidget()
	{
		if (!m_wRoot)
			return;
		m_wRoot.RemoveFromHierarchy();
		m_wRoot = null;
	}

	protected void Tick()
	{
		IEntity copCar = GetPlayerCopVehicle();
		if (!copCar)
		{
			// Player exited the cop vehicle — auto hide & remove.
			Hide();
			DestroyWidget();
			return;
		}

		IEntity target = FindVehicleInCone(copCar);
		if (!target)
		{
			SetText("SpeedValue", "—");
			SetText("PlateValue", "—");
			SetDotColor("SpeedDot", true);
			SetDotColor("PlateDot", true);
			return;
		}

		float speedKmh = GetVehicleSpeedKmh(target);
		string plate = GetVehiclePlate(target);
		bool overLimit = IsSpeedOverLimit(speedKmh);
		bool plateFlag = IsPlateFlagged(plate);

		SetText("SpeedValue", string.Format("%1 km/h", Math.Round(speedKmh)));
		SetText("PlateValue", plate);
		SetDotColor("SpeedDot", !overLimit);
		SetDotColor("PlateDot", !plateFlag);
	}

	// Walks up the parent chain from the player's controlled entity until
	// it hits a Vehicle. If that vehicle has RP_PoliceVehicleComponent,
	// returns it; otherwise returns null. Returns null if the player is on
	// foot or in a non-police vehicle.
	protected IEntity GetPlayerCopVehicle()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;
		IEntity player = pc.GetControlledEntity();
		if (!player)
			return null;
		IEntity p = player.GetParent();
		while (p)
		{
			if (Vehicle.Cast(p))
			{
				if (p.FindComponent(RP_PoliceVehicleComponent))
					return p;
				return null;
			}
			p = p.GetParent();
		}
		return null;
	}

	protected IEntity FindVehicleInCone(IEntity copCar)
	{
		vector tm[4];
		copCar.GetTransform(tm);
		vector originPos = tm[3];
		vector originForward = tm[2];

		m_aQueryResults.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(
			originPos, m_fConeRangeMeters,
			QueryCollect, null,
			EQueryEntitiesFlags.DYNAMIC);

		float halfAngleCos = Math.Cos(m_fConeHalfAngleDeg * Math.DEG2RAD);
		IEntity best;
		float bestDist = float.MAX;

		foreach (IEntity ent : m_aQueryResults)
		{
			if (ent == copCar)
				continue;
			if (!Vehicle.Cast(ent))
				continue;

			vector toEnt = ent.GetOrigin() - originPos;
			float dist = toEnt.Length();
			if (dist < 0.5 || dist > m_fConeRangeMeters)
				continue;

			vector dir = toEnt * (1.0 / dist);
			float dotVal = vector.Dot(dir, originForward);
			if (dotVal < halfAngleCos)
				continue;

			if (dist < bestDist)
			{
				bestDist = dist;
				best = ent;
			}
		}
		return best;
	}

	protected bool QueryCollect(IEntity entity)
	{
		m_aQueryResults.Insert(entity);
		return true;
	}

	protected float GetVehicleSpeedKmh(IEntity vehicle)
	{
		Physics phys = vehicle.GetPhysics();
		if (!phys)
			return 0;
		return phys.GetVelocity().Length() * 3.6;
	}

	// POC stub: stand-in plate from the entity's workbench name.
	// Replace with a real plate registry once we add one.
	protected string GetVehiclePlate(IEntity vehicle)
	{
		string name = vehicle.GetName();
		if (name.IsEmpty())
			return "—";
		return name;
	}

	protected bool IsSpeedOverLimit(float speedKmh)
	{
		return speedKmh > m_fSpeedLimitKmh;
	}

	// POC stub: hardcoded watchlist (currently empty).
	// Wire this up to a real registry / RPC when ready.
	protected bool IsPlateFlagged(string plate)
	{
		return false;
	}

	protected void SetText(string widgetName, string text)
	{
		if (!m_wRoot)
			return;
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (!w)
			return;
		TextWidget tw = TextWidget.Cast(w);
		if (tw)
			tw.SetText(text);
	}

	// 0xAARRGGBB packed. Green = OK, red = flagged.
	protected void SetDotColor(string widgetName, bool isOk)
	{
		if (!m_wRoot)
			return;
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (!w)
			return;
		TextWidget tw = TextWidget.Cast(w);
		if (!tw)
			return;
		if (isOk)
			tw.SetColorInt(0xFF00FF00);
		else
			tw.SetColorInt(0xFFFF0000);
	}
}
