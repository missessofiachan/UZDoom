# UZDoom Source Port: Comprehensive Project Analysis, Competitor Feature Matrix, and Architectural Roadmap

## 1. Executive Summary

**UZDoom** represents a modern evolutionary step in the Doom source port ecosystem. Branching from the highly flexible ZDoom/GZDoom lineage, UZDoom carves out a distinct competitive identity by prioritizing premium user experience (UX), native platform integrations (such as zero-dependency Discord Rich Presence IPC), and advanced physical feedback mechanisms (sophisticated gamepad and trigger haptics). 

While traditional source ports focus heavily on either absolute preservation (Chocolate Doom, Crispy Doom) or specialized modern execution domains (DSDA-Doom for speedrunning, Helion for hyper-performance rendering), UZDoom aims to be the definitive "modern-comfort, high-immersion" engine. This document provides a exhaustive analysis of UZDoom's technical baseline relative to the ecosystem, examines inspirations from alternative ports, and establishes a concrete, multi-tiered engineering roadmap for future development.

---

## 2. Comprehensive Feature Matrix

The following matrix compares UZDoom against primary contemporary source ports across critical architectural, performance, and user-experience parameters.

| Feature / Attribute | UZDoom | GZDoom | DSDA-Doom | Woof! | Chocolate Doom | Doom Retro | Crispy Doom | Helion |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Primary Design Philosophy** | Modern UX & Premium Haptics | Universal modding & compatibility | Speedrunning & demo recording | Modern MBF21 standard | Nostalgic preservation | High-polish micro-details | Enhanced vanilla comfort | GPU-driven slaughter maps |
| **Main Render Engine** | OpenGL / Vulkan / Software | OpenGL / Vulkan / Software | OpenGL / Software | Software (Widescreen) | Software (Original) | Software (True Color) | Software (Widescreen) | Custom GPU (Java) |
| **Scripting Capabilities** | ZScript, Decorate, ACS | ZScript, Decorate, ACS | DeHackEd, BEX | DeHackEd, MBF21 | None | Custom ExTND DeHackEd | DeHackEd | None |
| **Haptic Feedback & Rumble** | **Advanced** (Body & Trigger) | Basic / Standard | No | No | No | No | No | No |
| **Discord Rich Presence** | **Native** (Zero-dep IPC) | Optional (Heavy lib dependency) | No | No | No | No | No | No |
| **Rewind & Practice Tools** | No | No | **Yes** (Keys & Timeline) | No | No | No | No | No |
| **Interactive Automap Markers** | Basic | Basic | Map markers | Map markers | None | **Advanced** (Interactions) | Basic | Basic |
| **Multiplayer Netcode** | P2P Lockstep | P2P Lockstep | P2P Lockstep | P2P Lockstep | P2P Lockstep | N/A (Local/IPX) | P2P Lockstep | N/A |
| **Mod Downloader/Browser** | No | No | No | No | No | No | No | No |
| **Micro-Details (e.g. Gore)** | Standard | Standard | Minimal | Minimal | Original | **High Polish** (Wall drips) | High (Wreckage splats) | Minimal |
| **Slaughtermap Performance** | Average | Average | Good | Excellent | Crashes (Limits) | Good | Good | **Exceptional** |

---

## 3. Deep-Dive Analysis of Competitor Ports (Inspirational Mechanics)

To further elevate UZDoom, specific design achievements from specialized ports can be isolated, analyzed, and adapted.

### 3.1 DSDA-Doom / PrBoom+ (Speedrun Optimization & Input Verification)
* **Frame-Perfect Rewind System:** DSDA-Doom records full game-state snapshots directly into a cyclic memory buffer every game tic (1/35th of a second). Because Doom's game logic is completely deterministic, tracking delta changes across player coordinates, sector positions, and active thinker tables allows instantaneous rollback. This bypasses the typical "load game" latency, establishing an invaluable loop for high-difficulty encounter practice.
* **Key-Press Input Overlay:** Draws highly visible HUD layouts reflecting raw asynchronous input data (binary status of Forward, Back, Strafe, Use, Fire, and angular mouse deltas). This serves an architectural role in verifying demo correctness and adds immense presentation value for content creators.
* **Interactive Demo Analysis:** Extends the `.lmp` playback engine to decoupled temporal states. By indexing block data within the demo stream, players can fast-forward, slow down, pause, or skip to arbitrary timestamps without desynchronizing the underlying engine logic.
* **Strict Compatibility Level Settings (`complevel`):** Implements rigorous code-path branching that switches core physics rules, enemy AI behaviors, and actor boundary logic on the fly to exactly emulate historical engines (e.g., Doom v1.9, Boom, MBF).

### 3.2 Doom Retro (Aesthetic Polishing & Atmosphere)
* **Gravity-Induced Decal Sliding:** Rather than treating bullet puffs and blood splatters as static texturing, Doom Retro assigns active, ticking structures to decals. Blood splatters on vertical surfaces gradually increment their Y-coordinate down based on a localized gravity simulation, leaving faint alpha trails until they settle or hit floor boundaries.
* **Dynamic Shell Casings:** Spawns physics-enabled cosmetic actors when weapons cycle. These actors possess localized mass and velocity vectors, bouncing off geometric boundaries and triggering surface-dependent acoustic sound effects (e.g., brass clicking against a concrete sector floor vs. landing silently on water).
* **Muzzle Flash Light Gradients:** Injects real-time dynamic light sources that don't just illuminate walls, but alter the gamma/color tint of the weapon viewport sprite itself, grounding the weapon model into the virtual environment.
* **Reactive HUD Feedback:** Modifies the baseline status bar drawing routines to overlay full-screen/HUD alpha-blended blood splatters or dynamic red chromatic aberration shifts relative to the direction and severity of received damage.
* **Advanced Automap Features:** Transforms the static automap into an interactive radar interface. Locked doors pulse in synchronization with their corresponding red/blue/yellow keycard requirements, and item counters dynamically flag items with distinct iconography once they enter the player's direct line of sight.

### 3.3 Crispy Doom (Enhanced Vanilla Ergonomics)
* **Widescreen Asset Emulation:** Utilizes adaptive wide graphics stretching/filling techniques. By extending the bounds of original 4:3 graphics seamlessly to 16:9 or 21:9 via smart procedural padding or asset cropping, the engine provides an uninhibited panoramic field of view without introducing geometric distortion or generic black pillarboxes.
* **Visual Assist Overlays:** Implements native HUD helper elements, including dynamic target crosshairs, brightmap texture alignments, and directional sound indicator arrows to improve game accessibility.

### 3.4 Helion (GPU-Driven Slaughtermap Architectural Engineering)
* **GPU-Driven Occlusion Culling:** Bypasses classic CPU-bound Binary Space Partitioning (BSP) rendering bottlenecks. Helion compiles map geometry directly into highly optimized static GPU VBOs/IBOs at load time, offloading frustrating line-of-sight and visibility sorting routines to advanced occlusion culling and instanced draw passes, scaling effortlessly to tens of thousands of simultaneous rendered actors.

---

## 4. Comprehensive Engineering Roadmap for UZDoom

The following development matrix outlines actionable milestones for UZDoom, categorized by complexity, implementation strategy, and structural engine impacts.

### 🟢 4.1 Easy Wins (Low Effort, High Immediate Reward)

#### 4.1.1 Auto-Run Flag for `compile.sh`
* **Description:** Provide developers and power users with immediate feedback loops by allowing compilation and automated execution within a single command string.
* **Implementation Strategy:** Integrate standard bash `getopt` parsing within the existing build scripts to capture `--run` or `-r`. Capture all trailing tokens via standard argument forwarding (`"$@"`) and pass them cleanly to the compiled binary context:
    ```bash
    ./compile.sh --run -iwad doom2.wad -file custom.wad +map map01
    ```
* **Status:** **[COMPLETED]** #### 4.1.2 Linux IWAD Search Path Expansion
* **Description:** Eradicate modern directory configuration friction on Linux operating systems by automatically discovering legal game data installations across containerized runtimes and third-party stores.
* **Implementation Strategy:** Open `src/d_iwad.cpp` and inject explicit search strings targeting standard paths for Steam, flatpaks, and Heroic Launcher.
    * *Target Paths to Append:*
        * `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Doom 2/base/`
        * `~/.var/app/com.heroicgameslauncher.hgl/config/heroic/`
        * `~/Games/Heroic/`
        * `~/.local/share/Steam/steamapps/common/Ultimate Doom/base/`

#### 4.1.3 Rich Presence Party Support
* **Description:** Boost modern multiplayer connectivity visibility by broadcasting active lobby states directly to Discord profile cards.
* **Implementation Strategy:** Extend the custom zero-dependency IPC framework within `src/discord_presence.cpp`. Map network session parameters (`netnodes`, `maxnodes`) straight into the outgoing JSON serialization buffer:
    ```json
    {
      "partyId": "Lobby_UUID_String",
      "partySize": 3,
      "partyMax": 4
    }
    ```

#### 4.1.4 Rich Presence Custom WAD Graphics
* **Description:** Dynamically represent legendary community megawads on Discord by switching rich presence imagery based on active file hashes or file names.
* **Implementation Strategy:** Within `I_TickDiscordPresence()`, implement a fast string/hash look-up map comparing active elements in the global `Wads` structure against known historical IDs (*Sigil*, *Eviternity*, *Ancient Aliens*, *MyHouse.wad*). When a match is verified, swap the default `large_image` payload key with the respective community asset asset ID.

#### 4.1.5 Quick Console Command for Discord Status
* **Description:** Enable immediate telemetry logging and debugging by allowing users to instantly output or copy current session status strings.
* **Implementation Strategy:** Bind a new console command (`CCMD(copy_discord_status)`) inside the rich presence source file. Collect current map name, completion metrics, and lobby status, formatted into a standard clipboard copy routine via native platform abstractions (X11/Wayland clipboard selections on Linux, `OpenClipboard` on Windows).

---

### 🟡 4.2 Medium Wins (Medium Effort, High Strategic Value)

#### 4.2.1 In-Game Gamepad Haptics Calibration Menu
* **Description:** Provide users with explicit physical fine-tuning over UZDoom's standout advanced haptics ecosystem.
* **Implementation Strategy:** Add new engine-wide CVars: `haptics_trigger_intensity` (float), `haptics_body_intensity` (float), and `haptics_frequency_multiplier` (float). Construct a responsive UI settings menu panel via `menudef.txt`. Embed a native callback button within the menu code that triggers a brief 250ms diagnostic rumble pulse at the modified settings whenever values are scrubbed.

#### 4.2.2 Doom Retro-Style Automap Interactions
* **Description:** Infuse visual readability and clarity into the technical wireframe automap display.
* **Implementation Strategy:** Modify the line and polygon drawing loops situated in `src/am_map.cpp`. Interrogate sector line structures (`line_t`); if a line is bound to a locked door special activation type, look up the required lock ID and alter the render color index to match the key color (Red, Blue, Yellow). For actors, query item flags (`MF_SPECIAL`); if discovered within the player's field of view but uncollected, draw a persistent, desaturated texture sprite icon at those structural coordinates.

#### 4.2.3 Dynamic Hitmarker & Damage Crosshair // already mods
* **Description:** Increase weapon tactile satisfaction by injecting instantaneous, clear visual feedback loops on successful enemy penetration.
* **Implementation Strategy:** Tap into the core actor damage pipeline (`AActor::TakeDamage`). If the damage source originates from the local player actor, set a floating global frame timer (`hitmarker_ticks = 6`). Modify the HUD crosshair rendering routines to overlay a scaled, alpha-blended diagonal tick mark array or color shift whenever `hitmarker_ticks > 0`, decrementing the frame clock every engine cycle.

#### 4.2.4 Improved Soundfont (.sf2/.dls) Management
* **Description:** Allow seamless, on-the-fly customization of the game’s MIDI playback soundtrack environment without requiring external toolchains.
* **Implementation Strategy:** Introduce a dedicated system folder (`soundfonts/`). Write a directory scanning module that runs during early initialization to catalog valid `.sf2` and `.dls` binary files, populating a selection CVar. When a user selects a different file via the audio options menu, drop the current MIDI synthesizer instance and immediately hot-reload the audio engine stream using the newly targeted soundfont path.

#### 4.2.5 Autosave Customization (Smart Auto-Saves) // already mod
* **Description:** Prevent frustrating combat deaths from resetting massive exploration progress while simultaneously avoiding gameplay micro-stutters during intense encounters.
* **Implementation Strategy:** Implement an automated checkpoint manager module. The manager tracks player proximity variables and enemy aggression levels (`AActor::IsTargeted`). It authorizes an background autosave task *only* when the player enters a new sector or acquires a key item, and when the global player combat alert status has dropped below a specified threshold for more than 5 consecutive seconds.

---

### 🔴 4.3 Harder Wins (High Effort, Significant Structural Coding)

#### 4.3.1 Action Rewind Timeline Buffer
* **Description:** Introduce an advanced, frame-accurate rollback mechanic to assist players during high-difficulty map sections.
* **Implementation Strategy:** Create a large, fixed-size circular ring memory buffer in RAM. Every single game tic (35Hz), serialize unstable mutable level components: player positioning/velocity, actor health states, active projectile vectors, sector heights/lighting adjustments, and active thinker nodes. 
* **Architectural Challenge:** When the rewind button is actively depressed, the normal physics engine ticker must stall, and the system must pull historical frames sequentially from the circular queue. Because GZDoom architectures are highly dynamic and rely on pointer-heavy object graphs, serialization must be optimized using low-overhead memory snapshotting or custom localized structure tracking to avoid memory fragmentation and micro-stutters.

#### 4.3.2 Widescreen Asset Auto-Generation / Patching
* **Description:** Cleanly present classic legacy 4:3 art elements on ultra-wide screens without requiring manual, file-heavy graphic modification packs.
* **Implementation Strategy:** Hook into the primary 2D GUI drawing states. If a non-widescreen asset (such as an intermission map background or title screen) is called for drawing, inject a post-processing shader pipeline. The shader samples the boundary edge pixels of the 4:3 asset and applies horizontal mirrored stretching, smart blurring, or neural procedural edge expansion to fill out the remaining widescreen viewport aspect ratio safely.

#### 4.3.3 Weapon Bobbing Inertia & Tilt //already mod
* **Description:** Replace mechanical, predictable weapon sprite swaying with a dynamic, physical weight model.
* **Implementation Strategy:** Completely refactor the weapon sprite transformation logic inside `src/p_pspr.cpp`. Replace the baseline periodic mathematical sine wave formulas with a real physical mass-spring-damper math model. The weapon viewport coordinates should experience lag and inertia based on active player velocity vectors, dipping downward upon heavy landing impacts, tilting outward based on high-speed rotational mouse look deltas, and swaying organically based on weapon weight constants.

#### 4.3.4 Interactivity-Driven Blood Sliding & Splatters // already mod
* **Description:** Create a dynamic, highly persistent visual narrative of combat history directly onto level geometry.
* **Implementation Strategy:** Modify the decal spawning and update loops (`src/p_decal.cpp`). For wall-bound decals, append an active thinking component that updates at reduced rates (e.g., 10Hz). Calculate localized downward velocity based on an arbitrary gravity constant and surface parameters. For floors, track player and monster movement steps; if an actor crosses a sector heavily populated with blood decals, cache a short state modifier causing the actor to spawn fading footprint decals for its next 5 steps.

---

### 🟣 4.4 Experimental / Long-Term Wins (Very High Complexity)

#### 4.4.1 Hot-Reloadable ZScript Engine
* **Description:** Revolutionize the development lifecycle for modders by allowing real-time script adjustments without necessitating a application reboot.
* **Implementation Strategy:** Modularize the ZScript virtual machine (VM) boundaries. The compilation phase must be abstracted to compile incoming script text into isolated, transient bytecode blocks. 
* **Architectural Challenge:** Traditional ZScript architectures tightly tie compiled bytecode definitions directly to permanent native actor structures. Hot-reloading requires mapping a structural translation layer that safely updates internal VM function pointer tables, updates live instances of actors in memory to point to new method addresses, and safely reconciles field variable offsets without corrupting active game memory state or throwing segmentation faults.

#### 4.4.2 In-Game idgames WAD Downloader & Launcher
* **Description:** Eliminate the need for web browsers and external zip file managers by delivering an integrated, native mod distribution ecosystem.
* **Implementation Strategy:** Write an asynchronous network client service utilizing `libcurl` running completely decoupled from the primary engine loops. Construct a bespoke menu UI that queries the official `/idgames` database API via JSON. Downloaded packages are streamed directly into a localized cache directory, automatically extracted via a native unzipping utility library, and passed directly to the internal WAD loading structure, dynamically recycling the active engine state to launch the downloaded content cleanly.

#### 4.4.3 Multi-Threaded BSP Traverser
* **Description:** Future-proof the engine's processing capacity for hyper-complex slaughter maps featuring tens of thousands of active actors.
* **Implementation Strategy:** Radically overhaul the execution model of the core engine. Classic Doom performs visibility queries, physics checks, actor interaction ticks, and line-of-sight operations sequentially inside a single, primary execution thread. This task requires decoupling the static BSP tree structure into a strictly read-only concurrent memory model during the traversal phase.
* **Architectural Challenge:** Multi-threaded workers must be spawned to process independent sectors or actor arrays simultaneously. However, because ZScript execution blocks can dynamically alter world states, manipulate sector geometry, or destroy actors arbitrarily mid-tick, a robust, ultra-fast thread synchronization layer or command-buffering queue must be implemented to collect thread changes and apply them atomically at the end of the game tick, avoiding catastrophic race conditions.
