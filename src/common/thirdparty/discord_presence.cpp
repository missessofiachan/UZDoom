#include "discord_presence.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <chrono>
#include <vector>
#include <cstring>
#include <cerrno>
#include <exception>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h> // Fixed: Swapped select.h for poll.h to prevent FD_SET stack smashing
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

#ifndef _WIN32
static bool WriteAll(int fd, const void* buf, size_t count) {
    const char* ptr = static_cast<const char*>(buf);
    while (count > 0) {
        ssize_t bytes_written = write(fd, ptr, count);
        if (bytes_written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += bytes_written;
        count -= bytes_written;
    }
    return true;
}

static bool ReadAll(int fd, void* buf, size_t count) {
    char* ptr = static_cast<char*>(buf);
    while (count > 0) {
        ssize_t bytes_read = read(fd, ptr, count);
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (bytes_read == 0) return false;
        ptr += bytes_read;
        count -= bytes_read;
    }
    return true;
}
#endif

// Exception for safe error propagation
class DiscordException : public std::runtime_error {
public:
    explicit DiscordException(const std::string& msg) : std::runtime_error(msg) {}
};

class DiscordRpcClient {
private:
    std::thread mThread;
    std::mutex mMutex;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mConnected{false};
    std::atomic<int> mConsecutiveFailures{0};
    std::atomic<bool> mDisabledDueToErrors{false};

    std::string mAppId;
    std::string mState;
    std::string mDetails;
    std::string mLargeImage = "game-image";
    std::string mLargeText = "UZDoom";
    int64_t mStartTime = 0;
    bool mNeedUpdate = false;

    static constexpr int MAX_CONSECUTIVE_FAILURES = 10;
    static constexpr int SOCKET_TIMEOUT_MS = 2000;  // 2 second timeout for socket ops

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
        try {
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

                    // Set non-blocking mode
                    int flags = fcntl(fd, F_GETFL, 0);
                    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                        close(fd);
                        continue;
                    }

                    struct sockaddr_un addr;
                    memset(&addr, 0, sizeof(addr));
                    addr.sun_family = AF_UNIX;
                    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

                    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 || errno == EINPROGRESS) {
                        // Fixed: Using poll() instead of select()
                        struct pollfd pfd;
                        pfd.fd = fd;
                        pfd.events = POLLOUT;
                        
                        if (poll(&pfd, 1, SOCKET_TIMEOUT_MS) > 0) {
                            int err = 0;
                            socklen_t errlen = sizeof(err);
                            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == 0 && err == 0) {
                                mSocket = fd;
                                return true;
                            }
                        }
                    }
                    close(fd);
                }
            }
#endif
        } catch (...) {
            // Silently catch any exceptions during connection
        }
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
        try {
            uint32_t header[2];
            header[0] = opcode;
            header[1] = (uint32_t)json.size();

#ifdef _WIN32
            DWORD written;
            if (!WriteFile(mPipe, header, sizeof(header), &written, NULL) || written != sizeof(header)) {
                throw DiscordException("Failed to write header");
            }
            if (!WriteFile(mPipe, json.data(), (DWORD)json.size(), &written, NULL) || written != json.size()) {
                throw DiscordException("Failed to write payload");
            }
#else
            if (!WriteAll(mSocket, header, sizeof(header))) {
                throw DiscordException("Failed to write header");
            }
            if (!WriteAll(mSocket, json.data(), json.size())) {
                throw DiscordException("Failed to write payload");
            }
#endif

            // Read response with timeout
            uint32_t respHeader[2];
#ifdef _WIN32
            DWORD readBytes;
            if (ReadFile(mPipe, respHeader, sizeof(respHeader), &readBytes, NULL) && readBytes == sizeof(respHeader)) {
                if (respHeader[1] > 0 && respHeader[1] < 1024 * 1024) {  // Sanity check: max 1MB response
                    std::vector<char> buf(respHeader[1]);
                    ReadFile(mPipe, buf.data(), respHeader[1], &readBytes, NULL);
                }
            }
#else
            // Fixed: Using poll() instead of select() to prevent FD stack smashing
            struct pollfd pfd;
            pfd.fd = mSocket;
            pfd.events = POLLIN;
            
            if (poll(&pfd, 1, SOCKET_TIMEOUT_MS) <= 0) {
                throw DiscordException("Read timeout");
            }
            
            if (ReadAll(mSocket, respHeader, sizeof(respHeader))) {
                if (respHeader[1] > 0 && respHeader[1] < 1024 * 1024) {  // Sanity check
                    std::vector<char> buf(respHeader[1]);
                    ReadAll(mSocket, buf.data(), respHeader[1]);
                }
            } else {
                throw DiscordException("Failed to read response");
            }
#endif
            return true;
        } catch (const DiscordException&) {
            return false;
        } catch (const std::exception&) {
            return false;
        } catch (...) {
            return false;
        }
    }

    void ThreadFunc() {
        while (mRunning) {
            try {
                if (mDisabledDueToErrors) {
                    // Fixed: Replaced monolithic 30s block with interruptible chunked sleeps 
                    // This prevents the game from hanging when quitting.
                    for (int i = 0; i < 60 && mRunning; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    if (!mRunning) break; // Break loop early if game is shutting down

                    if (mConsecutiveFailures > 0) {
                        mConsecutiveFailures--;
                    }
                    if (mConsecutiveFailures < MAX_CONSECUTIVE_FAILURES / 2) {
                        mDisabledDueToErrors = false;
                    }
                    continue;
                }

                if (!mConnected) {
                    if (Connect()) {
                        try {
                            std::string handshake = "{\"v\":1,\"client_id\":\"" + mAppId + "\"}";
                            if (SendPayload(0, handshake)) {
                                mConnected = true;
                                mNeedUpdate = true;
                                mConsecutiveFailures = 0;
                            } else {
                                Disconnect();
                                mConsecutiveFailures++;
                            }
                        } catch (...) {
                            Disconnect();
                            mConsecutiveFailures++;
                        }
                    } else {
                        mConsecutiveFailures++;
                    }
                    
                    // Disable Discord if too many failures occur
                    if (mConsecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                        mDisabledDueToErrors = true;
                        Disconnect();
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
                        try {
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
                                mConsecutiveFailures++;
                            } else {
                                mConsecutiveFailures = 0;
                            }
                        } catch (...) {
                            Disconnect();
                            mConsecutiveFailures++;
                        }
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (const std::exception&) {
                // Catch any unexpected exceptions to prevent thread crashes
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (...) {
                // Catch absolutely everything
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        Disconnect();
    }

public:
    ~DiscordRpcClient() {
        Stop();
    }

    void Start(const std::string& appId) {
        try {
            if (mRunning) return;
            mAppId = appId;
            mRunning = true;
            mDisabledDueToErrors = false;
            mConsecutiveFailures = 0;
            mStartTime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            mThread = std::thread(&DiscordRpcClient::ThreadFunc, this);
        } catch (...) {
            // Silently fail if thread creation fails
            mRunning = false;
        }
    }

    void Stop() {
        try {
            if (!mRunning) return;
            mRunning = false;
            if (mThread.joinable()) {
                mThread.join();
            }
        } catch (...) {
            // Silently catch any thread cleanup errors
        }
    }

    void Update(const std::string& details, const std::string& state, const std::string& largeImage, const std::string& largeText) {
        try {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mDetails != details || mState != state || mLargeImage != largeImage || mLargeText != largeText) {
                mDetails = details;
                mState = state;
                mLargeImage = largeImage;
                mLargeText = largeText;
                mNeedUpdate = true;
            }
        } catch (...) {
            // Silently catch any mutex errors
        }
    }

    std::string GetAppId() const {
        return mAppId;
    }
};

static DiscordRpcClient gDiscordClient;

static std::string getWeaponEmoji(const std::string& weapon) {
    std::string lower = weapon;
    for (char &c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower.find("fist") != std::string::npos) return "👊";
    if (lower.find("saw") != std::string::npos || lower.find("chainsaw") != std::string::npos) return "🪚";
    if (lower.find("pistol") != std::string::npos) return "🔫";
    if (lower.find("shotgun") != std::string::npos) return "💥";
    if (lower.find("chaingun") != std::string::npos || lower.find("minigun") != std::string::npos) return "🔥";
    if (lower.find("rocket") != std::string::npos || lower.find("missile") != std::string::npos) return "🚀";
    if (lower.find("plasma") != std::string::npos) return "⚡";
    if (lower.find("bfg") != std::string::npos) return "🟢";
    return "🔫";
}

void I_TickDiscordPresence() {
    try {
        if (!i_discordrpc) {
            gDiscordClient.Stop();
            return;
        }

        static auto lastUpdateTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        bool forceUpdate = gDiscordClient.GetAppId().empty();
        if (!forceUpdate && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdateTime).count() < 1000) {
            return;
        }
        lastUpdateTime = now;

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

            // Fixed: Strict bounds check added for consoleplayer
            std::string weaponName = "Fists";
            int health = 0;
            
            if (consoleplayer >= 0 && consoleplayer < MAXPLAYERS) {
                health = std::max(0, players[consoleplayer].health);
                if (players[consoleplayer].ReadyWeapon) {
                    weaponName = players[consoleplayer].ReadyWeapon->GetTag();
                }
            }

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
    } catch (const std::exception& e) {
        // Catch any exceptions to prevent game crash
    } catch (...) {
        // Catch absolutely everything to keep game stable
    }
}

void I_ShutdownDiscordPresence() {
    try {
        gDiscordClient.Stop();
    } catch (...) {
        // Silently catch any shutdown errors
    }
}