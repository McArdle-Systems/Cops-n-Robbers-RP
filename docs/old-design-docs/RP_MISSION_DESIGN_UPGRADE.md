# Cops n Robbers: The 2026 "Active Surveillance" Upgrade

## 🚨 Core Vision
Transform the "Cops n Robbers" experience from a passive patrol into an active, sensor-driven investigation. The player should use the vehicle's technology (LPR/Radar) to "hunt" for criminal activity.

## 🖥️ The 2026 HUD System (The "Eyes")
The HUD is a multi-state, toggleable interface overlaid on the player's viewport.

### 1. LPR (License Plate Reader) Module
*   **Mechanism:** A continuous scanning loop that compares passing vehicle plates against the `Warrant_Registry`.
*   **Visual States:**
    *   **`SCANNING` (Default):** A subtle, pulsing blue radar sweep on the UI.
    *   **`MATCH_FOUND` (Alert):** High-intensity **RED** flash on the HUD. Triggers a loud, high-priority audio alarm.
*   **Interaction:** One-tap toggle `[LPR: ON/OFF]`.
*   **Data Display:** When a match occurs, the UI displays: `[PLATE_ID] | [WARRANT_TYPE] | [CRIME_LEVEL]`.

### 2. Radar/Speed Module
*   **Mechanism:** Proximity-based speed detection.
*   **Visual States:**
    *   **`CLEAR`:** Minimalist dot-map of nearby entities.
    *   **`VIOLATION`:** Speeding/Traffic violation icons appear when a threshold is exceeded.
*   **Interaction:** One-tap toggle `[RADAR: ON/OFF]`.

### 3. Manual Interrogation Interface
*   **Trigger:** Proximity interaction with an NPC or Vehicle.
*   **Functionality:** A manual lookup field allowing the player to input `Name`, `Plate`, or `ID` to query the `Warrant_Registry` manually.

## 🚔 The Arrest & Booking Lifecycle (The "Hands")
Leveraging `ACE Captives` for the physical mechanics, but adding the "Police Logic" layer.

### 1. The Arrest Sequence
*   **Phase A (Subdue):** Use `ACE Captives` (Taser/Handcuffs) to transition the NPC to a `RESTRAINED` state.
*   **Phase B (Transport):** The NPC is moved to the `VEHICLE_REAR_SEAT` transform.

### 2. The Booking Cycle
*   **The "Station" Trigger:** Entering the `Police_Station` zone triggers the `Booking_Sequence`.
*   **The "Jail" State:** The NPC is moved to a `Jail_Cell` object and assigned a `Time_In_Custody` timer.
*   **The "Release" Loop:** 
    *   Upon `Timer_Expiry`, the NPC is automatically "Released" (re-spawned at a random nearby intersection).
    *   **Goal:** Creates a "revolving door" effect where criminals are a persistent, cyclical threat.

## 🛠️ Technical Dependencies
*   **`ACE Captives`**: For the physical cuffing/restraint physics.
*   **`Reloadz Vehicles/Police`**: For the LEO-specific vehicle assets.
*   **`Warrant_Registry`**: A centralized database/JSON object containing active criminal records.
