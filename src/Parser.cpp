#include "Parser.h"
#include "TelemetryData.h"
#include <iostream>

using namespace std;

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

    if (g_data.lat != 0.0f || g_data.lon != 0.0f) {
        g_data.history_lat.push_back(g_data.lat);
        g_data.history_lon.push_back(g_data.lon);
        g_data.history_time.push_back(elapsed);
    }

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