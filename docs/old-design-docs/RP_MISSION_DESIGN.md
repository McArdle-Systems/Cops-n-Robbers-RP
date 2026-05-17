# Arma Reforger RP Mission: "Cops n Robbers" (Modern Edition)

## 1. Mission Concept

Inspired by the classic "Cops n Robbers" for Operation Flashpoint, this mission aims to provide a dynamic role-playing experience in Arma Reforger. Players can choose from various roles, including law enforcement, emergency services, and civilians, each with unique objectives and interactions. The mission emphasizes emergent gameplay and player-driven narratives within a modern setting.

## 2. Core Factions

*   **Police Department (PD):** Enforce laws, respond to crimes, conduct patrols, and maintain public order.
*   **Emergency Medical Services (EMS):** Provide medical assistance, transport injured individuals, and respond to emergencies.
*   **Fire Department (FD):** (If possible with Arma Reforger's engine capabilities) Respond to fires, conduct rescue operations, and manage hazardous situations.
*   **Civilians:** Live out daily lives, engage in various jobs/activities, and interact with other factions. Civilians can also choose to engage in illegal activities.

## 3. Bot-First Philosophy

This is not a multiplayer mode with bot fillers. It is a single-player living-world that happens to support multiplayer.

Bots are not background — they are the **AI Director's actors**. They populate the streets, generate the calls, drive the pacing, and provide opposition. The first playtest target is one player, alone, having fun. Multiplayer is the bonus.

Practical implication: every system (economy, heat, jobs, arrests) is designed bot-first, human-second. If it works for a solo player against bots, it scales to humans.

## 4. Potential Additional Factions/Roles

*   **Criminal Organizations:** Player or bot-controlled groups focused on illicit activities.
*   **Journalists/Media:** Document events, report on ongoing situations, and influence public opinion.
*   **Delivery/Logistics:** Civilian roles focused on transporting goods and providing services.

## 5. Initial Gameplay Loops (High-Level)

*   **PD:** Patrols, traffic stops, responding to civilian distress calls, investigating crimes.
*   **EMS:** Responding to accidents, medical emergencies, transporting patients.
*   **FD:** (If implemented) Responding to fires, rescue operations.
*   **Civilians:** Undertaking jobs (e.g., taxi driver, delivery person), engaging in social interactions, participating in legal or illegal activities.

## 6. Phasing

Each phase ships a self-contained playable experience. No phase exists only as foundation work for the next — every phase's "done" state is a thing you can sit down and enjoy.

### Phase 0a — AI Behavior POC
**Goal:** Prove the custom AI approach works at all. A focused sandbox for the gaps Workshop doesn't fill — civilian behavior, AI Director, traffic basics. Not a mission. Not a product. A proving ground that graduates into a permanent regression testbed once the approach is validated.

**Scope:**
*   One Everon town (e.g., St. Pierre)
*   N civilian bots running experimental behavior trees
*   M PD patrol bots
*   Console/admin UI to spawn, kill, observe bot state, trigger Director events on demand
*   Metrics overlay: framerate, active bot count, distance-to-player

**Explicitly out of scope:**
*   Economy, money, shops
*   Persistence (use ephemeral state)
*   Player progression, jobs, drug runs
*   Multiplayer / server config

**Why before Phase 0:** Civilian AI is the largest custom workload AND the biggest engine risk. Iteration speed matters more than feature breadth here. The testbed survives forever as the regression sandbox — every behavior change can be validated against it.

**Promotion criteria (graduate behaviors into Phase 0):**
*   Civilian routines feel "alive" without scripted hand-holding
*   Framerate holds with 30+ active civilians in view
*   Director can fire 5–10 events/hour without obvious staging artifacts
*   A 10-minute solo patrol surfaces >2 emergent moments

### Phase 0 — The Living Town
**Goal:** Drive a patrol car through a populated town and have it feel like a place.

**Deliverables:**
*   Map locked in (default: Everon)
*   15–30 civilian bots with idle routines (walk, sit, enter/exit buildings)
*   4–8 PD bots on patrol routes
*   Player can spawn as PD; drive, walk, exit vehicle
*   Day/night cycle visibly affects the world
*   **Workshop survey complete** (see §8)

**Done when:** A 5-minute drive through town feels alive enough that you want to do it again.

### Phase 1 — Crime Happens
**Goal:** Bots commit crimes; you respond as cop.

**Deliverables:**
*   AI Director scaffold (one hardcoded event: petty theft)
*   Cuffing/arresting bots
*   Basic jail loop (bot detained → respawn elsewhere)
*   Police radio/dispatch UI surface for incoming calls

**Done when:** You can play "patrol cop" for an hour and have multiple meaningful calls.

### Phase 2 — Money & Robber Side
**Goal:** Both sides of the law are playable solo.

**Deliverables:**
*   Money/economy system
*   One job: drug run (one route, end-to-end)
*   Buyable starter vehicles
*   Wanted/heat system
*   Bot cops respond to player crime
*   Director event variety: 3–5 templates

**Done when:** A solo robber session is as fun as a solo cop session.

### Phase 3 — Friends Mode
**Goal:** Two players on opposite sides have a great time.

**Deliverables:**
*   Persistence (vehicles, money, gear stick across sessions)
*   2–3 more job types
*   Server hosting setup documented
*   Comms integration sanity-checked

**Done when:** A 2–3 person playtest leaves everyone wanting more.

### Phase 4 — Soft Public
**Goal:** A small Discord-only server that people choose to come back to.

**Deliverables:**
*   Polish + balance pass
*   More content (jobs, vehicles, locations)
*   Admin tools (kick, restore lost vehicles, anti-grief)
*   Onboarding / tutorial flow

**Done when:** Someone joins a second time without being asked.

## 7. Engine Reality / Known Gaps

Honest assessment of where Reforger helps us and where we hit walls.

**Hard problems (engine-level):**
*   **Civilian behavior is unbuilt.** Reforger ships military AI. Idle wandering, panic responses, calling 911 — all has to be authored. This is the largest single workload.
*   **Civilian-context vehicle AI is rough.** Combat-context driving works okay; sedans navigating town intersections + parked cars does not. Workarounds: short tested route segments, offscreen teleport-jumps, "delivery via despawn," walking-only bots in Phase 0.
*   **No traffic system.** No road graph, density management, or lane discipline out of box. Build or borrow.
*   **No fire dynamics.** FD stays cosmetic/RP unless someone mods fire propagation (massive undertaking).

**Buildable framework gaps:**
*   NPC interaction surfaces (talking, jobs, payment) — no built-in dialogue UI
*   Persistence layer (money/inventory/owned vehicles across sessions)
*   Economy backend
*   Heat/wanted system
*   Arrest/jail mechanic for bots specifically

**Content gaps:**
*   No real city map. Existing maps are villages/forests. Cap at "small town RP" until a city map exists.

**What we can lean on:**
*   Combat AI (decent for shootouts)
*   Waypoint system (works for walking + short vehicle runs)
*   Workshop ecosystem (see §8)
*   Player profile primitives (some scaffolding for persistence)

## 8. Workshop-First Strategy

Before building anything custom in a phase, survey Workshop. **Adopt > Fork > Inspire > Build.**

**Survey targets:**

*Civilian content*
*   Civilian character models & clothing
*   Civilian vehicle packs (sedans, trucks, vans)
*   Pedestrian skin variety

*Behavior & AI*
*   Civilian AI behaviors (walk routines, panic, dispatch reactions)
*   Traffic systems (road network, spawn density)
*   Bot squad management mods

*RP frameworks*
*   Existing RP gamemodes (study or fork)
*   Life-style frameworks
*   Character/save persistence systems

*Police gameplay*
*   Cuffing & arrest mods
*   Emergency vehicle siren/light frameworks
*   Ticketing / dispatch UIs
*   Police loadouts

*Interaction & UI*
*   Dialogue/menu frameworks
*   Shop / buy UIs
*   Economy backends

*Maps*
*   Urban-density terrains
*   Town extensions for existing maps

**Evaluation rubric for each candidate mod:**
*   Maintained? (last update within ~6 months)
*   License compatible?
*   Quality (bugs, performance)?
*   Decision: **Adopt / Fork / Inspire / Skip**

The survey is itself a Phase 0 deliverable. Output: a one-page table of "what we found, what we're using, what we're building." That table becomes the build plan for everything after.

## 9. Workshop Survey Results (2026-04-29)

Detailed mod list and version pins live in `RP_DEPENDENCIES.md`. Headline summary:

**Adopt directly (use-as-is, dependency only):**
*   `EnfusionPersistenceFramework` (MIT, ~1M downloads) — the de facto save/load layer
*   `ACE Captives` (GPLv2, 800K downloads) — surrender, zip cuffs, frisk, restrained state
*   `Reloadz Vehicles` + `Reloadz Police` + `Sheriff Pack` (APL-ND) — civilian + LEO vehicles, uniforms, Impound Sheet
*   `Vinny - Civilians` (APL-SA), `Domins Civilian Expansion` (APL) — skin variety, EMS/FD cosmetics
*   `Better Everon` (APL) — densifies existing towns

**Fork (carry source, modify):**
*   `Shop System` (APL-SA on GitHub) — extend with sell flow

**Reference only (read, don't fork):**
*   `EveronLife` (MIT) — initially considered as a fork base, but hands-on inspection (2026-04-29) revealed it's a thin tech demo, not a framework. Genuinely useful only as a worked example of EPF + EDF persistence wiring. Build clean against the frameworks directly; consult EveronLife source when stuck on persistence gotchas.

**Inspire (study, don't import):**
*   `Cops V Gangs Life` — closest concept match
*   `Aurora Life` — role-split validation
*   `Police Sirene` — audio/light component reference

**Build from scratch (gaps Workshop doesn't fill):**
*   Civilian idle/panic AI — the largest custom workload
*   Traffic system (road graph, density, lane discipline)
*   AI Director (event scheduling, intensity curve)
*   Dispatch / 911 / ticketing UI
*   NPC dialogue trees
*   Heat/wanted system
*   Jail / booking lifecycle

**Map ceiling:** No urban-density city map exists. "Small-town RP" cap holds until one ships. Better Everon is the best play for now.

**Net effect on plan:** Workshop saves nearly all the polish/plumbing in Phases 1–3 (persistence, cuffing, shops, vehicles, uniforms). Custom build narrows to AI Director + civilian behavior + LEO UX layer. Tighter scope than the original framing.
