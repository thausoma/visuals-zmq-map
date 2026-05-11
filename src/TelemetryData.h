#ifndef TELEMETRY_DATA_H
#define TELEMETRY_DATA_H

#include <vector>
#include <string>
#include <mutex>
#include <map>

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
    bool use_sql_storage = false;

    std::map<std::string, CellHistory> cell_logs; 
    
    std::vector<double> history_lat;
    std::vector<double> history_lon;
    std::vector<double> history_time;

    double base_timestamp = 0;
    float view_min_time = 0;
    float view_max_time = 100;
    float max_recorded_time = 100;
};
extern TelemetryData g_data;

#endif