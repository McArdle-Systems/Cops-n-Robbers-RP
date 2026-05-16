/**
 * RP_RTTestComponent
 *
 * Minimum-viable RTTextureWidget self-test. Place a prefab carrying
 * this component anywhere in the world and it spawns a tiny widget
 * tree on game start, binds the RTTextureWidget to itself, and ticks
 * an incrementing counter into a TextWidget.
 *
 * Purpose: prove (or disprove) that the RT-to-mesh pipeline works at
 * all, without any of MFD framework / HUD / SlotManager / vehicle
 * context. The host entity's mesh material must reference the bound
 * render target via $rendertarget on a texture-map field (BCRMap on
 * MatPBRBasic works once ApplyAlbedoToEmissive is on and the emissive
 * tint isn't black).
 *
 * Expected behavior: a counter ("RT TEST 0", "RT TEST 1", ...) appears
 * on the host mesh's surface once per second.
 */

[ComponentEditorProps(category: "RP/Surveillance", description: "Standalone render-target self-test. Spawns a layout, binds its RTTextureWidget to this entity, and ticks a counter into the bound text widget. Attach to a prefab with a $rendertarget-aware material on its mesh.")]
class RP_RTTestComponentClass : ScriptComponentClass
{
}

class RP_RTTestComponent : ScriptComponent
{
	[Attribute(defvalue: "{8C0F3E120000A500}UI/RP_RTTest.layout", desc: "Layout containing an RTTextureWidget root with an inner TextWidget named 'RTText'.", UIWidgets.ResourcePickerThumbnail, params: "layout")]
	protected ResourceName m_sLayoutPath;

	protected Widget m_wRoot;
	protected RTTextureWidget m_wRT;
	protected TextWidget m_wText;
	protected int m_iCounter;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		Print(string.Format("[RP_RTTest] OnPostInit on %1 at %2", owner, owner.GetOrigin()), LogLevel.NORMAL);
		if (!GetGame().InPlayMode())
		{
			Print("[RP_RTTest] Not in play mode, skipping init.", LogLevel.NORMAL);
			return;
		}
		// Defer init by 250ms to ensure workspace + entity hierarchy
		// are settled by the time we bind.
		GetGame().GetCallqueue().CallLater(InitRT, 250, false);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(InitRT);
		GetGame().GetCallqueue().Remove(Tick);
		// Skip RemoveRenderTarget — by the time OnDelete fires, the
		// entity behind 'owner' is already destroyed and any call
		// referencing it throws "Entity is already deleted". The
		// engine cleans up the binding for us when the entity dies.
		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();
		m_wRT = null;
		m_wRoot = null;
		m_wText = null;
		super.OnDelete(owner);
	}

	protected void InitRT()
	{
		Print("[RP_RTTest] InitRT firing.", LogLevel.NORMAL);
		if (m_sLayoutPath.IsEmpty())
		{
			Print("[RP_RTTest] No layout configured (m_sLayoutPath empty).", LogLevel.ERROR);
			return;
		}
		Print(string.Format("[RP_RTTest] Layout path: %1", m_sLayoutPath), LogLevel.NORMAL);

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
		{
			Print("[RP_RTTest] No workspace available.", LogLevel.ERROR);
			return;
		}
		Print("[RP_RTTest] Workspace OK.", LogLevel.NORMAL);

		m_wRoot = ws.CreateWidgets(m_sLayoutPath);
		if (!m_wRoot)
		{
			Print(string.Format("[RP_RTTest] CreateWidgets returned null for %1", m_sLayoutPath), LogLevel.ERROR);
			return;
		}
		Print(string.Format("[RP_RTTest] Layout root widget: %1", m_wRoot), LogLevel.NORMAL);

		Widget rt = m_wRoot.FindAnyWidget("RTSurface");
		if (!rt)
		{
			Print("[RP_RTTest] FindAnyWidget('RTSurface') returned null. Walking children:", LogLevel.ERROR);
			DumpWidgetTree(m_wRoot, 0);
			return;
		}
		m_wRT = RTTextureWidget.Cast(rt);
		if (!m_wRT)
		{
			Print(string.Format("[RP_RTTest] Found 'RTSurface' but it's not an RTTextureWidget. Type: %1", rt.GetTypeName()), LogLevel.ERROR);
			return;
		}
		Print("[RP_RTTest] Found RTTextureWidget 'RTSurface'.", LogLevel.NORMAL);

		Widget t = m_wRoot.FindAnyWidget("RTText");
		m_wText = TextWidget.Cast(t);
		if (!m_wText)
			Print("[RP_RTTest] 'RTText' widget not found — counter won't update.", LogLevel.WARNING);
		else
			Print("[RP_RTTest] Found TextWidget 'RTText'.", LogLevel.NORMAL);

		IEntity owner = GetOwner();
		if (!owner)
		{
			Print("[RP_RTTest] GetOwner() returned null — can't bind RT.", LogLevel.ERROR);
			return;
		}
		Print(string.Format("[RP_RTTest] Calling SetRenderTarget on owner: %1 at %2", owner, owner.GetOrigin()), LogLevel.NORMAL);
		m_wRT.SetRenderTarget(owner);
		Print("[RP_RTTest] SetRenderTarget completed. Starting tick.", LogLevel.NORMAL);

		GetGame().GetCallqueue().CallLater(Tick, 1000, true);
		Tick();
	}

	// Recursive widget tree dump for debug — walks via GetChildren / GetSibling
	// and logs each widget's name and type with indentation.
	protected void DumpWidgetTree(Widget w, int depth)
	{
		if (!w)
			return;
		string indent;
		for (int i = 0; i < depth; i++)
			indent = indent + "  ";
		Print(string.Format("%1- %2 (%3)", indent, w.GetName(), w.GetTypeName()), LogLevel.NORMAL);
		Widget c = w.GetChildren();
		while (c)
		{
			DumpWidgetTree(c, depth + 1);
			c = c.GetSibling();
		}
	}

	protected void Tick()
	{
		m_iCounter++;
		if (m_wText)
			m_wText.SetText(string.Format("RT TEST %1", m_iCounter));
		// Force the widget tree to re-render so the RT picks up the
		// new text on this frame.
		if (m_wRoot)
			m_wRoot.Update();
		if (m_wRT)
			m_wRT.Update();
	}
}
