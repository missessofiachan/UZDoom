#include "discord_presence.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <chrono>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Engine headers needed for game state information
#include "d_player.h"
#include "g_levellocals.h"
#include "c_dispatch.h"
#include "version.h"
#include "common/filesystem/include/fs_filesystem.h"
#include "gi.h"
#include "startupinfo.h"

// External declarations we need
extern int consoleplayer;
extern player_t players[MAXPLAYERS];
EXTERN_CVAR(Bool, i_discordrpc)
EXTERN_CVAR(String, discord_appid)
EXTERN_CVAR(String, discord_largeimage)
EXTERN_CVAR(String, discord_largetext)
const char * G_SkillName();

// For Process ID
#ifdef _WIN32
#define getpid GetCurrentProcessId
#endif

class DiscordRpcClient {
private:
    std::thread mThread;
    std::mutex mMutex;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mConnected{false};

    std::string mAppId;
    std::string mState;
    std::string mDetails;
    std::string mLargeImage = "game-image";
    std::string mLargeText = "UZDoom";
    int64_t mStartTime = 0;
    bool mNeedUpdate = false;

#ifdef _WIN32
    HANDLE mPipe = INVALID_HANDLE_VALUE;
#else
    int mSocket = -1;
#endif

    std::string EscapeJSON(const std::string& s) {
        std::string res;
        res.reserve(s.size());
        for (char c : s) {
            if (c == '\\') res += "\\\\";
            else if (c == '"') res += "\\\"";
            else if (c == '\n') res += "\\n";
            else if (c == '\r') res += "\\r";
            else if (c == '\t') res += "\\t";
            else if ((unsigned char)c < 32) {}
            else res += c;
        }
        return res;
    }

    bool Connect() {
#ifdef _WIN32
        for (int i = 0; i < 10; ++i) {
            std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
            mPipe = CreateFileA(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (mPipe != INVALID_HANDLE_VALUE) {
                return true;
            }
        }
#else
        const char* envs[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };
        std::vector<std::string> paths;
        for (const char* env : envs) {
            const char* val = getenv(env);
            if (val && val[0]) {
                paths.push_back(val);
            }
        }
        paths.push_back("/tmp");

        for (const auto& dir : paths) {
            for (int i = 0; i < 10; ++i) {
                std::string path = dir + "/discord-ipc-" + std::to_string(i);
                int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) continue;

                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

                if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                    mSocket = fd;
                    return true;
                }
                close(fd);
            }
        }
#endif
        return false;
    }

    void Disconnect() {
#ifdef _WIN32
        if (mPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(mPipe);
            mPipe = INVALID_HANDLE_VALUE;
        }
#else
        if (mSocket >= 0) {
            close(mSocket);
            mSocket = -1;
        }
#endif
        mConnected = false;
    }

    bool SendPayload(uint32_t opcode, const std::string& json) {
        uint32_t header[2];
        header[0] = opcode;
        header[1] = (uint32_t)json.size();

#ifdef _WIN32
        DWORD written;
        if (!WriteFile(mPipe, header, sizeof(header), &written, NULL) || written != sizeof(header)) {
            return false;
        }
        if (!WriteFile(mPipe, json.data(), (DWORD)json.size(), &written, NULL) || written != json.size()) {
            return false;
        }
#else
        if (write(mSocket, header, sizeof(header)) != sizeof(header)) {
            return false;
        }
        if (write(mSocket, json.data(), json.size()) != (ssize_t)json.size()) {
            return false;
        }
#endif
        // Read response header and payload to discard/acknowledge
        uint32_t respHeader[2];
#ifdef _WIN32
        DWORD readBytes;
        if (ReadFile(mPipe, respHeader, sizeof(respHeader), &readBytes, NULL) && readBytes == sizeof(respHeader)) {
            if (respHeader[1] > 0) {
                std::vector<char> buf(respHeader[1]);
                ReadFile(mPipe, buf.data(), respHeader[1], &readBytes, NULL);
            }
        }
#else
        if (read(mSocket, respHeader, sizeof(respHeader)) == sizeof(respHeader)) {
            if (respHeader[1] > 0) {
                std::vector<char> buf(respHeader[1]);
                ssize_t r = read(mSocket, buf.data(), respHeader[1]);
                (void)r;
            }
        }
#endif
        return true;
    }

    void ThreadFunc() {
        while (mRunning) {
            if (!mConnected) {
                if (Connect()) {
                    std::string handshake = "{\"v\":1,\"client_id\":\"" + mAppId + "\"}";
                    if (SendPayload(0, handshake)) {
                        mConnected = true;
                        mNeedUpdate = true;
                    } else {
                        Disconnect();
                    }
                }
            }

            if (mConnected) {
                std::string details, state, largeImage, largeText;
                bool doUpdate = false;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    if (mNeedUpdate) {
                        details = mDetails;
                        state = mState;
                        largeImage = mLargeImage;
                        largeText = mLargeText;
                        mNeedUpdate = false;
                        doUpdate = true;
                    }
                }

                if (doUpdate) {
                    std::string payload = "{\n"
                        "  \"cmd\": \"SET_ACTIVITY\",\n"
                        "  \"args\": {\n"
                        "    \"pid\": " + std::to_string(getpid()) + ",\n"
                        "    \"activity\": {\n"
                        "      \"details\": \"" + EscapeJSON(details) + "\",\n"
                        "      \"state\": \"" + EscapeJSON(state) + "\",\n"
                        "      \"timestamps\": {\n"
                        "        \"start\": " + std::to_string(mStartTime) + "\n"
                        "      },\n"
                        "      \"assets\": {\n"
                        "        \"large_image\": \"" + EscapeJSON(largeImage) + "\",\n"
                        "        \"large_text\": \"" + EscapeJSON(largeText) + "\"\n"
                        "      }\n"
                        "    }\n"
                        "  },\n"
                        "  \"nonce\": \"1\"\n"
                        "}";
                    if (!SendPayload(1, payload)) {
                        Disconnect();
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        Disconnect();
    }

public:
    void Start(const std::string& appId) {
        if (mRunning) return;
        mAppId = appId;
        mRunning = true;
        mStartTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        mThread = std::thread(&DiscordRpcClient::ThreadFunc, this);
    }

    void Stop() {
        if (!mRunning) return;
        mRunning = false;
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    void Update(const std::string& details, const std::string& state, const std::string& largeImage, const std::string& largeText) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mDetails != details || mState != state || mLargeImage != largeImage || mLargeText != largeText) {
            mDetails = details;
            mState = state;
            mLargeImage = largeImage;
            mLargeText = largeText;
            mNeedUpdate = true;
        }
    }

    std::string GetAppId() const {
        return mAppId;
    }
};

static DiscordRpcClient gDiscordClient;

static std::string getWeaponEmoji(const std::string& weapon) {
    std::string lower = weapon;
    for (char &c : lower) c = tolower(c);
    if (lower.find("fist") != std::string::npos) return "👊";
    if (lower.find("saw") != std::string::npos || lower.find("chainsaw") != std::string::npos) return "🪚";
    if (lower.find("pistol") != std::string::npos) return "🔫";
    if (lower.find("shotgun") != std::string::npos) return "🔫";
    if (lower.find("chaingun") != std::string::npos || lower.find("minigun") != std::string::npos) return "🔫";
    if (lower.find("rocket") != std::string::npos || lower.find("missile") != std::string::npos) return "🚀";
    if (lower.find("plasma") != std::string::npos) return "⚡";
    if (lower.find("bfg") != std::string::npos) return "🟢";
    return "🔫";
}

void I_TickDiscordPresence() {
    if (!i_discordrpc) {
        gDiscordClient.Stop();
        return;
    }

    const char* appId = DEFAULT_DISCORD_APP_ID;
    const char* cvarAppId = discord_appid;
    if (cvarAppId && cvarAppId[0] != '\0') {
        appId = cvarAppId;
    } else if (GameStartupInfo.DiscordAppId.IsNotEmpty()) {
        appId = GameStartupInfo.DiscordAppId.GetChars();
    }

    if (gDiscordClient.GetAppId() != appId) {
        gDiscordClient.Stop();
    }
    gDiscordClient.Start(appId);

    std::string details = "🎮 Main Menu";
    std::string state = "";
    std::string largeImage = "game-image";
    std::string largeText = "UZDoom";

    // Set large image and text based on active game/IWAD configuration or startup info name
    std::string gameName = "";
    if (GameStartupInfo.Name.IsNotEmpty()) {
        gameName = GameStartupInfo.Name.GetChars();
    } else if (gameinfo.ConfigName.IsNotEmpty()) {
        gameName = gameinfo.ConfigName.GetChars();
    }

    // Resolve Large Image
    const char* cvarLargeImage = discord_largeimage;
    if (cvarLargeImage && cvarLargeImage[0] != '\0') {
        largeImage = cvarLargeImage;
    } else if (GameStartupInfo.DiscordLargeImage.IsNotEmpty()) {
        largeImage = GameStartupInfo.DiscordLargeImage.GetChars();
    } else {
        // Determine base game asset key using the IWAD filename to prevent mods from breaking it
        std::string iwadFile = "";
        int iwadContainer = fileSystem.GetIwadNum();
        if (iwadContainer >= 0) {
            const char* resName = fileSystem.GetResourceFileName(iwadContainer);
            if (resName) {
                std::string fullPath(resName);
                size_t slash = fullPath.find_last_of("/\\");
                if (slash != std::string::npos) {
                    iwadFile = fullPath.substr(slash + 1);
                } else {
                    iwadFile = fullPath;
                }
                for (char &c : iwadFile) c = tolower(c);
            }
        }

        if (!iwadFile.empty()) {
            if (iwadFile.find("plutonia") != std::string::npos) {
                largeImage = "plutonia";
            } else if (iwadFile.find("tnt") != std::string::npos) {
                largeImage = "tnt";
            } else if (iwadFile.find("doom2") != std::string::npos) {
                largeImage = "doom2";
            } else if (iwadFile.find("doom") != std::string::npos) {
                largeImage = "doom";
            } else if (iwadFile.find("heretic") != std::string::npos) {
                largeImage = "heretic";
            } else if (iwadFile.find("hexen") != std::string::npos) {
                largeImage = "hexen";
            } else if (iwadFile.find("strife") != std::string::npos) {
                largeImage = "strife";
            } else if (iwadFile.find("chex") != std::string::npos) {
                largeImage = "chex";
            }
        } else if (!gameName.empty()) {
            // Fall back to config/startup name if IWAD name is somehow empty
            std::string lowerGame = gameName;
            for (char &c : lowerGame) c = tolower(c);
            
            if (lowerGame.find("plutonia") != std::string::npos) {
                largeImage = "plutonia";
            } else if (lowerGame.find("tnt") != std::string::npos) {
                largeImage = "tnt";
            } else if (lowerGame.find("doom 2") != std::string::npos || lowerGame.find("doom ii") != std::string::npos) {
                largeImage = "doom2";
            } else if (lowerGame.find("doom") != std::string::npos) {
                largeImage = "doom";
            } else if (lowerGame.find("heretic") != std::string::npos) {
                largeImage = "heretic";
            } else if (lowerGame.find("hexen") != std::string::npos) {
                largeImage = "hexen";
            } else if (lowerGame.find("strife") != std::string::npos) {
                largeImage = "strife";
            } else if (lowerGame.find("chex") != std::string::npos) {
                largeImage = "chex";
            }
        }
    }

    // Resolve Large Text
    const char* cvarLargeText = discord_largetext;
    if (cvarLargeText && cvarLargeText[0] != '\0') {
        largeText = cvarLargeText;
    } else if (GameStartupInfo.DiscordLargeText.IsNotEmpty()) {
        largeText = GameStartupInfo.DiscordLargeText.GetChars();
    } else {
        if (!gameName.empty()) {
            largeText = "UZDoom (" + gameName + ")";
        }
    }

    if (gamestate == GS_LEVEL && primaryLevel) {
        std::string mapName = primaryLevel->MapName.GetChars();
        std::string levelName = primaryLevel->LevelName.GetChars();
        const char* skillName = G_SkillName();
        std::string difficulty = skillName ? skillName : "Unknown";
        details = "🕹️ " + mapName + ": " + levelName + " (" + difficulty + ")";

        std::string weaponName = "Fists";
        if (players[consoleplayer].ReadyWeapon) {
            weaponName = players[consoleplayer].ReadyWeapon->GetTag();
        }

        int health = std::max(0, players[consoleplayer].health);
        int kills = primaryLevel->killed_monsters;
        int totalKills = primaryLevel->total_monsters;
        int secrets = primaryLevel->found_secrets;
        int totalSecrets = primaryLevel->total_secrets;

        std::string wadName = "";
        if (primaryLevel->lumpnum >= 0) {
            int container = fileSystem.GetFileContainer(primaryLevel->lumpnum);
            if (container >= 0) {
                const char* resName = fileSystem.GetResourceFileName(container);
                if (resName) {
                    std::string fullPath(resName);
                    size_t slash = fullPath.find_last_of("/\\");
                    if (slash != std::string::npos) {
                        wadName = fullPath.substr(slash + 1);
                    } else {
                        wadName = fullPath;
                    }
                }
            }
        }

        state = "❤️ Health: " + std::to_string(health) + "%  •  💀 Kills: " + std::to_string(kills) + "/" + std::to_string(totalKills) +
                "  •  🔍 Secrets: " + std::to_string(secrets) + "/" + std::to_string(totalSecrets) +
                "  •  " + getWeaponEmoji(weaponName) + " " + weaponName;
        if (!wadName.empty()) {
            state += "  •  💾 " + wadName;
        }
    } else {
        std::string wadName = "";
        int container = fileSystem.GetIwadNum();
        if (container >= 0) {
            const char* resName = fileSystem.GetResourceFileName(container);
            if (resName) {
                std::string fullPath(resName);
                size_t slash = fullPath.find_last_of("/\\");
                if (slash != std::string::npos) {
                    wadName = fullPath.substr(slash + 1);
                } else {
                    wadName = fullPath;
                }
            }
        }
        if (!wadName.empty()) {
            state = "💾 WAD: " + wadName;
        }
    }

    gDiscordClient.Update(details, state, largeImage, largeText);
}

void I_ShutdownDiscordPresence() {
    gDiscordClient.Stop();
}
