#include "Heatmap.h"
#include "TelemetryData.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <mutex>
#include <pqxx/pqxx>
#include <filesystem>
#include <GL/glew.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

struct DataPoint { double lat, lon, value; };
struct RGBA { uint8_t r, g, b, a; };

GLuint g_heatmap_texture = 0;

double haversine(double lat1, double lon1, double lat2, double lon2) {
    double R = 6371000.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat/2)*sin(dLat/2) +
               cos(lat1*M_PI/180.0)*cos(lat2*M_PI/180.0) *
               sin(dLon/2)*sin(dLon/2);
    return R * 2 * atan2(sqrt(a), sqrt(1-a));
}

RGBA get_color(double val, const string& criterion) {
    if (criterion == "RSRP") {
        if (val < -110) return {0, 0, 0, 0};
        if (val <= -100) return {0, 0, 139, 200};
        if (val <= -90)  return {0, 255, 255, 200};
        if (val <= -80)  return {255, 165, 0, 200};
        return {255, 0, 0, 200};
    }
    if (criterion == "RSRQ") {
        if (val < -20) return {0, 0, 0, 0};
        if (val <= -15) return {0, 0, 139, 200};
        if (val <= -10) return {0, 255, 255, 200};
        if (val <= -6)  return {255, 165, 0, 200};
        return {255, 0, 0, 200};
    }
    if (criterion == "RSSI") {
        if (val < -110) return {0, 0, 0, 0};
        if (val <= -95) return {0, 0, 139, 200};
        if (val <= -85) return {0, 255, 255, 200};
        if (val <= -70) return {255, 165, 0, 200};
        return {255, 0, 0, 200};
    }
    if (val < 0) return {0, 0, 0, 0};
    if (val > 500) val = 500;
    uint8_t intensity = (uint8_t)(255 * (val / 500.0));
    uint8_t blue = (uint8_t)(255 - intensity);
    return {0, intensity, blue, 200};
}

void process_heatmap(const string& db_conn, const string& criterion, const string& earfcn) {
    cout << "[HEATMAP] ===== STARTED =====" << endl;
    cout << "[HEATMAP] Criterion: " << criterion << ", EARFCN: " << earfcn << endl;

    if (!g_data.db_connected) {
        cerr << "[HEATMAP] ERROR: Database offline" << endl;
        return;
    }

    vector<DataPoint> points;

    try {
        pqxx::connection c(db_conn);
        pqxx::nontransaction N(c);

        string field = "c.rsrp";
        if (criterion == "RSRQ") field = "c.rsrq";
        else if (criterion == "RSSI") field = "c.rssi";
        else if (criterion == "Altitude") field = "m.altitude";

        string query = "SELECT m.latitude, m.longitude, " + field + " as val "
                       "FROM cell_data c JOIN measurements m ON c.measurement_id = m.id "
                       "WHERE c.earfcn = $1";
                       
        cout << "[HEATMAP] SQL query: " << query << endl;
        pqxx::result res = N.exec_params(query, earfcn);
        cout << "[HEATMAP] Rows fetched: " << res.size() << endl;

        for (auto const &row : res) {
            double v = row["val"].as<double>(0.0);
            double lat = row["latitude"].as<double>(0.0);
            double lon = row["longitude"].as<double>(0.0);
            
            if (v != 0.0 && lat != 0.0 && lon != 0.0) {
                points.push_back({lat, lon, v});
            }
        }
    } catch (const exception& e) {
        cerr << "[HEATMAP] DB ERROR: " << e.what() << endl;
        return;
    }

    cout << "[HEATMAP] Valid points after filter: " << points.size() << endl;

    if (points.empty()) {
        cout << "[HEATMAP] ABORT: No data for EARFCN " << earfcn << endl;
        return;
    }

    double min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    for (const auto& p : points) {
        if (p.lat < min_lat) min_lat = p.lat;
        if (p.lat > max_lat) max_lat = p.lat;
        if (p.lon < min_lon) min_lon = p.lon;
        if (p.lon > max_lon) max_lon = p.lon;
    }
    double pad = 0.001; 
    min_lat -= pad; max_lat += pad;
    min_lon -= pad; max_lon += pad;

    cout << "[HEATMAP] BBOX: lat[" << min_lat << "," << max_lat 
         << "] lon[" << min_lon << "," << max_lon << "]" << endl;


    const int W = 400, H = 400;
    vector<uint8_t> pixels(W * H * 4, 0);

    const double MAX_RADIUS = 300.0;
    int colored_pixels = 0;
    int total_iterations = 0;

    cout << "[HEATMAP] Starting IDW grid " << W << "x" << H << "..." << endl;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            double p_lon = min_lon + (max_lon - min_lon) * ((double)x / W);
            double p_lat = max_lat - (max_lat - min_lat) * ((double)y / H);

            double sum_w = 0.0, sum_v = 0.0;
            bool has = false;

            for (const auto& pt : points) {
                double d = haversine(p_lat, p_lon, pt.lat, pt.lon);
                if (d <= MAX_RADIUS) {
                    if (d < 1.0) d = 1.0;
                    double w = 1.0 / (d * d);
                    sum_w += w;
                    sum_v += w * pt.value;
                    has = true;
                }
            }

            if (has && sum_w > 0) {
                double z = sum_v / sum_w;
                RGBA col = get_color(z, criterion);
                
                if (colored_pixels < 5) {
                    cout << "[HEATMAP] Pixel(" << x << "," << y << ") z=" << z 
                         << " color=(" << (int)col.r << "," << (int)col.g << "," << (int)col.b << "," << (int)col.a << ")" << endl;
                }
                
                int idx = (y * W + x) * 4;
                pixels[idx]   = col.r;
                pixels[idx+1] = col.g;
                pixels[idx+2] = col.b;
                pixels[idx+3] = col.a;
                if (col.a > 0) colored_pixels++;
            }
            total_iterations++;
        }
        
        if (y % 100 == 0) {
            cout << "[HEATMAP] Progress: " << y << "/" << H << " rows, colored=" << colored_pixels << endl;
        }
    }

    cout << "[HEATMAP] DONE: total=" << total_iterations 
         << ", colored=" << colored_pixels 
         << " (" << (100.0 * colored_pixels / total_iterations) << "%)" << endl;

    filesystem::create_directories("build");
    stbi_write_png("build/heatmap.png", W, H, 4, pixels.data(), W * 4);
    cout << "[HEATMAP] Saved to build/heatmap.png" << endl;

    if (filesystem::exists("build/heatmap.png")) {
        auto size = filesystem::file_size("build/heatmap.png");
        cout << "[HEATMAP] File size: " << size << " bytes" << endl;
    }

    {
        lock_guard<mutex> lock(g_data.mtx);
        g_data.heatmap_min_lat = min_lat;
        g_data.heatmap_max_lat = max_lat;
        g_data.heatmap_min_lon = min_lon;
        g_data.heatmap_max_lon = max_lon;
        g_data.heatmap_earfcn = earfcn;
        g_data.heatmap_criterion = criterion;
    }
    g_data.heatmap_ready = true;

    cout << "[HEATMAP] ===== FINISHED =====" << endl;
}

void generate_heatmap_thread(const string& db_conn, const string& criterion, const string& earfcn) {
    thread(process_heatmap, db_conn, criterion, earfcn).detach();
}

void reload_heatmap_texture() {
    if (!filesystem::exists("build/heatmap.png")) {
        cout << "[HEATMAP-TEX] File not found: build/heatmap.png" << endl;
        return;
    }

    int w, h, ch;
    unsigned char* data = stbi_load("build/heatmap.png", &w, &h, &ch, 4);
    if (!data) {
        cerr << "[HEATMAP-TEX] Failed to load PNG" << endl;
        return;
    }

    if (g_heatmap_texture == 0) glGenTextures(1, &g_heatmap_texture);
    
    glBindTexture(GL_TEXTURE_2D, g_heatmap_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    cout << "[HEATMAP-TEX] Loaded texture " << w << "x" << h << endl;
}