#include "UI.h"
#include "TelemetryData.h"
#include "Database.h"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <thread>
#include <mutex>
#include "Map.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using namespace std;

void ui_loop() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow("Telemetry Monitor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Smartphone Data");
        {
            lock_guard<mutex> lock(g_data.mtx);

            if (g_data.db_connected) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "● DATABASE: ONLINE");
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "○ DATABASE: OFFLINE");
                ImGui::SameLine();
                if (ImGui::SmallButton("Retry Connect")) {
                    thread([]() { init_database(); }).detach();
                }
            }
            ImGui::Text("Source: %s", g_data.data_source.c_str());
            ImGui::Separator();

            ImGui::Text("Latitude:  %.6f", g_data.lat);
            ImGui::Text("Longitude: %.6f", g_data.lon);
            ImGui::Text("Net Type:  %s", g_data.type.c_str());
            ImGui::Text("RSRP:      %d dBm", g_data.rsrp);

            ImGui::Separator();
            ImGui::Text("Time Range Filter (seconds from start):");
            ImGui::SliderFloat("Start Time", &g_data.view_min_time, 0.0f, g_data.max_recorded_time);
            ImGui::SliderFloat("End Time",   &g_data.view_max_time, 0.0f, g_data.max_recorded_time);

            if (g_data.view_min_time > g_data.view_max_time) {
                g_data.view_min_time = g_data.view_max_time;
            }

            ImGui::Separator();
            if (ImGui::Button("Migrate JSON -> PostgreSQL", ImVec2(-1, 40))) {
                thread(migrate_json_to_sql).detach();
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
            ImPlot::SetupAxisLimits(ImAxis_X1, (double)g_data.view_min_time, (double)g_data.view_max_time, ImPlotCond_Always);
            ImPlot::SetupAxes("Time (sec)", "RSRP (dBm)");
            ImPlot::SetupAxisLimits(ImAxis_Y1, -130, -50, ImPlotCond_Once);

            lock_guard<mutex> lock(g_data.mtx);
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