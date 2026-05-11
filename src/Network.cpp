#include "Network.h"
#include "TelemetryData.h"
#include "Parser.h"
#include <zmq.hpp>
#include <pqxx/pqxx>
#include <fstream>
#include <iostream>

using namespace std;

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
            if (log_file.is_open()) { 
                log_file << raw << endl; 
                log_file.close(); 
            }

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