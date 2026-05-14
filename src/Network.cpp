#include "Network.h"
#include "TelemetryData.h"
#include "Parser.h"
#include <zmq.hpp>
#include <iostream>
#include <string>
#include "Database.h"

using namespace std;

void zmq_server() {
    zmq::context_t ctx(1);
    zmq::socket_t sock(ctx, zmq::socket_type::rep);

    try {
        sock.bind("tcp://*:25566");
        cout << "[ZMQ] Server started on port 25566" << endl;
    } catch (const zmq::error_t& e) {
        cerr << "[ZMQ] Failed to bind: " << e.what() << endl;
        return;
    }

    while (true) {
        zmq::message_t msg;
        if (sock.recv(msg, zmq::recv_flags::none)) {
            string raw(static_cast<char*>(msg.data()), msg.size());

            if (!raw.empty()) {
                save_packet(raw);

                lock_guard<mutex> lock(g_data.mtx);
                if (g_data.view_max_time >= g_data.max_recorded_time - 5.0f) {
                    g_data.view_max_time = g_data.max_recorded_time;
                }
            }

            sock.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        }
    }
}