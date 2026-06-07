# Lessons Learned: Discord Rich Presence in UZDoom

Here are the key takeaways and technical insights gained from implementing Discord Rich Presence (RPC) in this codebase:

## 1. Custom Protocol vs. Heavy Libraries
- **The Pitfall**: The original integration relied on the legacy `discord-rpc` library, which introduced hundreds of lines of boilerplate, socket polling layers, and build complexity. Linking prebuilt binary blobs often leads to architecture or `glibc` version mismatches (especially on immutable/distrobox setups).
- **The Lesson**: Implementing Discord's local IPC protocol directly is incredibly simple. It uses Unix Domain Sockets on Linux/macOS (`/run/user/<UID>/discord-ipc-0` or `/tmp/discord-ipc-0`) and Named Pipes on Windows (`\\.\pipe\discord-ipc-0`). 
- Doing this natively in a single C++ source file keeps compilation completely cross-platform, dependency-free, and straightforward.

## 2. Non-Blocking Background Threading
- **The Pitfall**: Doing blocking I/O operations (like socket connection retries or read/writes) on the main game thread can cause frame stuttering or freezes.
- **The Lesson**: Encapsulating the RPC client in a background thread that sleeps periodically ensures the engine's main render loop runs completely unimpeded. Reconnection attempts are done asynchronously without affecting gameplay.

## 3. Light JSON Construction in C++
- **The Pitfall**: Pulling in massive third-party JSON libraries (like `nlohmann/json` or `rapidjson`) just to send a few status updates adds unnecessary bloat.
- **The Lesson**: Simple string interpolation is perfectly safe and efficient for generating JSON payloads if a proper character escape helper (e.g., handling `\` and `"`) is used.

## 4. Hooking Into the Doom Engine State
Navigating the codebase revealed the primary structures for accessing game information:
- **Level/Map details**: Accessed via `primaryLevel->MapName` and `primaryLevel->LevelName`.
- **Kills and Secrets**: Tracked dynamically inside the `primaryLevel` locals:
  - Monsters: `primaryLevel->killed_monsters` / `primaryLevel->total_monsters`
  - Secrets: `primaryLevel->found_secrets` / `primaryLevel->total_secrets`
- **Active Weapon**: Accessible via `players[consoleplayer].ReadyWeapon->GetTag()`, which automatically retrieves the localized, user-friendly weapon name.
- **Player Health**: Accessed using `players[consoleplayer].health` (capped at `0` for readability when dead).
- **Difficulty (Skill) Name**: Retrieved cleanly using the global helper `G_SkillName()` (declared in `g_mapinfo.h`), which yields the translated difficulty string (e.g., `Hurt Me Plenty`).
- **Loaded WAD**: Obtained using the virtual file system manager:
  `fileSystem.GetResourceFileName(fileSystem.GetFileContainer(primaryLevel->lumpnum))`
