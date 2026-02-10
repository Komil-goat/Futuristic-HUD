#include "SystemMonitor.h"

#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <signal.h>
#include <dirent.h>
#include <fstream>
#else
#include <sys/sysinfo.h>
#include <signal.h>
#include <dirent.h>
#include <fstream>
#endif

using json = nlohmann::json;

// --- Utility: curl write callback ---
namespace {
size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

SystemMonitor::SystemMonitor() {
    m_cpuHistory.reserve(MaxHistory);
#ifdef _WIN32
    // Prime CPU timing info
    SampleCpuUsage();
#else
    SampleCpuUsage();
#endif
    // Start background weather worker
    m_weatherThread = std::thread(&SystemMonitor::WeatherWorker, this);
}

SystemMonitor::~SystemMonitor() {
    m_weatherThreadStop.store(true);
    // Wake worker if it's idle by toggling flag
    if (m_weatherThread.joinable()) {
        m_weatherThread.join();
    }
}

void SystemMonitor::Update() {
    UpdateHardware();

    // Refresh process list at a lighter rate if desired
    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        m_processesCache = QueryProcesses();
    }
}

HardwareStats SystemMonitor::GetHardwareStats() const {
    std::lock_guard<std::mutex> lock(m_hwMutex);
    return m_hwStats;
}

std::vector<ProcessInfo> SystemMonitor::GetProcesses(const std::string& filter) const {
    std::vector<ProcessInfo> result;
    std::string filterLower = toLower(filter);

    std::lock_guard<std::mutex> lock(m_procMutex);
    for (const auto& p : m_processesCache) {
        if (filterLower.empty() ||
            toLower(p.name).find(filterLower) != std::string::npos ||
            std::to_string(p.pid).find(filterLower) != std::string::npos) {
            result.push_back(p);
        }
    }
    return result;
}

bool SystemMonitor::TerminateProcess(int pid, std::string& errorMessage) {
#ifdef _WIN32
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!hProc) {
        errorMessage = "OpenProcess failed.";
        return false;
    }
    BOOL ok = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    if (!ok) {
        errorMessage = "TerminateProcess failed.";
        return false;
    }
    return true;
#else
    if (kill(pid, SIGTERM) != 0) {
        errorMessage = std::string("kill(SIGTERM) failed: ") + std::strerror(errno);
        return false;
    }
    return true;
#endif
}

void SystemMonitor::RequestWeatherRefresh() {
    // Signal worker to perform a fetch
    if (!m_weatherLoading.exchange(true)) {
        // Newly requested, worker will do the job
    }
}

std::optional<WeatherInfo> SystemMonitor::GetWeather() const {
    std::lock_guard<std::mutex> lock(m_weatherMutex);
    return m_weather;
}

void SystemMonitor::UpdateHardware() {
    float cpu = SampleCpuUsage(); // 0..100
    HardwareStats stats;
    stats.cpuLoadPercent = cpu;
    SampleRamUsage(stats);

    {
        std::lock_guard<std::mutex> lock(m_hwMutex);
        m_hwStats = stats;
        if (m_cpuHistory.size() >= MaxHistory) {
            m_cpuHistory.erase(m_cpuHistory.begin());
        }
        m_cpuHistory.push_back(cpu);
    }
}

// --- CPU / RAM sampling ---

float SystemMonitor::SampleCpuUsage() {
#ifdef _WIN32
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return 0.0f;
    }
    auto filetimeToULL = [](const FILETIME& ft) -> unsigned long long {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return li.QuadPart;
    };

    unsigned long long idle = filetimeToULL(idleTime);
    unsigned long long kernel = filetimeToULL(kernelTime);
    unsigned long long user = filetimeToULL(userTime);

    unsigned long long idleDiff = idle - m_lastIdleTime;
    unsigned long long kernelDiff = kernel - m_lastKernelTime;
    unsigned long long userDiff = user - m_lastUserTime;
    unsigned long long total = kernelDiff + userDiff;

    m_lastIdleTime = idle;
    m_lastKernelTime = kernel;
    m_lastUserTime = user;

    if (total == 0) return 0.0f;
    float usage = 100.0f * (1.0f - (float)idleDiff / (float)total);
    return usage;
#elif defined(__APPLE__)
    // macOS: approximate CPU usage using load average vs. CPU count
    double load = 0.0;
    if (getloadavg(&load, 1) != 1) {
        return 0.0f;
    }

    int ncpu = 0;
    size_t len = sizeof(ncpu);
    if (sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0) != 0 || ncpu <= 0) {
        ncpu = 1;
    }

    float usage = static_cast<float>(std::min(load / static_cast<double>(ncpu), 1.0) * 100.0);
    return usage;
#else
    // Linux /proc/stat
    std::ifstream stat("/proc/stat");
    if (!stat) return 0.0f;
    std::string cpuLabel;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    stat >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    if (cpuLabel != "cpu") return 0.0f;

    unsigned long long idleAll = idle + iowait;
    unsigned long long nonIdle = user + nice + system + irq + softirq + steal;
    unsigned long long total = idleAll + nonIdle;

    unsigned long long totalDiff = total - m_lastTotalJiffies;
    unsigned long long idleDiff = idleAll - m_lastIdleJiffies;

    m_lastTotalJiffies = total;
    m_lastIdleJiffies = idleAll;

    if (totalDiff == 0) return 0.0f;
    float usage = 100.0f * (float)(totalDiff - idleDiff) / (float)totalDiff;
    return usage;
#endif
}

void SystemMonitor::SampleRamUsage(HardwareStats& stats) const {
#ifdef _WIN32
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        double total = static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        double used = total - static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        stats.ramTotalGB = static_cast<float>(total);
        stats.ramUsedGB = static_cast<float>(used);
    }
#elif defined(__APPLE__)
    // macOS: use vm statistics and page size
    int64_t pageSize = 0;
    size_t len = sizeof(pageSize);
    if (sysctlbyname("hw.pagesize", &pageSize, &len, nullptr, 0) != 0 || pageSize <= 0) {
        return;
    }

    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmStat{};
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vmStat), &count) != KERN_SUCCESS) {
        return;
    }

    uint64_t totalPages = vmStat.active_count + vmStat.inactive_count +
                          vmStat.wire_count + vmStat.free_count;
    uint64_t usedPages = vmStat.active_count + vmStat.inactive_count + vmStat.wire_count;

    double total = static_cast<double>(totalPages) * static_cast<double>(pageSize) /
                   (1024.0 * 1024.0 * 1024.0);
    double used = static_cast<double>(usedPages) * static_cast<double>(pageSize) /
                  (1024.0 * 1024.0 * 1024.0);

    stats.ramTotalGB = static_cast<float>(total);
    stats.ramUsedGB = static_cast<float>(used);
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        double total = static_cast<double>(info.totalram) * info.mem_unit / (1024.0 * 1024.0 * 1024.0);
        double free = static_cast<double>(info.freeram) * info.mem_unit / (1024.0 * 1024.0 * 1024.0);
        stats.ramTotalGB = static_cast<float>(total);
        stats.ramUsedGB = static_cast<float>(total - free);
    }
#endif
}

// --- Process enumeration ---

std::vector<ProcessInfo> SystemMonitor::QueryProcesses() const {
    std::vector<ProcessInfo> procs;
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return procs;

    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Process32First(snap, &entry)) {
        do {
            ProcessInfo p;
            p.pid = static_cast<int>(entry.th32ProcessID);
            p.name = entry.szExeFile;
            procs.push_back(std::move(p));
        } while (Process32Next(snap, &entry));
    }
    CloseHandle(snap);
#else
    // POSIX (macOS/Linux): use 'ps' to enumerate processes
    FILE* pipe = popen("ps -axo pid=,comm=", "r");
    if (!pipe) {
        return procs;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::istringstream iss(buffer);
        int pid = 0;
        std::string name;
        if (!(iss >> pid))
            continue;
        std::getline(iss, name);
        // Trim leading spaces
        name.erase(name.begin(),
                   std::find_if(name.begin(), name.end(),
                                [](unsigned char c) { return !std::isspace(c); }));
        if (name.empty()) name = "unknown";

        ProcessInfo p;
        p.pid = pid;
        p.name = name;
        procs.push_back(std::move(p));
    }
    pclose(pipe);
#endif
    return procs;
}

// --- Weather ---

void SystemMonitor::WeatherWorker() {
    while (!m_weatherThreadStop.load()) {
        if (m_weatherLoading.load()) {
            FetchWeatherBlocking();
            m_weatherLoading.store(false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void SystemMonitor::FetchWeatherBlocking() {
    // Hard-coded city (Berlin for example)
    const char* url =
        "https://api.open-meteo.com/v1/forecast?latitude=41.29&longitude=69.23&current_weather=true";

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::lock_guard<std::mutex> lock(m_weatherMutex);
        m_weather.reset();
        return;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::lock_guard<std::mutex> lock(m_weatherMutex);
        m_weather.reset();
        return;
    }

    try {
        auto j = json::parse(response);
        if (!j.contains("current_weather")) {
            std::lock_guard<std::mutex> lock(m_weatherMutex);
            m_weather.reset();
            return;
        }

        auto cw = j["current_weather"];
        WeatherInfo info;
        info.temperatureC = cw.value("temperature", 0.0);
        info.windKph = cw.value("windspeed", 0.0);
        int code = cw.value("weathercode", 0);
        info.summary = "Code " + std::to_string(code);
        info.lastUpdated = std::chrono::system_clock::now();

        std::lock_guard<std::mutex> lock(m_weatherMutex);
        m_weather = info;
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_weatherMutex);
        m_weather.reset();
    }
}