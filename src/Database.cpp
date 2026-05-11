#include "Database.h"
#include "TelemetryData.h"
#include "Parser.h"
#include <iostream>
#include <fstream>
#include <pqxx/pqxx>

using namespace std;

void load_history() {
    ifstream log_file("telemetry_log.json");
    string line;
    while (getline(log_file, line)) {
        if (!line.empty()) {
            parse_json_to_data(line);
        }
    }
    g_data.view_max_time = g_data.max_recorded_time;
    cout << "[DISK] Loaded history from JSON. Max time: " << g_data.max_recorded_time << "s" << endl;
}

void migrate_json_to_sql() {
    try {
        cout << "[SQL] Starting migration..." << endl;
        pqxx::connection c("host=127.0.0.1 port=5533 dbname=mobile_monitor user=thausoma password=password123");
        
        pqxx::work W(c);
        W.exec("TRUNCATE measurements CASCADE;");

        ifstream log_file("telemetry_log.json");
        string line;
        int count = 0;
        while (getline(log_file, line)) {
            if (line.empty()) continue;

            pqxx::result res = W.exec_params(
                "INSERT INTO measurements (latitude, longitude, altitude, accuracy, net_type, rsrp_global, current_time_ms) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                find_val(line, "Latitude"), find_val(line, "Longitude"), 
                find_val(line, "Altitude"), find_val(line, "Accuracy"),
                find_val(line, "Net Type"), find_val(line, "RSRP"), find_val(line, "Current Time")
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
                        m_id, find_val(sub, "Type"), find_val(sub, "Band"), find_val(sub, "CellIdentity"),
                        find_val(sub, "EARFCN"), find_val(sub, "MCC"), find_val(sub, "MNC"),
                        find_val(sub, "PCI"), find_val(sub, "TAC"), find_val(sub, "ASU Level"),
                        find_val(sub, "CQI"), (find_val(sub, "Type") == "GSM" ? find_val(sub, "Dbm") : find_val(sub, "RSRP")),
                        find_val(sub, "RSRQ"), find_val(sub, "RSSI"), find_val(sub, "RSSNR"), find_val(sub, "Timing Advance")
                    );
                }
            }
            count++;
        }
        W.commit();
        cout << "[SQL] Migration successful. Entries: " << count << endl;

        {
            lock_guard<mutex> lock(g_data.mtx);
            g_data.use_sql_storage = true;
        }
        load_from_sql(); 

    } catch (const std::exception &e) {
        cerr << "[SQL] Migration Error: " << e.what() << endl;
    }
}

void load_from_sql() {
    try {
        pqxx::connection c("host=127.0.0.1 port=5533 dbname=mobile_monitor user=thausoma password=password123");
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
        cout << "[SQL] Data reloaded. Points: " << res.size() << endl;
    } catch (const std::exception &e) {
        cerr << "[SQL] Fetch Error: " << e.what() << endl;
    }
}