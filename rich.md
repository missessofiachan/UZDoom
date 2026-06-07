Here is the complete boilerplate implementation for **uzdoom** alongside a comprehensive markdown guide to step you through the integration process.

This implementation adapts the zero-dependency, direct IPC architecture from RealRTCW into a structure optimized for a Doom-style ticker engine, handling the 8-byte framing header and utilizing smart dynamic state tracking.

---

## 1. Header File: `discord_rpc_doom.h`

```c
#ifndef DISCORD_RPC_DOOM_H
#define DISCORD_RPC_DOOM_H

#ifdef __cplusplus
extern "C" {
#endif

// Public interface functions called by the main engine loops
void Discord_Init(void);
void Discord_RunFrame(void);
void Discord_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // DISCORD_RPC_DOOM_H

```

---

## 2. Source File: `discord_rpc_doom.c`

```c
#include "discord_rpc_doom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef WIN32
#include <windows.h>
#define getpid GetCurrentProcessId
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#endif

// --- Mocking Doom Engine Components (Replace with actual engine includes) ---
// In actual uzdoom, include headers like: "d_event.h", "g_game.h", "p_local.h", etc.
typedef enum {
    GS_LEVEL,
    GS_INTERMISSION,
    GS_FINALE,
    GS_DEMOSCREEN,
    GS_LEVEL_MENU // representation of being in main menu
} gamestate_t;

// Simulated global engine variables matching uzdoom architecture
extern gamestate_t gamestate;
extern const char* cur_iwad_name; // e.g. "doom2.wad", "plutonia.wad"

typedef struct {
    char mapname[8];       // e.g., "MAP01" or "E1M1"
    char level_name[64];   // e.g., "Hangar"
} level_info_t;
extern level_info_t level;

typedef struct {
    int health;
    const char* ready_weapon_name; // Acquired via ReadyWeapon->GetClass()->TypeName.GetChars()
} player_t;
extern player_t players[1]; 
#define consoleplayer 0
// ----------------------------------------------------------------------------

// Discord Protocol Constants
#define DISCORD_APPLICATION_ID "YOUR_DISCORD_APP_ID_HERE" // Change this to your client ID
[cite_start]#define THROTTLE_TIME_SECONDS 4 [cite: 10, 125]

typedef enum {
    DISCORD_STATE_DISCONNECTED,
    DISCORD_STATE_CONNECTED
} discordState_t;

#ifdef WIN32
static HANDLE discord_pipe = INVALID_HANDLE_VALUE;
static HANDLE worker_thread = NULL;
static CRITICAL_SECTION payload_mutex;
[cite_start]#define sleep_ms(x) Sleep(x) [cite: 120]
#else
static int discord_fd = -1;
static pthread_t worker_thread;
[cite_start]static pthread_mutex_t payload_mutex = PTHREAD_MUTEX_INITIALIZER; [cite: 66]
[cite_start]#define sleep_ms(x) usleep((x) * 1000) [cite: 120]
#endif

static discordState_t discord_conn_state = DISCORD_STATE_DISCONNECTED;
static char pending_payload[1024] = "";
static int pending_update = 0;
static int thread_active = 0;
static time_t next_allowed_update_time = 0;
static time_t start_time = 0;

[cite_start]// Dynamic State-Tracking Variables to prevent profile update spamming [cite: 8]
[cite_start]static int last_health = -1; [cite: 8, 72]
static char last_weapon[64] = "";
static char last_map[16] = "";
static gamestate_t last_gamestate = -1;

[cite_start]// Escape JSON payload characters gracefully to prevent syntax issues [cite: 16, 84]
static void EscapeJsonString(const char *in, char *out, int maxlen) {
    int j = 0;
    [cite_start]for (int i = 0; in[i] && j < maxlen - 5; i++) { [cite: 85]
        if (in[i] == '"') { out[j++] = '\\'; out[j++] = '"'; [cite_start]} [cite: 85]
        else if (in[i] == '\\') { out[j++] = '\\'; out[j++] = '\\'; [cite_start]} [cite: 86]
        else if (in[i] == '\n') { out[j++] = '\\'; out[j++] = 'n'; [cite_start]} [cite: 87]
        else { out[j++] = in[i]; [cite_start]} [cite: 89]
    }
    out[j] = '\0';
}

[cite_start]// Handshake protocol framing to communicate natively with local Discord app [cite: 2]
static int Discord_WritePacket(uint32_t opcode, const char *json_payload) {
    uint32_t len = (uint32_t)strlen(json_payload);
    uint8_t header[8];
    memcpy(header, &opcode, 4);   [cite_start]// 0 = Handshake, 1 = Frame Update [cite: 116]
    memcpy(header + 4, &len, 4);  [cite_start]// Length of JSON string [cite: 116]

#ifdef WIN32
    if (discord_pipe == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    [cite_start]if (!WriteFile(discord_pipe, header, 8, &written, NULL) || written != 8) return 0; [cite: 5]
    [cite_start]if (!WriteFile(discord_pipe, json_payload, len, &written, NULL) || written != len) return 0; [cite: 5]
#else
    if (discord_fd < 0) return 0;
    [cite_start]if (send(discord_fd, header, 8, 0) != 8) return 0; [cite: 96]
    [cite_start]if (send(discord_fd, json_payload, len, 0) != (ssize_t)len) return 0; [cite: 96]
#endif
    return 1;
}

static void Discord_CloseConnection(void) {
#ifdef WIN32
    if (discord_pipe != INVALID_HANDLE_VALUE) { CloseHandle(discord_pipe); discord_pipe = INVALID_HANDLE_VALUE; [cite_start]} [cite: 90]
#else
    if (discord_fd >= 0) { close(discord_fd); discord_fd = -1; [cite_start]} [cite: 91]
#endif
    discord_conn_state = DISCORD_STATE_DISCONNECTED;
}

static int Discord_EstablishConnection(void) {
    if (discord_conn_state == DISCORD_STATE_CONNECTED) return 1;

#ifdef WIN32
    char pipe_path[128];
    for (int i = 0; i < 10; i++) {
        [cite_start]snprintf(pipe_path, sizeof(pipe_path), "\\\\.\\pipe\\discord-ipc-%d", i); [cite: 101]
        [cite_start]HANDLE pipe = CreateFileA(pipe_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); [cite: 5, 102]
        if (pipe != INVALID_HANDLE_VALUE) {
            discord_pipe = pipe;
            discord_conn_state = DISCORD_STATE_CONNECTED;
            break;
        }
    }
#else
    [cite_start]const char *dirs[] = {getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), "/tmp"}; [cite: 6, 104]
    struct sockaddr_un addr;
    
    for (int d = 0; d < 3; d++) {
        [cite_start]if (!dirs[d] || !dirs[d][0]) continue; [cite: 105]
        for (int i = 0; i < 10; i++) {
            [cite_start]int fd = socket(AF_UNIX, SOCK_STREAM, 0); [cite: 6, 106]
            if (fd < 0) continue;
            
            memset(&addr, 0, sizeof(addr));
            [cite_start]addr.sun_family = AF_UNIX; [cite: 105]
            [cite_start]snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/discord-ipc-%d", dirs[d], i); [cite: 106]
            
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                discord_fd = fd;
                discord_conn_state = DISCORD_STATE_CONNECTED;
                break;
            }
            close(fd);
        }
        if (discord_conn_state == DISCORD_STATE_CONNECTED) break;
    }
#endif

    if (discord_conn_state == DISCORD_STATE_CONNECTED) {
        // Send internal initialization handshake frame (Opcode 0)
        char handshake[128];
        snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", DISCORD_APPLICATION_ID);
        if (!Discord_WritePacket(0, handshake)) {
            Discord_CloseConnection();
            return 0;
        }
        return 1;
    }
    return 0;
}

[cite_start]// Background thread loop to bypass networking stutter inside the main renderer [cite: 4]
#ifdef WIN32
DWORD WINAPI Discord_WorkerThread(LPVOID lpParam) {
#else
void *Discord_WorkerThread(void *arg) {
#endif
    while (thread_active) {
        char active_payload[1024] = "";
        int dynamic_update_queued = 0;

#ifdef WIN32
        EnterCriticalSection(&payload_mutex);
#else
        [cite_start]pthread_mutex_lock(&payload_mutex); [cite: 118]
#endif
        if (pending_update) {
            strncpy(active_payload, pending_payload, sizeof(active_payload));
            pending_update = 0;
            dynamic_update_queued = 1;
        }
#ifdef WIN32
        LeaveCriticalSection(&payload_mutex);
#else
        [cite_start]pthread_mutex_unlock(&payload_mutex); [cite: 119]
#endif

        if (dynamic_update_queued) {
            if (Discord_EstablishConnection()) {
                // Send rich presence payload update frame (Opcode 1)
                if (!Discord_WritePacket(1, active_payload)) {
                    Discord_CloseConnection();
                }
            }
        }
        sleep_ms(100);
    }
    Discord_CloseConnection();
    return 0;
}

void Discord_Init(void) {
    [cite_start]if (thread_active) return; [cite: 121]
    thread_active = 1;
    
#ifdef WIN32
    [cite_start]InitializeCriticalSection(&payload_mutex); [cite: 122]
    [cite_start]worker_thread = CreateThread(NULL, 0, Discord_WorkerThread, NULL, 0, NULL); [cite: 122]
#else
    [cite_start]pthread_mutex_init(&payload_mutex, NULL); [cite: 122]
    [cite_start]pthread_create(&worker_thread, NULL, Discord_WorkerThread, NULL); [cite: 122]
#endif
}

void Discord_Shutdown(void) {
    [cite_start]if (!thread_active) return; [cite: 123]
    thread_active = 0;
    
#ifdef WIN32
    if (worker_thread != NULL) { WaitForSingleObject(worker_thread, 1000); CloseHandle(worker_thread); worker_thread = NULL; [cite_start]} [cite: 123]
    [cite_start]DeleteCriticalSection(&payload_mutex); [cite: 124]
#else
    [cite_start]pthread_join(worker_thread, NULL); [cite: 124]
    [cite_start]pthread_mutex_destroy(&payload_mutex); [cite: 124]
#endif
}

[cite_start]// Packages the text and passes it cleanly across the thread mutation pipeline [cite: 26]
static void Discord_UpdatePresence(const char *details, const char *state) {
    char esc_details[256], esc_state[256];
    [cite_start]EscapeJsonString(details, esc_details, sizeof(esc_details)); [cite: 131]
    [cite_start]EscapeJsonString(state, esc_state, sizeof(esc_state)); [cite: 131]

    char timestamp_block[64] = "";
    if (start_time != 0) {
        [cite_start]snprintf(timestamp_block, sizeof(timestamp_block), "\"timestamps\":{\"start\":%lld},", (long long)start_time); [cite: 130]
    }

    char payload[1024];
    snprintf(payload, sizeof(payload),
        "{"
        "\"cmd\":\"SET_ACTIVITY\","
        "\"args\":{"
            "\"pid\":%d,"
            "\"activity\":{"
                "\"details\":\"%s\","
                "\"state\":\"%s\","
                "%s"
                "\"assets\":{"
                    "\"large_image\":\"doom_main\","
                    "\"large_text\":\"uzdoom Engine\""
                "}"
            "}"
        "},"
        "\"nonce\":\"1\""
        [cite_start]"}", (int)getpid(), esc_details, esc_state, timestamp_block); [cite: 132]

#ifdef WIN32
    EnterCriticalSection(&payload_mutex);
#else
    [cite_start]pthread_mutex_lock(&payload_mutex); [cite: 133]
#endif
    [cite_start]strncpy(pending_payload, payload, sizeof(pending_payload)); [cite: 133]
    [cite_start]pending_update = 1; [cite: 133]
#ifdef WIN32
    LeaveCriticalSection(&payload_mutex);
#else
    [cite_start]pthread_mutex_unlock(&payload_mutex); [cite: 134]
#endif
}

[cite_start]// Tick step entry engine callback hook [cite: 25]
void Discord_RunFrame(void) {
    [cite_start]if (!thread_active) Discord_Init(); [cite: 135]

    time_t current_time = time(NULL);
    int state_changed = 0;

    // Build user friendly IWAD name tags
    char friendly_iwad[64];
    if (cur_iwad_name) {
        if (strcasecmp(cur_iwad_name, "doom.wad") == 0) strcpy(friendly_iwad, "The Ultimate Doom");
        else if (strcasecmp(cur_iwad_name, "doom2.wad") == 0) strcpy(friendly_iwad, "Doom II");
        else if (strcasecmp(cur_iwad_name, "tnt.wad") == 0) strcpy(friendly_iwad, "Final Doom: TNT");
        else if (strcasecmp(cur_iwad_name, "plutonia.wad") == 0) strcpy(friendly_iwad, "Final Doom: Plutonia");
        else strcpy(friendly_iwad, cur_iwad_name);
    } else {
        strcpy(friendly_iwad, "Doom Engine");
    }

    // Process State Changes
    if (gamestate != last_gamestate) {
        state_changed = 1;
        last_gamestate = gamestate;
        if (gamestate == GS_LEVEL) {
            start_time = current_time; // Reset match timer upon level start
        } else if (gamestate >= GS_LEVEL_MENU) {
            start_time = 0; // Clear timer when in menus
        }
    }

    if (gamestate == GS_LEVEL) {
        // Evaluate changing active telemetry values
        if (strcmp(level.mapname, last_map) != 0 || 
            players[consoleplayer].health != last_health ||
            strcmp(players[consoleplayer].ready_weapon_name, last_weapon) != 0) {
            
            state_changed = 1;
            strncpy(last_map, level.mapname, sizeof(last_map));
            last_health = players[consoleplayer].health;
            strncpy(last_weapon, players[consoleplayer].ready_weapon_name, sizeof(last_weapon));
        }
    }

    [cite_start]// Enforce throttling to shield application from flood limitations [cite: 7, 10]
    if (state_changed) {
        if (current_time >= next_allowed_update_time) {
            [cite_start]next_allowed_update_time = current_time + THROTTLE_TIME_SECONDS; [cite: 10, 125]
            
            char details_str[128] = "";
            char state_str[128] = "";

            if (gamestate >= GS_LEVEL_MENU) {
                [cite_start]snprintf(details_str, sizeof(details_str), "Main Menu"); [cite: 127]
                snprintf(state_str, sizeof(state_str), "Browsing Options (%s)", friendly_iwad);
            } else if (gamestate == GS_INTERMISSION) {
                snprintf(details_str, sizeof(details_str), "Intermission Screen");
                snprintf(state_str, sizeof(state_str), "Reviewing stats for %s", level.mapname);
            } else if (gamestate == GS_LEVEL) {
                snprintf(details_str, sizeof(details_str), "Playing: %s (%s)", level.level_name, level.mapname);
                snprintf(state_str, sizeof(state_str), "HP: %d%% | %s | %s", 
                         last_health, last_weapon, friendly_iwad);
            }

            Discord_UpdatePresence(details_str, state_str);
        }
    }
}

```

---

## 3. Implementation Guide: `uzdoom_discord_guide.md`

```markdown
# Integrating Native Discord Rich Presence into uzdoom

This manual explains how to step-by-step incorporate the native socket-based custom Discord RPC pipeline inside your customized Doom engine (`id Tech 1` source branch variants like `uzdoom` / `GZDoom`).

---

## Technical Concept Strategy
Unlike modern heavy extensions, this approach creates zero runtime thread blocks or static link prerequisites on compiled builds. The connection operates asynchronously on a separate system execution sequence, maintaining optimal framerates.

---

## Step 1: File Deployment
Add the created `discord_rpc_doom.h` and `discord_rpc_doom.c` files directly to your internal engine source folder workspace directory:
`src/components/discord/`

---

## Step 2: Hooking into the Ticker Frame Loop
Doom games operate using a state ticker loop engine. The hook must run every single frame update.

Open the main loop frame step runner, which resides inside either `src/g_game.c` or `src/d_main.c` depending on how structured your source branch is (`G_Ticker()` or `D_DoomLoop()`).

```c
#include "components/discord/discord_rpc_doom.h"

void G_Ticker (void)
{
    // ... standard doom engine ticker handling routines loop ...
    
    // Core Engine Hook Injection Step
    Discord_RunFrame();
}

```

---

## Step 3: Map Global State Variables

Examine the variables assigned inside your engine data context matching the template properties:

1. **Active Map Identifiers**: Replace standard text extraction mappings with the real structural properties: `level.mapname` (e.g., *E1M1*) and `level.level_name` (*Hangar*).
2. **Player States**: Query properties via console index trackers:
* **Health**: `players[consoleplayer].health`
* **Weapon Identifier Strings**: Dynamically target internal weapon structures. In complex `GZDoom` setups, target via:
`players[consoleplayer].ReadyWeapon->GetClass()->TypeName.GetChars()`



---

## Step 4: Hooking Engine Lifetime Cycles

Ensure the socket states safely start and tear down alongside engine lifespans to prevent detached rogue processes.

Inside your root main driver lifecycle module (`D_DoomMain` or `main` entry structure):

```c
void D_DoomMain (void)
{
    // At launch sequence initialization
    Discord_Init();
    
    // ... engine execution run cycle loops ...
    
    // At engine closure context
    Discord_Shutdown();
}

```

---

## Step 5: Application Dashboard Setup
Client ID. 1513053682747572235

1. Go to the [Discord Developer Portal](https://www.google.com/search?q=https://discord.com/developers/applications).
2. Create an Application, and replace `YOUR_DISCORD_APP_ID_HERE` inside `discord_rpc_doom.c` with the Application Client ID.
3. Head over to **Rich Presence -> Art Assets** and register a large image asset placeholder key named `doom_main`.

```

```