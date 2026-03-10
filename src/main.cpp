#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <zmq.hpp> 

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using namespace std;

struct TelemetryData {
    float lat = 0, lon = 0, alt = 0, acc = 0;
    int rsrp = 0;
    string type = "";
    string raw = "";
    mutex mtx; 
    vector<double> x_time; 
    vector<double> y_rsrp;
    chrono::steady_clock::time_point start_time;

    TelemetryData() { start_time = chrono::steady_clock::now(); }
} g_data;

string find_val(string json_text, string key) {
    string stroka_poiska = "\"" + key + "\":";
    size_t start_index = json_text.find(stroka_poiska);

    if (start_index == string::npos) { //если слово не нашлось
        return "0";
    }

    size_t value_start = start_index + stroka_poiska.length();

    if (json_text[value_start] == '\"') {
        value_start++;
    }

    size_t value_end = json_text.find_first_of("\",}]", value_start);

    if (value_end == string::npos) {
        return "0";
    }

    string result = json_text.substr(value_start, value_end - value_start);
    return result;
}

int get_registered_rsrp(string json) {
    size_t reg_pos = json.find("\"isRegistered\":true");
    if (reg_pos == string::npos) {
        return stoi(find_val(json, "Primary_RSRP"));
    }
    string sub = json.substr(reg_pos);
    return stoi(find_val(sub, "RSRP"));
}

void zmq_server() {
    zmq::context_t ctx(1);
    zmq::socket_t sock(ctx, zmq::socket_type::rep);
    sock.bind("tcp://*:25565");

    while (true) {
        zmq::message_t msg;
        if (sock.recv(msg, zmq::recv_flags::none)) {
            string raw(static_cast<char*>(msg.data()), msg.size());
            
            auto now = chrono::steady_clock::now();
            double elapsed = chrono::duration_cast<chrono::milliseconds>(now - g_data.start_time).count() / 1000.0;

            lock_guard<mutex> lock(g_data.mtx);
            g_data.raw = raw;
            g_data.lat = stof(find_val(raw, "Latitude"));
            g_data.lon = stof(find_val(raw, "Longitude"));
            g_data.alt = stof(find_val(raw, "Altitude"));
            g_data.acc = stof(find_val(raw, "Accuracy"));
            
            g_data.rsrp = get_registered_rsrp(raw);
            g_data.type = find_val(raw, "Net Type");

            g_data.x_time.push_back(elapsed);
            g_data.y_rsrp.push_back((double)g_data.rsrp);

            if(g_data.x_time.size() > 200) {
                g_data.x_time.erase(g_data.x_time.begin());
                g_data.y_rsrp.erase(g_data.y_rsrp.begin());
            }

            sock.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        }
    }
}

void ui_loop() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow("Telemetry Monitor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Smartphone Data");
        {
            lock_guard<mutex> lock(g_data.mtx);
            ImGui::Text("Latitude:  %.6f", g_data.lat);
            ImGui::Text("Longitude: %.6f", g_data.lon);
            ImGui::Text("Net Type:  %s", g_data.type.c_str());
            ImGui::Text("RSRP:      %d dBm", g_data.rsrp);
            ImGui::Separator();
            if (ImGui::TreeNode("Raw JSON")) {
                ImGui::TextWrapped("%s", g_data.raw.c_str());
                ImGui::TreePop();
            }
        }
        ImGui::End();

        ImGui::Begin("Signal Level (RSRP) History");
        if (ImPlot::BeginPlot("RSRP over Time", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time (sec)", "RSRP (dBm)");

            lock_guard<mutex> lock(g_data.mtx);
            
            ImPlot::SetupAxisLimits(ImAxis_Y1, -120, -60, ImPlotCond_Once);
            ImPlot::SetupAxisFormat(ImAxis_Y1, "%.2f dBm"); 

            if (!g_data.x_time.empty()) {
                ImPlot::PlotLine("Serving Cell Signal", g_data.x_time.data(), g_data.y_rsrp.data(), (int)g_data.x_time.size());
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    thread(zmq_server).detach();
    ui_loop();

    return 0;
}   