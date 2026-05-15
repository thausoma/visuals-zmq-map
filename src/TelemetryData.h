#ifndef TELEMETRY_DATA_H
#define TELEMETRY_DATA_H

#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <atomic>


struct CellHistory {
    std::vector<double> x_time;
    std::vector<double> y_rsrp;
};

struct TelemetryData {
    float lat = 0, lon = 0, alt = 0, acc = 0;
    int rsrp = 0;
    std::string type = "";
    std::string raw = "";
    std::mutex mtx;

    bool db_connected = false;
    std::string data_source = "None";

    std::map<std::string, CellHistory> cell_logs;
    std::vector<double> history_lat;
    std::vector<double> history_lon;
    std::vector<double> history_time;

    double base_timestamp = 0;
    float view_min_time = 0;
    float view_max_time = 100;
    float max_recorded_time = 100;
    double heatmap_min_lat = 0, heatmap_max_lat = 0;
    double heatmap_min_lon = 0, heatmap_max_lon = 0;
    bool heatmap_ready = false;
    std::string heatmap_earfcn = "";
    std::string heatmap_criterion = "";


    
    void clear_all() {
        cell_logs.clear();
        history_lat.clear();
        history_lon.clear();
        history_time.clear();
        base_timestamp = 0;
        max_recorded_time = 0;
        raw = "";
    }
};
extern TelemetryData g_data;

#endif