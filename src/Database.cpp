#include "Database.h"
#include "TelemetryData.h"
#include "Parser.h"
#include <iostream>
#include <fstream>
#include <pqxx/pqxx>

using namespace std;

const string DB_CONN = "host=127.0.0.1 port=5533 dbname=mobile_monitor user=thausoma password=password123";

bool check_db_alive() {
    try {
        pqxx::connection c(DB_CONN);
        return c.is_open();
    } catch (...) {
        return false;
    }
}

void init_database() {
    g_data.db_connected = check_db_alive();
    sync_all_data();
}

void sync_all_data() {
    g_data.clear_all();

    if (g_data.db_connected) {
        try {
            pqxx::connection c(DB_CONN);
            pqxx::nontransaction N(c);

            pqxx::result res = N.exec("SELECT latitude, longitude, current_time_ms FROM measurements ORDER BY current_time_ms ASC");
            for (auto const &row : res) {
                double ts = row["current_time_ms"].as<double>() / 1000.0;
                if (g_data.base_timestamp == 0) g_data.base_timestamp = ts;
                double elapsed = ts - g_data.base_timestamp;

                double lat = row["latitude"].as<double>();
                double lon = row["longitude"].as<double>();

                if (lat != 0.0 && lon != 0.0) {
                    g_data.history_lat.push_back(lat);
                    g_data.history_lon.push_back(lon);
                    g_data.history_time.push_back(elapsed);
                }
                g_data.max_recorded_time = (float)elapsed;
            }

            pqxx::result cell_res = N.exec(
                "SELECT m.current_time_ms, c.cell_type, c.pci, c.earfcn, c.rsrp "
                "FROM cell_data c JOIN measurements m ON c.measurement_id = m.id "
                "ORDER BY m.current_time_ms ASC"
            );

            for (auto const &row : cell_res) {
                double elapsed = (row["current_time_ms"].as<double>() / 1000.0) - g_data.base_timestamp;
                string label = row["cell_type"].as<string>() + "_P" + row["pci"].as<string>() + "_E" + row["earfcn"].as<string>();
                int val = row["rsrp"].as<int>();
                if (val != 0) {
                    g_data.cell_logs[label].x_time.push_back(elapsed);
                    g_data.cell_logs[label].y_rsrp.push_back((double)val);
                }
            }

            g_data.data_source = "PostgreSQL";
            g_data.view_max_time = g_data.max_recorded_time;
            cout << "[DB] Synced from SQL. Points: " << res.size() << endl;
            return;
        } catch (const exception &e) {
            cerr << "[DB] SQL sync failed: " << e.what() << endl;
            g_data.db_connected = false;
        }
    }

    g_data.data_source = "Local JSON (Backup)";
    ifstream log_file("telemetry_log.json");
    string line;
    while (getline(log_file, line)) {
        if (!line.empty()) parse_json_to_data(line);
    }
    g_data.view_max_time = g_data.max_recorded_time;
    cout << "[DB] Synced from JSON. Max time: " << g_data.max_recorded_time << endl;
}

void save_packet(const string& raw_json) {
    {
        ofstream log_file("telemetry_log.json", ios::app);
        if (log_file.is_open()) log_file << raw_json << endl;
    }

    if (g_data.db_connected) {
        try {
            pqxx::connection c(DB_CONN);
            pqxx::work W(c);

            pqxx::result res = W.exec_params(
                "INSERT INTO measurements (latitude, longitude, altitude, accuracy, net_type, rsrp_global, current_time_ms) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                find_val(raw_json, "Latitude"), find_val(raw_json, "Longitude"),
                find_val(raw_json, "Altitude"), find_val(raw_json, "Accuracy"),
                find_val(raw_json, "Net Type"), find_val(raw_json, "RSRP"), find_val(raw_json, "Current Time")
            );

            int m_id = res[0][0].as<int>();

            size_t cell_start = raw_json.find("\"Cells\":[");
            if (cell_start != string::npos) {
                size_t pos = cell_start;
                size_t array_end = raw_json.find("]", cell_start);
                while ((pos = raw_json.find("{", pos + 1)) != string::npos && pos < array_end) {
                    size_t end_pos = raw_json.find("}", pos);
                    if (end_pos == string::npos) break;
                    string sub = raw_json.substr(pos, end_pos - pos + 1);
                    string c_type = find_val(sub, "Type");

                    W.exec_params(
                        "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                        m_id, c_type, find_val(sub, "Band"), find_val(sub, "CellIdentity"),
                        find_val(sub, "EARFCN"), find_val(sub, "MCC"), find_val(sub, "MNC"),
                        find_val(sub, "PCI"), find_val(sub, "TAC"), find_val(sub, "ASU Level"),
                        find_val(sub, "CQI"), (c_type == "GSM" ? find_val(sub, "Dbm") : find_val(sub, "RSRP")),
                        find_val(sub, "RSRQ"), find_val(sub, "RSSI"), find_val(sub, "RSSNR"), find_val(sub, "Timing Advance")
                    );
                    pos = end_pos;
                }
            }
            W.commit();
        } catch (const exception &e) {
            cerr << "[DB] Save to SQL failed: " << e.what() << endl;
            g_data.db_connected = false;
        }
    }

    parse_json_to_data(raw_json);
}

void migrate_json_to_sql() {
    if (!check_db_alive()) {
        cerr << "[DB] Cannot migrate: DB unreachable" << endl;
        return;
    }

    try {
        pqxx::connection c(DB_CONN);
        pqxx::work W(c);
        W.exec("TRUNCATE measurements CASCADE;");

        ifstream log_file("telemetry_log.json");
        string line;
        int count = 0;
        while (getline(log_file, line)) {
            if (line.empty() || line.find('{') == string::npos) continue;

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
                size_t array_end = line.find("]", cell_start);
                while ((pos = line.find("{", pos + 1)) != string::npos && pos < array_end) {
                    size_t end_pos = line.find("}", pos);
                    if (end_pos == string::npos) break;
                    string sub = line.substr(pos, end_pos - pos + 1);
                    string c_type = find_val(sub, "Type");

                    W.exec_params(
                        "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                        m_id, c_type, find_val(sub, "Band"), find_val(sub, "CellIdentity"),
                        find_val(sub, "EARFCN"), find_val(sub, "MCC"), find_val(sub, "MNC"),
                        find_val(sub, "PCI"), find_val(sub, "TAC"), find_val(sub, "ASU Level"),
                        find_val(sub, "CQI"), (c_type == "GSM" ? find_val(sub, "Dbm") : find_val(sub, "RSRP")),
                        find_val(sub, "RSRQ"), find_val(sub, "RSSI"), find_val(sub, "RSSNR"), find_val(sub, "Timing Advance")
                    );
                    pos = end_pos;
                }
            }
            count++;
        }
        W.commit();
        cout << "[DB] Migration complete. Records: " << count << endl;

        g_data.db_connected = true;
        sync_all_data();

    } catch (const exception &e) {
        cerr << "[DB] Migration failed: " << e.what() << endl;
    }
}