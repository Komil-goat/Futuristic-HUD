# Futuristic Hardware HUD

A futuristic, semi‑transparent hardware‑monitoring HUD for macOS, built with C++20, Dear ImGui, GLFW, and OpenGL 3.
It floats as a borderless overlay and shows live CPU load, RAM usage, process list with terminate, and a weather widget powered by Open‑Meteo.

---

## Features

### HUD‑style overlay

- Borderless, semi‑transparent window with neon‑blue accent theme
- Rounded corners, dark UI, designed for a “sci‑fi dashboard” look

### Hardware tab

- Live CPU usage with scrolling history plot
- RAM usage (used vs total, in GB)

### Process manager

- Searchable list by name or PID
- Terminate button per process (sends a safe terminate signal)

### Weather widget

- Fetches current weather for a hardcoded city via Open‑Meteo
- Non‑blocking network call using std::thread
- Shows temperature, wind speed, and a simple summary code

### Clean architecture

- App class encapsulates GLFW + ImGui init, frame loop, rendering, and shutdown
- SystemMonitor class handles all system data (hardware, processes, weather)
- UI code lives in a dedicated RenderUI() method

---

## macOS Prerequisites

You’ll need:

- macOS (tested on recent macOS with Apple Clang)
- Xcode Command Line Tools (for compilers and system headers)
- CMake (3.21+)
- curl / libcurl (via Xcode SDK or Homebrew)
- A reasonably recent Python 3 (glad uses it to generate OpenGL loader code)

### 1. Install Command Line Tools

```bash
xcode-select --install
```

If you already have Xcode or its tools installed, this may simply confirm.

### 2. Install CMake (and optionally curl) via Homebrew

If you don’t have Homebrew yet, install it from https://brew.sh, then:

```bash
brew install cmake
# curl usually comes with macOS; this is optional:
brew install curl
```

### 3. Fix Python SSL certificates (only once, if needed)

If during the first build you see a Python SSLCertVerificationError from glad, run:

```bash
/Applications/Python\ 3.13/Install\ Certificates.command
```

(or the equivalent for your Python version, e.g. Python 3.12), then rebuild.

---

## Building and Running (macOS)

Clone the repo and cd into it:

```bash
git clone <your-repo-url> FuturisticHUD
cd FuturisticHUD
```

Configure (FetchContent will download ImGui, GLFW, glad, nlohmann/json on first run):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Build:

```bash
cmake --build build -j
```

Run:

```bash
./build/futuristic_hud
```

On the very first configure+build, expect a few minutes while dependencies are fetched and glad generates its OpenGL loader.

---

## Controls & Usage

### Quit the app

- Press Esc while the HUD window is focused, or
- If run from Terminal: press Ctrl + C in the terminal

### Tabs

#### Hardware

- Shows current CPU load (%) and a scrolling history graph
- Shows used / total RAM in GB

#### Processes

- Type in the search box to filter by process name or PID
- Click Terminate to send a terminate signal to that process

#### Weather

- Click Refresh to fetch current weather data
- Shows temperature (°C), wind speed (km/h), and a simple status code

---

## Changing the Weather Location (City)

Weather is fetched from Open‑Meteo using hardcoded latitude/longitude.

Open src/SystemMonitor.cpp.

Find SystemMonitor::FetchWeatherBlocking() and this line:

```cpp
const char* url =
    "https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.41&current_weather=true";
```

Replace latitude and longitude with your city’s coordinates, for example:

```cpp
// Example: New York City
const char* url =
    "https://api.open-meteo.com/v1/forecast?latitude=40.71&longitude=-74.01&current_weather=true";
```

Optionally, update the UI label in src/main.cpp inside the Weather tab:

```cpp
ImGui::Text("Weather - New York (Open-Meteo)");
```

Rebuild:

```bash
cmake --build build
./build/futuristic_hud
```

---

## Project Structure

### CMakeLists.txt

Configures the project and fetches:

- Dear ImGui
- GLFW
- glad
- nlohmann/json

### src/main.cpp

Defines the App class:

- Init(), Run(), NewFrame(), Render(), Shutdown()
- RenderUI() builds the Dear ImGui interface and tabs
- Sets the custom HUD theme and transparent, borderless GLFW window

### src/SystemMonitor.h / src/SystemMonitor.cpp

SystemMonitor:

- Samples CPU usage and RAM usage (macOS and Windows/Linux paths)
- Enumerates running processes and provides a TerminateProcess API
- Fetches weather data in a background std::thread using libcurl and nlohmann::json

---

## Notes

- The project is intentionally self‑contained: all third‑party libraries are pulled via CMake FetchContent and not committed to the repo.
- On the first build, network access is required for CMake to download ImGui, GLFW, glad, and nlohmann/json.
- The CPU and memory numbers may not exactly match Activity Monitor, because macOS and Linux/Windows expose slightly different APIs; the implementation aims for a reasonable real‑time approximation.
- If you run into build issues on macOS, feel free to open an issue with your macOS version, CMake version, and full build log.
