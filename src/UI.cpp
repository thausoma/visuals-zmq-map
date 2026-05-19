#include "UI.h"
#include "TelemetryData.h"
#include "Database.h"
#include "Map.h"
#include "Heatmap.h"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <filesystem>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using namespace std;
namespace fs = std::filesystem;

void ui_loop() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow("Telemetry Monitor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    glewInit();

    ImGui::CreateContext();
    ImPlot::CreateContext();

    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* c = style.Colors;

        c[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
        c[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.52f, 0.54f, 1.00f);
        c[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
        c[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
        c[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
        c[ImGuiCol_Border]                 = ImVec4(0.20f, 0.24f, 0.30f, 0.50f);
        c[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.20f, 0.26f, 1.00f);
        c[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.25f, 0.32f, 1.00f);
        c[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.30f, 0.38f, 1.00f);
        c[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
        c[ImGuiCol_TitleBgActive]          = ImVec4(0.15f, 0.40f, 0.55f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.10f, 0.13f, 0.75f);
        c[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
        c[ImGuiCol_ScrollbarBg]            = ImVec4(0.06f, 0.08f, 0.10f, 1.00f);
        c[ImGuiCol_ScrollbarGrab]          = ImVec4(0.22f, 0.28f, 0.36f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.28f, 0.35f, 0.44f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.33f, 0.42f, 0.52f, 1.00f);
        c[ImGuiCol_CheckMark]              = ImVec4(0.25f, 0.78f, 0.90f, 1.00f);
        c[ImGuiCol_SliderGrab]             = ImVec4(0.25f, 0.70f, 0.85f, 1.00f);
        c[ImGuiCol_SliderGrabActive]       = ImVec4(0.35f, 0.85f, 0.95f, 1.00f);
        c[ImGuiCol_Button]                 = ImVec4(0.15f, 0.45f, 0.60f, 1.00f);
        c[ImGuiCol_ButtonHovered]          = ImVec4(0.20f, 0.55f, 0.72f, 1.00f);
        c[ImGuiCol_ButtonActive]           = ImVec4(0.25f, 0.65f, 0.85f, 1.00f);
        c[ImGuiCol_Header]                 = ImVec4(0.15f, 0.40f, 0.55f, 0.80f);
        c[ImGuiCol_HeaderHovered]          = ImVec4(0.20f, 0.50f, 0.68f, 0.80f);
        c[ImGuiCol_HeaderActive]           = ImVec4(0.25f, 0.60f, 0.80f, 0.80f);
        c[ImGuiCol_Separator]              = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_SeparatorHovered]       = ImVec4(0.25f, 0.75f, 0.90f, 1.00f);
        c[ImGuiCol_SeparatorActive]        = ImVec4(0.25f, 0.75f, 0.90f, 1.00f);
        c[ImGuiCol_ResizeGrip]             = ImVec4(0.15f, 0.40f, 0.55f, 0.50f);
        c[ImGuiCol_ResizeGripHovered]      = ImVec4(0.25f, 0.78f, 0.90f, 0.75f);
        c[ImGuiCol_ResizeGripActive]       = ImVec4(0.35f, 0.88f, 1.00f, 1.00f);
        c[ImGuiCol_Tab]                    = ImVec4(0.10f, 0.14f, 0.20f, 1.00f);
        c[ImGuiCol_TabHovered]             = ImVec4(0.20f, 0.50f, 0.68f, 1.00f);
        c[ImGuiCol_TabActive]              = ImVec4(0.15f, 0.40f, 0.55f, 1.00f);
        c[ImGuiCol_PlotLines]              = ImVec4(0.25f, 0.75f, 0.90f, 1.00f);
        c[ImGuiCol_PlotLinesHovered]       = ImVec4(0.35f, 0.85f, 1.00f, 1.00f);
        c[ImGuiCol_PlotHistogram]          = ImVec4(0.25f, 0.75f, 0.90f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.35f, 0.85f, 1.00f, 1.00f);
        c[ImGuiCol_TextSelectedBg]         = ImVec4(0.25f, 0.75f, 0.90f, 0.50f);

        style.WindowRounding    = 6.0f;
        style.FrameRounding     = 4.0f;
        style.GrabRounding      = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.ChildRounding     = 4.0f;
        style.PopupRounding     = 4.0f;
        style.TabRounding       = 4.0f;
        style.WindowBorderSize  = 1.0f;
        style.FrameBorderSize   = 0.0f;
    }

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    static HeatmapConfig heatmap_cfg;
    static future<bool> heatmap_future;
    static bool heatmap_result_ready = false;
    static bool last_heatmap_result = false;
    static vector<int> available_pcis;
    static vector<int> selected_pcis;
    static bool use_all_pcis = true;
    static bool need_pci_refresh = true;
    static string last_earfcn_for_pci = "";

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Smartphone Data & Heatmap");
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);

            if (g_data.db_connected) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "● DATABASE: ONLINE");
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "○ DATABASE: OFFLINE");
                ImGui::SameLine();
                if (ImGui::SmallButton("Retry")) {
                    thread([](){ init_database(); }).detach();
                }
            }
            ImGui::Text("Source: %s", g_data.data_source.c_str());
            ImGui::Separator();

            ImGui::Text("Latitude:  %.6f", g_data.lat);
            ImGui::Text("Longitude: %.6f", g_data.lon);
            ImGui::Text("Net Type:  %s", g_data.type.c_str());
            ImGui::Text("RSRP:      %d dBm", g_data.rsrp);

            ImGui::Separator();
            ImGui::Text("Time Range:");
            ImGui::SliderFloat("Start", &g_data.view_min_time, 0.0f, g_data.max_recorded_time);
            ImGui::SliderFloat("End",   &g_data.view_max_time, 0.0f, g_data.max_recorded_time);
            if (g_data.view_min_time > g_data.view_max_time) g_data.view_min_time = g_data.view_max_time;

            ImGui::Separator();
            if (ImGui::Button("Migrate JSON -> PostgreSQL", ImVec2(-1, 40))) {
                thread(migrate_json_to_sql).detach();
            }

            ImGui::Separator();
            ImGui::Text("Heatmap Generator (IDW)");

            static int selected_criterion = 0;
            const char* criteria[] = { "RSRP", "RSRQ", "RSSI", "Altitude" };
            ImGui::Combo("Criterion", &selected_criterion, criteria, IM_ARRAYSIZE(criteria));
            heatmap_cfg.criterion = criteria[selected_criterion];

            static bool use_all_earfcns = false;
            static char target_earfcn[32] = "38100";

            ImGui::Text("EARFCN Filter:");
            if (ImGui::RadioButton("All EARFCNs", use_all_earfcns)) {
                use_all_earfcns = true;
                need_pci_refresh = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Specific EARFCN", !use_all_earfcns)) {
                use_all_earfcns = false;
                need_pci_refresh = true;
            }

            if (!use_all_earfcns) {
                ImGui::InputText("EARFCN", target_earfcn, IM_ARRAYSIZE(target_earfcn));
                heatmap_cfg.earfcn = string(target_earfcn);
            } else {
                heatmap_cfg.earfcn = "";
                ImGui::BeginDisabled();
                ImGui::InputText("EARFCN", target_earfcn, IM_ARRAYSIZE(target_earfcn));
                ImGui::EndDisabled();
            }

            string current_earfcn_key = use_all_earfcns ? "ALL" : heatmap_cfg.earfcn;
            if (need_pci_refresh || last_earfcn_for_pci != current_earfcn_key) {
                if (g_data.db_connected) {
                    if (use_all_earfcns) {
                        available_pcis = get_available_pcis(DB_CONN, "");
                    } else {
                        available_pcis = get_available_pcis(DB_CONN, heatmap_cfg.earfcn);
                    }
                    selected_pcis.clear();
                    use_all_pcis = true;
                }
                last_earfcn_for_pci = current_earfcn_key;
                need_pci_refresh = false;
            }

            ImGui::Separator();
            ImGui::Text("PCI Filter:");
            if (ImGui::Checkbox("Use all PCIs", &use_all_pcis)) {
                if (use_all_pcis) selected_pcis.clear();
            }

            if (!use_all_pcis && !available_pcis.empty()) {
                ImGui::Text("Available PCIs (%zu):", available_pcis.size());
                int cols = 5;
                int col = 0;
                for (size_t i = 0; i < available_pcis.size(); ++i) {
                    int pci = available_pcis[i];
                    bool is_selected = std::find(selected_pcis.begin(), selected_pcis.end(), pci) != selected_pcis.end();
                    string label = "PCI " + to_string(pci);
                    if (ImGui::Checkbox(label.c_str(), &is_selected)) {
                        if (is_selected) {
                            selected_pcis.push_back(pci);
                            std::sort(selected_pcis.begin(), selected_pcis.end());
                        } else {
                            selected_pcis.erase(std::remove(selected_pcis.begin(), selected_pcis.end(), pci), selected_pcis.end());
                        }
                    }
                    if (++col < cols && i < available_pcis.size() - 1) {
                        ImGui::SameLine();
                    }
                }
                if (!selected_pcis.empty()) {
                    ImGui::Text("Selected: %zu", selected_pcis.size());
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Select at least one PCI!");
                }
            } else if (!use_all_pcis && available_pcis.empty()) {
                ImGui::TextDisabled("No PCI data available");
            }

            heatmap_cfg.useAllPCIs = use_all_pcis;
            heatmap_cfg.selectedPCIs = selected_pcis;

            ImGui::Separator();

            static float search_radius = 35.0f;
            ImGui::SliderFloat("Search radius (m)", &search_radius, 10.0f, 40.0f, "%.0f m");
            heatmap_cfg.searchRadiusMeters = search_radius;

            static float idw_power = 2.0f;
            ImGui::SliderFloat("IDW power (p)", &idw_power, 1.0f, 4.0f, "%.1f");
            heatmap_cfg.idwPower = idw_power;

            static float alpha = 0.85f;
            ImGui::SliderFloat("Alpha", &alpha, 0.1f, 1.0f, "%.2f");
            heatmap_cfg.alpha = alpha;

            static int heatmap_zoom = 15;
            ImGui::SliderInt("Zoom", &heatmap_zoom, 10, 18);
            heatmap_cfg.zoom = heatmap_zoom;

            ImGui::Separator();

            bool can_generate = g_data.db_connected && 
                               (use_all_pcis || !selected_pcis.empty()) &&
                               (use_all_earfcns || !heatmap_cfg.earfcn.empty()) &&
                               !is_heatmap_generating();

            if (!can_generate && !g_data.db_connected) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "ERROR: DB offline!");
            } else if (!can_generate && !use_all_pcis && selected_pcis.empty()) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "ERROR: Select at least one PCI!");
            } else if (!can_generate && !use_all_earfcns && heatmap_cfg.earfcn.empty()) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "ERROR: Enter EARFCN!");
            }

            if (!can_generate) {
                ImGui::BeginDisabled();
            }

            if (ImGui::Button("Generate Heatmap", ImVec2(-1, 30))) {

                fs::remove_all("build");
                fs::create_directories("build");
                clear_heatmap_cache();

                heatmap_future = async(launch::async, [cfg = heatmap_cfg]() mutable -> bool {
                    extern const string DB_CONN;
                    return generate_heatmap_tiles(DB_CONN, cfg);
                });
                heatmap_result_ready = false;
            }

            if (!can_generate) {
                ImGui::EndDisabled();
            }


            if (is_heatmap_generating()) {
                int pct = get_heatmap_progress();
                char buf[32];
                snprintf(buf, sizeof(buf), "%d%%", pct);
                ImGui::ProgressBar(pct / 100.0f, ImVec2(-1, 18), buf);
                ImGui::TextColored(ImVec4(1,1,0,1), "%s", get_heatmap_message().c_str());
            } else if (g_data.heatmap_ready) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Heatmap: READY (%s/%s, zoom=%d)",
                    g_data.heatmap_criterion.c_str(), g_data.heatmap_earfcn.c_str(), g_data.heatmap_zoom);
            }


            if (heatmap_future.valid() && !heatmap_result_ready &&
                heatmap_future.wait_for(chrono::seconds(0)) == future_status::ready) {
                last_heatmap_result = heatmap_future.get();
                heatmap_result_ready = true;
            }

            ImGui::Separator();
            if (ImGui::TreeNode("Raw JSON")) {
                ImGui::TextWrapped("%s", g_data.raw.c_str());
                ImGui::TreePop();
            }
        }
        ImGui::End();

        render_map_window();

        ImGui::Begin("Signal Level (RSRP) History");
        if (ImPlot::BeginPlot("RSRP over Time", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time (sec)", "RSRP (dBm)");
            ImPlot::SetupAxisLimits(ImAxis_X1, (double)g_data.view_min_time, (double)g_data.view_max_time, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -130, -50, ImPlotCond_Once);

            std::lock_guard<std::mutex> lock(g_data.mtx);
            for (auto const& [label, hist] : g_data.cell_logs) {
                if (!hist.x_time.empty()) {
                    ImPlot::PlotLine(label.c_str(), hist.x_time.data(), hist.y_rsrp.data(), (int)hist.x_time.size());
                }
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
}