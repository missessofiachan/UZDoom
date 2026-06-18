#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Get the directory of this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Color codes for pretty output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 1. Parse arguments
CLEAN_BUILD=false
RUN_AFTER_BUILD=false
RUN_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -r|--run)
            RUN_AFTER_BUILD=true
            shift
            # Collect all remaining arguments to pass to the game binary
            while [[ $# -gt 0 ]]; do
                RUN_ARGS+=("$1")
                shift
            done
            ;;
        -h|--help)
            echo "Usage: $0 [options] [--game-args...]"
            echo ""
            echo "Options:"
            echo "  -c, --clean     Perform a clean build by deleting the build/ directory first."
            echo "  -r, --run       Run UZDoom automatically after a successful compilation."
            echo "                  All subsequent arguments are forwarded directly to the game."
            echo "  -h, --help      Display this help menu."
            echo ""
            echo "Examples:"
            echo "  $0 -c"
            echo "  $0 -r -iwad doom2.wad -file custom.wad"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use -h or --help for usage information."
            exit 1
            ;;
    esac
done

echo -e "${BLUE}=== UZDoom Build Script ===${NC}"

if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}Clean build requested. Removing build/ directory...${NC}"
    rm -rf build
fi

# 2. Detect container environment vs host
INSIDE_CONTAINER=false
if [ -f /run/.containerenv ] || [ -f /run/.toolboxenv ] || [ "${container}" = "podman" ] || [ "${container}" = "docker" ]; then
    INSIDE_CONTAINER=true
fi

# Determine how to run commands (directly or via distrobox)
USE_DISTROBOX=false
if [ "$INSIDE_CONTAINER" = false ]; then
    if command -v distrobox &> /dev/null; then
        # Check if bazzite-dev container exists
        if distrobox list 2>/dev/null | grep -q "bazzite-dev"; then
            USE_DISTROBOX=true
        fi
    fi
fi

# Helper function to run commands in the correct context
run_build_cmd() {
    if [ "$USE_DISTROBOX" = true ]; then
        distrobox enter bazzite-dev -- "$@"
    else
        "$@"
    fi
}

if [ "$USE_DISTROBOX" = true ]; then
    echo -e "${BLUE}Running build commands inside 'bazzite-dev' distrobox container...${NC}"
else
    if [ "$INSIDE_CONTAINER" = true ]; then
        echo -e "${BLUE}Running build commands directly inside container...${NC}"
    else
        echo -e "${YELLOW}Warning: Distrobox 'bazzite-dev' not found/not installed. Compiling on host...${NC}"
    fi
fi

# Determine parallel build job factor
if [ -n "$JOBS" ]; then
    NUM_JOBS="$JOBS"
elif command -v nproc &>/dev/null; then
    NUM_JOBS=$(nproc)
elif command -v sysctl &>/dev/null; then
    NUM_JOBS=$(sysctl -n hw.ncpu)
else
    NUM_JOBS=2
fi

# 3. Automatic CMake Reconfiguration Check
# If CMakeLists.txt or cmake files are newer than build/CMakeCache.txt, reconfigure.
NEEDS_CONFIGURE=false
if [ ! -d "build" ] || [ ! -f "build/CMakeCache.txt" ]; then
    NEEDS_CONFIGURE=true
else
    # Find files newer than build/CMakeCache.txt
    NEWER_FILES=$(find CMakeLists.txt src/CMakeLists.txt cmake/ libraries/ -type f -name "CMakeLists.txt" -newer build/CMakeCache.txt 2>/dev/null || true)
    if [ -n "$NEWER_FILES" ]; then
        echo -e "${YELLOW}Detected updates to CMake configuration files:${NC}"
        echo "$NEWER_FILES" | sed 's/^/ - /'
        NEEDS_CONFIGURE=true
    fi
fi

if [ "$NEEDS_CONFIGURE" = true ]; then
    echo -e "${YELLOW}Configuring build with CMake...${NC}"
    run_build_cmd cmake -B build -DCMAKE_BUILD_TYPE=Release
else
    echo -e "${GREEN}Build already configured and up-to-date. Skipping CMake configuration...${NC}"
fi

# Start build timer
START_TIME=$(date +%s)

# 4. Build the project
echo -e "${GREEN}Compiling UZDoom with ${NUM_JOBS} parallel jobs...${NC}"
if ! run_build_cmd cmake --build build -j"${NUM_JOBS}"; then
    echo -e "${RED}Build failed! If you encountered configuration issues, try a clean build:${NC}"
    echo -e "${YELLOW}  ./compile.sh --clean${NC}"
    exit 1
fi

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo -e "${GREEN}Build complete in ${ELAPSED}s! Executable is located at:${NC}"
echo -e "${BLUE}  build/uzdoom${NC}"

# 5. Optional Auto-Run
if [ "$RUN_AFTER_BUILD" = true ]; then
    echo -e "${GREEN}Launching UZDoom with arguments: ${RUN_ARGS[*]}...${NC}"
    run_build_cmd ./build/uzdoom "${RUN_ARGS[@]}"
fi

