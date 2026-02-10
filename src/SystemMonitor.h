#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <optional>
#include <chrono>

struct ProcessInfo {
    int pid;
    std::string name;
};

struct HardwareStats {
    float cpuLoadPercent = 0.0f;
    float ramUsedGB = 0.0f;
    float ramTotalGB = 0.0f;
};

struct WeatherInfo {
    std::string summary;
    double temperatureC = 0.0;
    double windKph = 0.0;
    std::chrono::system_clock::time_point lastUpdated{};
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    // Called each frame (or at some fixed interval)
    void Update();

    // Accessors (thread-safe where needed)
    HardwareStats GetHardwareStats() const;
    const std::vector<float>& GetCpuHistory() const { return m_cpuHistory; }

    std::vector<ProcessInfo> GetProcesses(const std::string& filter) const;

    // Returns true on success, false on error
    bool TerminateProcess(int pid, std::string& errorMessage);

    // Weather: trigger async refresh
    void RequestWeatherRefresh();
    bool IsWeatherLoading() const { return m_weatherLoading.load(); }
    std::optional<WeatherInfo> GetWeather() const;

private:
    // Hardware
    void UpdateHardware();

    // Processes (platform-specific)
    std::vector<ProcessInfo> QueryProcesses() const;

    // Weather
    void WeatherWorker();
    void FetchWeatherBlocking();

    // Helpers
    float SampleCpuUsage();
    void SampleRamUsage(HardwareStats& stats) const;

private:
    // Hardware data
    mutable std::mutex m_hwMutex;
    HardwareStats m_hwStats{};
    std::vector<float> m_cpuHistory; // 0..1 or 0..100 depending on how we interpret
    static constexpr size_t MaxHistory = 256;

    // CPU sampling state (platform-specific)
#ifdef _WIN32
    unsigned long long m_lastIdleTime = 0;
    unsigned long long m_lastKernelTime = 0;
    unsigned long long m_lastUserTime = 0;
#else
    unsigned long long m_lastTotalJiffies = 0;
    unsigned long long m_lastIdleJiffies = 0;
#endif

    // Weather data
    mutable std::mutex m_weatherMutex;
    std::optional<WeatherInfo> m_weather;
    std::atomic<bool> m_weatherLoading{false};
    std::thread m_weatherThread;
    std::atomic<bool> m_weatherThreadStop{false};

    // Cache of processes (updated in Update())
    mutable std::mutex m_procMutex;
    std::vector<ProcessInfo> m_processesCache;
};