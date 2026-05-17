# Chat HUD Investigation — Handoff

**Issue:** GitHub issue [#10](https://github.com/McArdle-Systems/Cops-n-Robbers-RP/issues/10) — chat input popup works but history panel never renders.

**Status:** Root cause identified 2026-05-15 — world was missing the `ScriptedChatEntity` (the backend that hosts `BaseChatComponent` + channels). Stock `DefaultPlayerControllerMP.et` already wires the chat `SCR_ChatHud` InfoDisplay (EveronLife's PC explicitly *clears* it, proving it exists in the parent). Without `ScriptedChatEntity` placed in the world, channels never register and the HUD has nothing to render. The input popup is from the chat-open input action, which is independent of the chat backend — that's why it appeared but the history did not.

**Fix applied:** Added `ScriptedChatEntity` at `Prefabs/MP/ScriptedChatEntity.et` to `Worlds/TestWorld_Layers/Managers.layer` (next to RadioManager). Reference: CNR_2025's `Worlds/CNR_2025_Arland_Layers/Logic.layer:99`.

**Next:** Test in workbench (open chat, send message, confirm history panel appears). If history shows, close issue #10. The investigation notes below are the old (incorrect) hypothesis that the HUD InfoDisplay was missing — kept as a record of what was ruled out.

## Current state

- World wires the PlayerController prefab in `Worlds/TestWorld_Layers/Managers.layer:97` to `Prefabs/Characters/Core/DefaultPlayerControllerMP_Factions.et` — our local override (added in commit `75f0f90`, "Add PC _Factions variant + workshop thumbnail").
- That override is **thin**: it only adds `SCR_SpawnerRequestComponent`, inheriting everything else from stock `DefaultPlayerControllerMP.et`. **It does not add any chat HUD entry.**
- **User confirmed in this session: the `_Factions` swap did not fix chat.** So the parent prefab also lacks a chat history HUD entry (or has it gated somehow).
- GameMode inheritance (at time of investigation): `RP_GameMode_Deathmatch_Automatic.et` → stock `GameMode_Deathmatch_Base.et`. The issue notes that **stock Deathmatch HUD is intentionally minimal (kill feed, timer, scoreboard) — Conflict mode is the known-good reference for a chat-visible HUD.** *(Post-fix update 2026-05-16: base mode swapped to `RP_GameMode_TeamDeathmatch_Auto.et` → stock `GameMode_TeamDeathmatch_Auto.et`; chat continues to work because the fix was placing `ScriptedChatEntity` in the world, not the HUD chain.)*

## What's been investigated

### Where chat HUD entries live (format reference)

EveronLife's `Character_Base.et` shows the format for adding an `InfoDisplay` entry to a `SCR_BaseHUDComponent`:

```
SCR_BaseHUDComponent "{520EA1D2DB118134}" {
  InfoDisplays {
    EL_BetaHud "{5CDCF78652BDD9FF}" {
      m_LayoutPath "{9165B2D1BAEA7F6A}UI/Layouts/HUD/Beta/Beta.layout"
      m_eLayer ALWAYS_TOP
    }
  }
}
```

EveronLife's `PlayerControllerRoleplayMP.et` shows how to override the parent's HUD manager (they clear it):

```
SCR_HUDManagerComponent "{2FDC275D9EBCDB8B}" {
  InfoDisplays {
  }
}
```

So to add a chat HUD, we need an `InfoDisplays { SCR_ChatXxx "{guid}" { m_LayoutPath "..."; m_eLayer ...; } }` block inside the PC's `SCR_HUDManagerComponent`.

### What's NOT known yet (the missing piece)

- The **exact `SCR_*` class name** for the chat HUD InfoDisplay entry (candidates: `SCR_ChatPanel`, `SCR_ChatHud`, `SCR_ChatPanelComponent`, or an `SCR_InfoDisplay` subclass).
- The **layout path** Reforger ships for the chat history widget (something like `UI/HUD/Modules/Chat/ChatPanel.layout`?).

### Resources to find them

- **Script API zip:** `/c/Program Files (x86)/Steam/steamapps/common/Arma Reforger Tools/Workbench/docs/ArmaReforgerScriptAPIPublic.zip`. Was about to extract and grep when paused. Already confirmed it ships with chat-related interface docs: `group__Chat.html`, `interfaceBaseChatComponent.html`, `interfaceBaseChatChannel.html`, plus full HUD docs (`interfaceSCR__BaseHUDComponent.html`, `interfaceHUDManagerComponent.html`).
- **Stock prefabs:** Reforger workshop addons live as `.pak` files in `C:\Users\bluej\Documents\My Games\ArmaReforger\addons\` — can't grep directly. Workbench addons in `C:\Users\bluej\Documents\My Games\ArmaReforgerWorkbench\addons\` (CNR_2025, EveronLife, VanillaTestProject, fob-builder) are text-readable. None of the workbench addons checked so far have a chat HUD entry to copy from.
- **Best comparison reference:** the Conflict-mode PlayerController prefab. Stock path likely `Prefabs/Characters/Core/DefaultPlayerControllerMP_Conflict.et` or similar. Worth checking if any workbench addon (CNR_2025?) wires to that and inherits the chat HUD.

## Suggested next steps for the next session

1. **Extract the Script API zip** to a temp folder and grep for `SCR_ChatPanel`, `SCR_ChatHud`, `SCR_ChatInfoDisplay`, etc. Identify the exact class hierarchy for chat HUD info displays.
2. **Find a stock Conflict-mode PC prefab** to read — either:
   - Check `addons/CNR_2025/` or `addons/VanillaTestProject/` for a Conflict-rooted PC,
   - Or extract the Reforger base game `.pak`s (research the right one — `data.pak` in the Reforger install dir),
   - Or use the [official Reforger Workshop wiki/docs](https://community.bistudio.com/wiki/Arma_Reforger).
3. **Add an InfoDisplay entry** to our `Prefabs/Characters/Core/DefaultPlayerControllerMP_Factions.et` under `SCR_HUDManagerComponent.InfoDisplays`, pointing at the chat HUD layout. Test in workbench first.
4. **Verify in-game:** chat input opens, submit a message, history panel appears.
5. Close issue #10 with a note about the fix.

## Files to touch

- `Prefabs/Characters/Core/DefaultPlayerControllerMP_Factions.et` — add `SCR_HUDManagerComponent` override with chat InfoDisplay.
- Possibly create a `.layout.meta`-paired layout if no stock chat layout works as-is. Probably not — stock chat layout should be fine.

## Branch / git state at pause

- Currently on `main`, working tree clean.
- Recent merges to main: PR #6 (Phase 1), #11 (radar page-swap), #12 (docs/phase split + README).
- `feature/phase-1` and `feature/phase-2` branches both exist.
- Recommended: branch `fix/chat-hud` from `main` for this work — it's orthogonal to Phase 2's LPR/watchlist/jail scope.

## Related memory entries

- [reforger_script_api_docs](../../.claude/projects/c--Users-bluej-Documents-My-Games-ArmaReforgerWorkbench-addons-cops-n-robbers-rp/memory/reforger_script_api_docs.md) — confirms Script API zip location.
- [reforger_mod_folder_locations](../../.claude/projects/c--Users-bluej-Documents-My-Games-ArmaReforgerWorkbench-addons-cops-n-robbers-rp/memory/reforger_mod_folder_locations.md) — workshop vs workbench addon paths.

## When fixed, consider saving as memory

Reference-type entry: "Reforger PC chat HUD wiring — needs explicit InfoDisplay entry; Deathmatch parent omits it by design." Include the final class name + layout path so future sessions don't redo this investigation.
