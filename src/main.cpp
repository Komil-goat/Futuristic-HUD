#include <iostream>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "SystemMonitor.h"

static void GlfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << '\n';
}

class App {
public:
    App() = default;
    ~App() { Shutdown(); }

    bool Init();
    void Run();

private:
    void Shutdown();
    void NewFrame();
    void Render();
    void RenderUI();

    void SetupImGuiStyle();

private:
    GLFWwindow* m_window = nullptr;
    bool m_running = false;

    SystemMonitor m_monitor;
    std::string m_procFilter;
    char m_procFilterBuf[128]{};

    // UI state
    std::string m_lastError;
};

bool App::Init() {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    // Transparent HUD-style window
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(1280, 720, "Futuristic Hardware HUD", nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetupImGuiStyle();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    m_running = true;
    return true;
}

void App::SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.WindowBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    ImVec4 bg = ImVec4(0.08f, 0.08f, 0.10f, 0.80f); // semi-transparent
    ImVec4 accent = ImVec4(0.0f, 0.65f, 1.0f, 1.0f); // neon blue

    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.05f, 0.07f, 0.85f);
    colors[ImGuiCol_Border] = accent;

    colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.90f);
    colors[ImGuiCol_TitleBgActive] = accent;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.51f);

    colors[ImGuiCol_Header] = ImVec4(0.14f, 0.14f, 0.18f, 0.75f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.18f, 0.22f, 0.85f);
    colors[ImGuiCol_HeaderActive] = accent;

    colors[ImGuiCol_Button] = ImVec4(0.12f, 0.12f, 0.18f, 0.85f);
    colors[ImGuiCol_ButtonHovered] = accent;
    colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.5f, 0.9f, 1.0f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.16f, 0.80f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.24f, 0.85f);
    colors[ImGuiCol_FrameBgActive] = accent;

    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.8f, 1.0f, 1.0f);

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.94f, 0.98f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);
}

void App::Run() {
    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(m_window, 1);
        }
        m_monitor.Update();
        NewFrame();
        RenderUI();
        Render();
    }
}

void App::Shutdown() {
    if (!m_window) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = nullptr;
    m_running = false;
}

void App::NewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void App::Render() {
    ImGui::Render();

    int display_w, display_h;
    glfwGetFramebufferSize(m_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

void App::RenderUI() {
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoTitleBar;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGui::Begin("Futuristic HUD", nullptr, flags);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Hardware")) {
            HardwareStats stats = m_monitor.GetHardwareStats();
            const std::vector<float>& hist = m_monitor.GetCpuHistory();

            ImGui::Text("CPU Load: %.1f%%", stats.cpuLoadPercent);
            if (!hist.empty()) {
                ImGui::PlotLines("CPU History", hist.data(),
                                 static_cast<int>(hist.size()),
                                 0, nullptr, 0.0f, 100.0f, ImVec2(0, 120));
            }

            ImGui::Separator();
            ImGui::Text("RAM: %.2f / %.2f GB",
                        stats.ramUsedGB, stats.ramTotalGB);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Processes")) {
            ImGui::Text("Process Manager");
            ImGui::InputTextWithHint("##filter", "Search by name or PID",
                                     m_procFilterBuf, sizeof(m_procFilterBuf));
            m_procFilter = m_procFilterBuf;

            auto procs = m_monitor.GetProcesses(m_procFilter);
            ImGui::Text("Total: %zu", procs.size());
            ImGui::Separator();

            ImGui::BeginChild("ProcList", ImVec2(0, 0), true);
            for (const auto& p : procs) {
                ImGui::PushID(p.pid);
                ImGui::Text("%d  %s", p.pid, p.name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Terminate")) {
                    std::string err;
                    if (!m_monitor.TerminateProcess(p.pid, err)) {
                        m_lastError = "Failed to terminate PID " + std::to_string(p.pid) + ": " + err;
                    } else {
                        m_lastError = "Sent terminate to PID " + std::to_string(p.pid);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (!m_lastError.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", m_lastError.c_str());
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Weather")) {
            ImGui::Text("Weather - Tashkent (Open-Meteo)");
            if (ImGui::Button("Refresh")) {
                m_monitor.RequestWeatherRefresh();
            }

            if (m_monitor.IsWeatherLoading()) {
                ImGui::Text("Loading...");
            } else {
                auto w = m_monitor.GetWeather();
                if (w.has_value()) {
                    ImGui::Text("Summary: %s", w->summary.c_str());
                    ImGui::Text("Temperature: %.1f C", w->temperatureC);
                    ImGui::Text("Wind: %.1f km/h", w->windKph);
                } else {
                    ImGui::Text("No data (yet).");
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

int main() {
    App app;
    if (!app.Init()) {
        return 1;
    }
    app.Run();
    return 0;
}
