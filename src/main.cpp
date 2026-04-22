#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <fstream>
#include <map>
#include <zmq.hpp> 
#include <pqxx/pqxx>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using namespace std;

struct CellHistory {
    vector<double> x_time;
    vector<double> y_rsrp;
};

struct TelemetryData {
    float lat = 0, lon = 0, alt = 0, acc = 0;
    int rsrp = 0;
    string type = "";
    string raw = "";
    mutex mtx; 
    bool use_sql_storage = false;

    map<string, CellHistory> cell_logs; 
    
    double base_timestamp = 0;
    float view_min_time = 0;
    float view_max_time = 100;
    float max_recorded_time = 100;

} g_data;

void load_from_sql();

string find_val(string json_text, string key) {
    string stroka_poiska = "\"" + key + "\":";
    size_t start_index = json_text.find(stroka_poiska);
    if (start_index == string::npos) return "0";
    size_t value_start = start_index + stroka_poiska.length();
    if (json_text[value_start] == '\"') value_start++;
    size_t value_end = json_text.find_first_of("\",}]", value_start);
    if (value_end == string::npos) return "0";
    return json_text.substr(value_start, value_end - value_start);
}

void parse_json_to_data(string raw) {
    lock_guard<mutex> lock(g_data.mtx);
    g_data.raw = raw;
    g_data.lat = stof(find_val(raw, "Latitude"));
    g_data.lon = stof(find_val(raw, "Longitude"));
    g_data.alt = stof(find_val(raw, "Altitude"));
    g_data.acc = stof(find_val(raw, "Accuracy"));
    g_data.type = find_val(raw, "Net Type");
    g_data.rsrp = stoi(find_val(raw, "RSRP"));

    string time_str = find_val(raw, "Current Time");
    if (time_str == "0") return;
    double current_ts = stod(time_str) / 1000.0;

    if (g_data.base_timestamp == 0) {
        g_data.base_timestamp = current_ts;
    }
    double elapsed = current_ts - g_data.base_timestamp;
    g_data.max_recorded_time = elapsed;

    size_t cell_start = raw.find("\"Cells\":[");
    if (cell_start != string::npos) {
        size_t pos = cell_start;
        while ((pos = raw.find("{", pos + 1)) != string::npos && pos < raw.find("]", cell_start)) {
            string sub = raw.substr(pos, raw.find("}", pos) - pos + 1);
            
            string pci = find_val(sub, "PCI");
            string earfcn = find_val(sub, "EARFCN");
            string type = find_val(sub, "Type");
            string c_rsrp = (type == "GSM") ? find_val(sub, "Dbm") : find_val(sub, "RSRP");
            string unique_id = type + "_P" + pci + "_E" + (earfcn != "0" ? earfcn : find_val(sub, "ARFCN"));

            if (c_rsrp != "0") {
                auto& hist = g_data.cell_logs[unique_id];
                hist.x_time.push_back(elapsed);
                hist.y_rsrp.push_back(stod(c_rsrp));
            }
        }
    }
}

void load_history() {
    ifstream log_file("telemetry_log.json");
    string line;
    while (getline(log_file, line)) {
        if (!line.empty()) {
            parse_json_to_data(line);
        }
    }
    g_data.view_max_time = g_data.max_recorded_time;
    cout << "Loaded history. Max time: " << g_data.max_recorded_time << " seconds." << endl;
}

void zmq_server() {
    zmq::context_t ctx(1);
    zmq::socket_t sock(ctx, zmq::socket_type::rep);
    sock.bind("tcp://*:25566");

    pqxx::connection c("host=127.0.0.1 port=5533 dbname=mobile_monitor user=thausoma password=password123");

    while (true) {
        zmq::message_t msg;
        if (sock.recv(msg, zmq::recv_flags::none)) {
            string raw(static_cast<char*>(msg.data()), msg.size());
            
            ofstream log_file("telemetry_log.json", ios::app); 
            if (log_file.is_open()) { log_file << raw << endl; log_file.close(); }

            parse_json_to_data(raw);

            if (g_data.use_sql_storage) {
                try {
                    pqxx::work W(c);
                    
                    pqxx::result res = W.exec_params(
                        "INSERT INTO measurements (latitude, longitude, altitude, accuracy, net_type, rsrp_global, current_time_ms) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                        find_val(raw, "Latitude"), find_val(raw, "Longitude"), 
                        find_val(raw, "Altitude"), find_val(raw, "Accuracy"),
                        find_val(raw, "Net Type"), find_val(raw, "RSRP"), find_val(raw, "Current Time")
                    );
                    int m_id = res[0][0].as<int>();

                    // Разбираем массив Cells и пишем каждую соту
                    size_t cell_start = raw.find("\"Cells\":[");
                    if (cell_start != string::npos) {
                        size_t pos = cell_start;
                        while ((pos = raw.find("{", pos + 1)) != string::npos && pos < raw.find("]", cell_start)) {
                            string sub = raw.substr(pos, raw.find("}", pos) - pos + 1);
                            
                            W.exec_params(
                                "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                                m_id, find_val(sub, "Type"), find_val(sub, "Band"), find_val(sub, "CellIdentity"),
                                find_val(sub, "EARFCN"), find_val(sub, "MCC"), find_val(sub, "MNC"),
                                find_val(sub, "PCI"), find_val(sub, "TAC"), find_val(sub, "ASU Level"),
                                find_val(sub, "CQI"), (find_val(sub, "Type") == "GSM" ? find_val(sub, "Dbm") : find_val(sub, "RSRP")),
                                find_val(sub, "RSRQ"), find_val(sub, "RSSI"), find_val(sub, "RSSNR"), find_val(sub, "Timing Advance")
                            );
                        }
                    }
                    W.commit();
                } catch (const std::exception &e) {
                    cerr << "[ZMQ-SQL] Real-time insert failed: " << e.what() << endl;
                }
            }

            if (g_data.view_max_time >= g_data.max_recorded_time - 5.0f) {
                g_data.view_max_time = g_data.max_recorded_time;
            }

            sock.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        }
    }
}

void migrate_json_to_sql() {
    try {
        cout << "[SQL] Starting migration process..." << endl;
        
        pqxx::connection c("host=127.0.0.1 port=5533 dbname=mobile_monitor user=thausoma password=password123");
        if (!c.is_open()) {
            cerr << "[SQL] Failed to open database!" << endl;
            return;
        }

        pqxx::work W(c);
        cout << "[SQL] Cleaning up old data..." << endl;
        W.exec("TRUNCATE measurements CASCADE;");

        ifstream log_file("telemetry_log.json");
        if (!log_file.is_open()) {
            cerr << "[SQL] ERROR: Could not open telemetry_log.json! Check if the file exists in the working directory." << endl;
            return;
        }

        string line;
        int count = 0;
        while (getline(log_file, line)) {
            if (line.empty()) continue;

            string lat = find_val(line, "Latitude");
            string lon = find_val(line, "Longitude");
            string alt = find_val(line, "Altitude");
            string acc = find_val(line, "Accuracy");
            string net = find_val(line, "Net Type");
            string rsrp_glob = find_val(line, "RSRP");
            string ts  = find_val(line, "Current Time");

            pqxx::result res = W.exec_params(
                "INSERT INTO measurements (latitude, longitude, altitude, accuracy, net_type, rsrp_global, current_time_ms) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                lat, lon, alt, acc, net, rsrp_glob, ts
            );

            int m_id = res[0][0].as<int>();

            size_t cell_start = line.find("\"Cells\":[");
            if (cell_start != string::npos) {
                size_t pos = cell_start;
                while ((pos = line.find("{", pos + 1)) != string::npos && pos < line.find("]", cell_start)) {
                    string sub = line.substr(pos, line.find("}", pos) - pos + 1);
                    
                    W.exec_params(
                        "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                        m_id, 
                        find_val(sub, "Type"), find_val(sub, "Band"), find_val(sub, "CellIdentity"),
                        find_val(sub, "EARFCN"), find_val(sub, "MCC"), find_val(sub, "MNC"),
                        find_val(sub, "PCI"), find_val(sub, "TAC"), find_val(sub, "ASU Level"),
                        find_val(sub, "CQI"), (find_val(sub, "Type") == "GSM" ? find_val(sub, "Dbm") : find_val(sub, "RSRP")),
                        find_val(sub, "RSRQ"), find_val(sub, "RSSI"), find_val(sub, "RSSNR"), find_val(sub, "Timing Advance")
                    );
                }
            }
            count++;
            if (count % 100 == 0) cout << "[SQL] Processed " << count << " lines..." << endl;
        }

        W.commit();
            cout << "[SQL] Migration successful! Total processed: " << count << " entries." << endl;
            
            {
                lock_guard<mutex> lock(g_data.mtx);
                g_data.use_sql_storage = true;
            }

            load_from_sql(); 

        } catch (const std::exception &e) {
            cerr << "[SQL] MIGRATION CRITICAL ERROR: " << e.what() << endl;
    }
}

void load_from_sql() {
    try {
        pqxx::connection c("host=127.0.0.1 port=5533 dbname=mobile_monitor user=thausoma password=password123");
        if (!c.is_open()) return;

        pqxx::nontransaction N(c);
        pqxx::result res = N.exec(
            "SELECT m.current_time_ms, c.cell_type, c.pci, c.earfcn, c.rsrp "
            "FROM cell_data c "
            "JOIN measurements m ON c.measurement_id = m.id "
            "ORDER BY m.current_time_ms ASC"
        );

        lock_guard<mutex> lock(g_data.mtx);
        g_data.cell_logs.clear();

        for (auto const &row : res) {
            double current_ts = row["current_time_ms"].as<double>() / 1000.0;
            if (g_data.base_timestamp == 0) g_data.base_timestamp = current_ts;
            double elapsed = current_ts - g_data.base_timestamp;

            string type = row["cell_type"].as<string>();
            string pci = row["pci"].as<string>();
            string earfcn = row["earfcn"].as<string>();
            
            string unique_id = type + "_P" + pci + "_E" + earfcn + " (SQL)";
            
            int rsrp_val = row["rsrp"].as<int>();
            if (rsrp_val != 0) {
                auto& hist = g_data.cell_logs[unique_id];
                hist.x_time.push_back(elapsed);
                hist.y_rsrp.push_back((double)rsrp_val);
            }
        }
        cout << "[SQL] Data reloaded from database. Points: " << res.size() << endl;

    } catch (const std::exception &e) {
        cerr << "[SQL] FETCH ERROR: " << e.what() << endl;
    }
}

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
            ImGui::Text("Latitude:  %.6f", g_data.lat);
            ImGui::Text("Longitude: %.6f", g_data.lon);
            ImGui::Text("Net Type:  %s", g_data.type.c_str());
            ImGui::Text("RSRP:      %d dBm", g_data.rsrp);
            
            ImGui::Separator();
            ImGui::Text("Time Range Filter (seconds from start):");
            ImGui::SliderFloat("Start Time", &g_data.view_min_time, 0.0f, g_data.max_recorded_time);
            ImGui::SliderFloat("End Time",   &g_data.view_max_time, 0.0f, g_data.max_recorded_time);

            ImGui::Separator();
            if (ImGui::Button("Migrate JSON -> PostgreSQL", ImVec2(-1, 40))) {
                thread(migrate_json_to_sql).detach();
            }

            if (g_data.use_sql_storage) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Storage: PostgreSQL Active");
            } else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Storage: Local JSON (Legacy)");
}
            
            if (g_data.view_min_time > g_data.view_max_time) {
                g_data.view_min_time = g_data.view_max_time;
            }

            ImGui::Separator();
            if (ImGui::TreeNode("Raw JSON")) {
                ImGui::TextWrapped("%s", g_data.raw.c_str());
                ImGui::TreePop();
            }
        }
        ImGui::End();

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

int main(int argc, char *argv[]) {
    load_history();
    
    thread(zmq_server).detach();
    ui_loop();
    return 0;
}