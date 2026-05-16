class RP_AdminPanelUI : ChimeraMenuBase
{
	// Hard upper bound for the slider — protects against fat-fingered
	// `+` spamming all the way to a crash.
	protected const int MAX_TRAFFIC_CAP = 50;

	protected static int s_iMaxActiveVehicles = 6;
	protected static RP_AdminPanelUI s_OpenInstance;

	protected TextWidget m_wValueText;
	protected TextWidget m_wSubtitle;
	protected SCR_ButtonTextComponent m_MinusBtn;
	protected SCR_ButtonTextComponent m_PlusBtn;

	protected bool m_bLoading;

	override void OnMenuOpen()
	{
		super.OnMenuOpen();
		s_OpenInstance = this;

		Widget root = GetRootWidget();
		if (!root)
			return;

		TextWidget title = TextWidget.Cast(root.FindAnyWidget("TitleLine"));
		if (title)
		{
			title.SetExactFontSize(28);
			title.SetColor(Color.FromInt(Color.WHITE));
			title.SetText("ADMIN CONTROLS");
		}

		m_wSubtitle = TextWidget.Cast(root.FindAnyWidget("SubtitleLine"));
		if (m_wSubtitle)
		{
			m_wSubtitle.SetExactFontSize(16);
			m_wSubtitle.SetColor(new Color(0.7, 0.7, 0.7, 1));
			m_wSubtitle.SetText("Max active civilian vehicles");
		}

		m_wValueText = TextWidget.Cast(root.FindAnyWidget("ValueText"));
		if (m_wValueText)
		{
			m_wValueText.SetExactFontSize(32);
			m_wValueText.SetColor(Color.FromInt(Color.WHITE));
		}

		Widget minusW = root.FindAnyWidget("MinusButton");
		if (minusW)
		{
			m_MinusBtn = SCR_ButtonTextComponent.Cast(minusW.FindHandler(SCR_ButtonTextComponent));
			if (m_MinusBtn)
				m_MinusBtn.m_OnClicked.Insert(OnMinusClicked);
		}

		Widget plusW = root.FindAnyWidget("PlusButton");
		if (plusW)
		{
			m_PlusBtn = SCR_ButtonTextComponent.Cast(plusW.FindHandler(SCR_ButtonTextComponent));
			if (m_PlusBtn)
				m_PlusBtn.m_OnClicked.Insert(OnPlusClicked);
		}

		Widget closeW = root.FindAnyWidget("CloseButton");
		if (closeW)
		{
			SCR_ButtonTextComponent closeBtn = SCR_ButtonTextComponent.Cast(closeW.FindHandler(SCR_ButtonTextComponent));
			if (closeBtn)
				closeBtn.m_OnClicked.Insert(OnCloseClicked);
		}

		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.AddActionListener("MenuBack",  EActionTrigger.DOWN, OnCloseClicked);
			input.AddActionListener("PauseMenu", EActionTrigger.DOWN, OnCloseClicked);
		}

		// Enter loading state until the server replies with the live cap.
		// On host, RequestTrafficCap synchronously calls ReceiveTrafficCap
		// which clears loading before this OnMenuOpen returns — so the
		// loading UI is only ever visible to remote clients during the
		// round-trip.
		SetLoadingState(true);

		RP_PlayerRpcRelayComponent relay = RP_PlayerRpcRelayComponent.GetLocal();
		if (relay)
		{
			relay.RequestTrafficCap();
		}
		else if (Replication.IsServer())
		{
			// Host without a relay-equipped character — read directly.
			RP_TrafficLoopComponent loop = RP_TrafficLoopComponent.GetInstance();
			if (loop)
				ReceiveTrafficCap(loop.GetTargetActiveCount());
		}
		else
		{
			// Remote client with no relay: we can't fetch and we can't
			// edit. Surface that in the subtitle and stay disabled.
			if (m_wSubtitle)
				m_wSubtitle.SetText("Unavailable — no relay on your character");
		}
	}

	override void OnMenuClose()
	{
		if (s_OpenInstance == this)
			s_OpenInstance = null;
		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.RemoveActionListener("MenuBack",  EActionTrigger.DOWN, OnCloseClicked);
			input.RemoveActionListener("PauseMenu", EActionTrigger.DOWN, OnCloseClicked);
		}
		super.OnMenuClose();
	}

	// Called by the relay when the server replies with the live cap.
	static void ReceiveTrafficCap(int currentCap)
	{
		s_iMaxActiveVehicles = currentCap;
		if (s_OpenInstance)
			s_OpenInstance.SetLoadingState(false);
	}

	protected void SetLoadingState(bool loading)
	{
		m_bLoading = loading;

		if (m_MinusBtn)
			m_MinusBtn.SetEnabled(!loading, false);
		if (m_PlusBtn)
			m_PlusBtn.SetEnabled(!loading, false);

		if (m_wValueText)
		{
			if (loading)
				m_wValueText.SetText("...");
			else
				m_wValueText.SetText(s_iMaxActiveVehicles.ToString());
		}
	}

	protected void UpdateValueDisplay()
	{
		if (m_wValueText && !m_bLoading)
			m_wValueText.SetText(s_iMaxActiveVehicles.ToString());
	}

	void OnMinusClicked()
	{
		if (m_bLoading)
			return;
		if (s_iMaxActiveVehicles > 0)
			s_iMaxActiveVehicles--;
		UpdateValueDisplay();
		SubmitNewCap();
	}

	void OnPlusClicked()
	{
		if (m_bLoading)
			return;
		if (s_iMaxActiveVehicles < MAX_TRAFFIC_CAP)
			s_iMaxActiveVehicles++;
		UpdateValueDisplay();
		SubmitNewCap();
	}

	protected void SubmitNewCap()
	{
		RP_PlayerRpcRelayComponent relay = RP_PlayerRpcRelayComponent.GetLocal();
		if (!relay)
		{
			Print("[RP_Admin] No relay on local player — cap change not sent. (Are you in a character with the relay component attached?)", LogLevel.WARNING);
			return;
		}
		relay.RequestSetMaxTraffic(s_iMaxActiveVehicles);
	}

	void OnCloseClicked()
	{
		Close();
	}
}
