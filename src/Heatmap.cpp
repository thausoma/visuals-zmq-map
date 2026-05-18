#include "Heatmap.h"
#include "TelemetryData.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <deque>
#include <functional>
#include <pqxx/pqxx>
#include <filesystem>
#include <GL/glew.h>
#include <algorithm>
#include <future>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

class ThreadPool {
public:
    ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = move(tasks.front());
                        tasks.pop_front();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            unique_lock<mutex> lock(queueMutex);
            tasks.emplace_back(forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (thread& worker : workers)
            worker.join();
    }

private:
    vector<thread> workers;
    deque<function<void()>> tasks;
    mutex queueMutex;
    condition_variable condition;
    bool stop;
};

static unique_ptr<ThreadPool> g_threadPool = nullptr;
static mutex g_poolMutex;

ThreadPool& getThreadPool() {
    lock_guard<mutex> lock(g_poolMutex);
    if (!g_threadPool) {
        g_threadPool = make_unique<ThreadPool>(4);
    }
    return *g_threadPool;
}

HeatmapStatus g_heatmap_status;
static unordered_map<string, GLuint> g_heatTile_cache;
static mutex g_heatCacheMtx;

struct DataPoint { double lat, lon, value; };
struct RGBA { uint8_t r, g, b, a; };

double lon_to_tile_x(double lon, int z) {
    return (lon + 180.0) / 360.0 * (1 << z);
}

double lat_to_tile_y(double lat, int z) {
    return (1.0 - asinh(tan(lat * M_PI / 180.0)) / M_PI) / 2.0 * (1 << z);
}

double tile_x_to_lon(double x, int z) {
    return x / (double)(1 << z) * 360.0 - 180.0;
}

double tile_y_to_lat(double y, int z) {
    double n = M_PI - 2.0 * M_PI * y / (double)(1 << z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

double haversine(double lat1, double lon1, double lat2, double lon2) {
    double R = 6371000.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat/2)*sin(dLat/2) +
               cos(lat1*M_PI/180.0)*cos(lat2*M_PI/180.0) *
               sin(dLon/2)*sin(dLon/2);
    return R * 2 * atan2(sqrt(a), sqrt(1-a));
}

void get_color_rsrp(double val, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, float alphaScale) {
    if (std::isnan(val) || val < -110.0) { r=0; g=0; b=0; a=0; return; }

    struct CP { double v; uint8_t r, g, b; };
    static const CP cps[] = {
        {-110, 0,   0,   139},
        {-100, 0,   255, 255},
        { -90, 255, 255, 0},    
        { -80, 255, 140, 0},
        { -60, 255, 0,   0},
    };

    const int N = sizeof(cps) / sizeof(cps[0]);
    if (val <= cps[0].v) { r=cps[0].r; g=cps[0].g; b=cps[0].b; a=(uint8_t)(220*alphaScale); return; }
    if (val >= cps[N-1].v){ r=cps[N-1].r; g=cps[N-1].g; b=cps[N-1].b; a=(uint8_t)(245*alphaScale); return; }

    for (int i = 1; i < N; ++i) {
        if (val <= cps[i].v) {
            double t = (val - cps[i-1].v) / (cps[i].v - cps[i-1].v);
            r = (uint8_t)(cps[i-1].r + t*(cps[i].r - cps[i-1].r));
            g = (uint8_t)(cps[i-1].g + t*(cps[i].g - cps[i-1].g));
            b = (uint8_t)(cps[i-1].b + t*(cps[i].b - cps[i-1].b));
            a = (uint8_t)(245*alphaScale);
            return;
        }
    }
    r=255; g=0; b=0; a=(uint8_t)(245*alphaScale);
}

void get_color_rsrq(double val, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, float alphaScale) {
    if (std::isnan(val) || val < -20.0) { r=0; g=0; b=0; a=0; return; }
    struct CP { double v; uint8_t r, g, b; };
    static const CP cps[] = {
        {-20, 0,   0,   139},
        {-15, 0,   255, 255},
        {-10, 255, 255, 0},
        { -6, 255, 140, 0},
        { -3, 255, 0,   0},
    };
    const int N = sizeof(cps)/sizeof(cps[0]);
    if (val <= cps[0].v) { r=cps[0].r; g=cps[0].g; b=cps[0].b; a=(uint8_t)(220*alphaScale); return; }
    if (val >= cps[N-1].v){ r=cps[N-1].r; g=cps[N-1].g; b=cps[N-1].b; a=(uint8_t)(245*alphaScale); return; }
    for (int i = 1; i < N; ++i) {
        if (val <= cps[i].v) {
            double t = (val - cps[i-1].v) / (cps[i].v - cps[i-1].v);
            r = (uint8_t)(cps[i-1].r + t*(cps[i].r - cps[i-1].r));
            g = (uint8_t)(cps[i-1].g + t*(cps[i].g - cps[i-1].g));
            b = (uint8_t)(cps[i-1].b + t*(cps[i].b - cps[i-1].b));
            a = (uint8_t)(245*alphaScale);
            return;
        }
    }
    r=255; g=0; b=0; a=(uint8_t)(245*alphaScale);
}

void get_color_rssi(double val, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, float alphaScale) {
    if (std::isnan(val) || val < -110.0) { r=0; g=0; b=0; a=0; return; }
    struct CP { double v; uint8_t r, g, b; };
    static const CP cps[] = {
        {-110, 0,   0,   139},
        { -95, 0,   255, 255},
        { -85, 255, 255, 0},
        { -70, 255, 140, 0},
        { -50, 255, 0,   0},
    };
    const int N = sizeof(cps)/sizeof(cps[0]);
    if (val <= cps[0].v) { r=cps[0].r; g=cps[0].g; b=cps[0].b; a=(uint8_t)(220*alphaScale); return; }
    if (val >= cps[N-1].v){ r=cps[N-1].r; g=cps[N-1].g; b=cps[N-1].b; a=(uint8_t)(245*alphaScale); return; }
    for (int i = 1; i < N; ++i) {
        if (val <= cps[i].v) {
            double t = (val - cps[i-1].v) / (cps[i].v - cps[i-1].v);
            r = (uint8_t)(cps[i-1].r + t*(cps[i].r - cps[i-1].r));
            g = (uint8_t)(cps[i-1].g + t*(cps[i].g - cps[i-1].g));
            b = (uint8_t)(cps[i-1].b + t*(cps[i].b - cps[i-1].b));
            a = (uint8_t)(245*alphaScale);
            return;
        }
    }
    r=255; g=0; b=0; a=(uint8_t)(245*alphaScale);
}

void get_color_altitude(double val, double minVal, double maxVal, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, float alphaScale) {
    if (std::isnan(val)) { r=0; g=0; b=0; a=0; return; }
    double ratio = (maxVal > minVal) ? (val - minVal) / (maxVal - minVal) : 0.5;
    ratio = std::clamp(ratio, 0.0, 1.0);
    struct CP { double t; uint8_t r, g, b; };
    static const CP cps[] = {
        {0.00, 10, 30, 150}, {0.35, 0, 220, 255}, {0.65, 255, 245, 60},
        {0.85, 255, 140, 0}, {1.00, 235, 20, 20}
    };
    for (int i = 1; i < 5; ++i) {
        if (ratio <= cps[i].t) {
            double u = (ratio - cps[i - 1].t) / (cps[i].t - cps[i - 1].t);
            r = (uint8_t)(cps[i - 1].r + u * (cps[i].r - cps[i - 1].r));
            g = (uint8_t)(cps[i - 1].g + u * (cps[i].g - cps[i - 1].g));
            b = (uint8_t)(cps[i - 1].b + u * (cps[i].b - cps[i - 1].b));
            a = (uint8_t)(245*alphaScale);
            return;
        }
    }
    r = cps[4].r; g = cps[4].g; b = cps[4].b; a = (uint8_t)(245*alphaScale);
}

void get_color(double val, double minVal, double maxVal, const string& criterion, float alphaScale, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if (criterion == "RSRP") get_color_rsrp(val, r, g, b, a, alphaScale);
    else if (criterion == "RSRQ") get_color_rsrq(val, r, g, b, a, alphaScale);
    else if (criterion == "RSSI") get_color_rssi(val, r, g, b, a, alphaScale);
    else get_color_altitude(val, minVal, maxVal, r, g, b, a, alphaScale);
}

double idw_interpolate(double lat, double lon, const vector<DataPoint>& pts, double searchRadius, double power) {
    double num = 0.0, den = 0.0;
    bool has = false;

    for (const auto& p : pts) {
        double dist = haversine(lat, lon, p.lat, p.lon);
        if (dist > searchRadius) continue;
        if (dist < 1.0) return p.value;
        double w = 1.0 / pow(dist, power);
        num += w * p.value;
        den += w;
        has = true;
    }

    return (has && den > 1e-9) ? num / den : numeric_limits<double>::quiet_NaN();
}

bool render_tile(const vector<DataPoint>& pts, double minLon, double maxLon, double minLat, double maxLat,
                 double minVal, double maxVal, const string& criterion, float alphaScale, float searchRadius, float idwPower,
                 vector<unsigned char>& out, int tileIdx, int tileTotal, atomic<bool>& canceled, atomic<int>& progress) {
    const int W = 256, H = 256;
    out.resize(W * H * 4);

    for (int i = 0; i < W * H; ++i) {
        out[i*4+0] = 8;
        out[i*4+1] = 22;
        out[i*4+2] = 130;
        out[i*4+3] = (unsigned char)(140 * alphaScale);
    }

    for (int py = 0; py < H; ++py) {
        int globalPct = (int)(100.0 * (tileIdx - 1 + py / (double)H) / tileTotal);
        progress.store(std::clamp(globalPct, 0, 99));

        if (canceled.load()) continue;

        double p_lat = maxLat - (py / (double)H) * (maxLat - minLat);

        for (int px = 0; px < W; ++px) {
            double p_lon = minLon + (px / (double)W) * (maxLon - minLon);
            double val = idw_interpolate(p_lat, p_lon, pts, searchRadius, idwPower);

            uint8_t r, g, b, a;
            get_color(val, minVal, maxVal, criterion, alphaScale, r, g, b, a);

            int idx = (py * W + px) * 4;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = a;
        }
    }
    return true;
}

bool save_png(const string& path, const vector<unsigned char>& img, int w, int h) {
    filesystem::create_directories(filesystem::path(path).parent_path());
    return stbi_write_png(path.c_str(), w, h, 4, img.data(), w * 4) != 0;
}


vector<DataPoint> collect_points(const string& db_conn, const HeatmapConfig& config) {
    vector<DataPoint> points;

    try {
        pqxx::connection c(db_conn);
        pqxx::nontransaction N(c);

        string field = "c.rsrp";
        if (config.criterion == "RSRQ") field = "c.rsrq";
        else if (config.criterion == "RSSI") field = "c.rssi";
        else if (config.criterion == "Altitude") field = "m.altitude";

        string query = "SELECT m.latitude, m.longitude, " + field + " as val, c.pci "
                       "FROM cell_data c JOIN measurements m ON c.measurement_id = m.id "
                       "WHERE c.earfcn = $1";

        if (!config.useAllPCIs && !config.selectedPCIs.empty()) {
            query += " AND c.pci IN (";
            for (size_t i = 0; i < config.selectedPCIs.size(); ++i) {
                if (i > 0) query += ",";
                query += to_string(config.selectedPCIs[i]);
            }
            query += ")";
        }

        pqxx::result res = N.exec_params(query, config.earfcn);

        for (auto const &row : res) {
            double v = row["val"].as<double>(0.0);
            double lat = row["latitude"].as<double>(0.0);
            double lon = row["longitude"].as<double>(0.0);

            if (v != 0.0 && lat != 0.0 && lon != 0.0) {
                if (config.criterion == "RSRP" && v <= -140) continue;
                points.push_back({lat, lon, v});
            }
        }
    } catch (const exception& e) {
        cerr << "[HEATMAP] DB ERROR: " << e.what() << endl;
    }

    return points;
}

vector<int> get_available_pcis(const string& db_conn, const string& earfcn) {
    vector<int> pcis;
    try {
        pqxx::connection c(db_conn);
        pqxx::nontransaction N(c);
        pqxx::result res = N.exec_params(
            "SELECT DISTINCT pci FROM cell_data WHERE earfcn = $1 AND pci IS NOT NULL AND pci > 0 ORDER BY pci",
            earfcn
        );
        for (auto const& row : res) {
            pcis.push_back(row[0].as<int>());
        }
    } catch (...) {}
    return pcis;
}

bool generate_heatmap_tiles(const string& db_conn, const HeatmapConfig& config) {
    g_heatmap_status.generating.store(true);
    g_heatmap_status.progress.store(0);
    {
        lock_guard<mutex> lock(g_heatmap_status.mtx);
        g_heatmap_status.message = "Сбор точек...";
    }

    auto points = collect_points(db_conn, config);
    if (points.empty()) {
        g_heatmap_status.generating.store(false);
        {
            lock_guard<mutex> lock(g_heatmap_status.mtx);
            g_heatmap_status.message = "Нет данных";
        }
        cerr << "[HEATMAP] ABORT: No data for EARFCN " << config.earfcn << endl;
        return false;
    }

    double minVal = 1e9, maxVal = -1e9;
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const auto& p : points) {
        minVal = min(minVal, p.value);
        maxVal = max(maxVal, p.value);
        minLat = min(minLat, p.lat);
        maxLat = max(maxLat, p.lat);
        minLon = min(minLon, p.lon);
        maxLon = max(maxLon, p.lon);
    }

    if (config.criterion == "RSRP") {
        minVal = -120.0;
        maxVal = -60.0;
    }

    double cLat = 0.5 * (minLat + maxLat);
    double latPad = config.searchRadiusMeters / 111320.0;
    double lonPad = config.searchRadiusMeters / max(1.0, 111320.0 * cos(cLat * M_PI / 180.0));
    double bboxMinLon = minLon - lonPad;
    double bboxMaxLon = maxLon + lonPad;
    double bboxMinLat = minLat - latPad;
    double bboxMaxLat = maxLat + latPad;

    int zoom = config.zoom;
    int minTX = (int)floor(lon_to_tile_x(bboxMinLon, zoom));
    int maxTX = (int)floor(lon_to_tile_x(bboxMaxLon, zoom));
    int minTY = (int)floor(lat_to_tile_y(bboxMaxLat, zoom));
    int maxTY = (int)floor(lat_to_tile_y(bboxMinLat, zoom));

    int worldTiles = (1 << zoom);
    minTX = clamp(minTX, 0, worldTiles - 1);
    maxTX = clamp(maxTX, 0, worldTiles - 1);
    minTY = clamp(minTY, 0, worldTiles - 1);
    maxTY = clamp(maxTY, 0, worldTiles - 1);

    int tileTotal = max(1, (maxTX - minTX + 1) * (maxTY - minTY + 1));

    {
        lock_guard<mutex> lock(g_heatmap_status.mtx);
        g_heatmap_status.message = "Генерация " + to_string(tileTotal) + " тайлов...";
    }

    atomic<bool> canceled{false};
    atomic<int> tilesGenerated{0};
    mutex localMutex;
    bool savedAny = false;

    auto processTile = [&](int tx, int ty) {
        if (canceled.load()) {
            tilesGenerated++;
            return;
        }

        double tMinLon = tile_x_to_lon(tx, zoom);
        double tMaxLon = tile_x_to_lon(tx + 1, zoom);
        double tMaxLat = tile_y_to_lat(ty, zoom);
        double tMinLat = tile_y_to_lat(ty + 1, zoom);
        double midLat = 0.5 * (tMinLat + tMaxLat);

        double latRad = (config.searchRadiusMeters * 1.5) / 111320.0;
        double lonRad = (config.searchRadiusMeters * 1.5) / max(1.0, 111320.0 * cos(midLat * M_PI / 180.0));

        vector<DataPoint> localPts;
        localPts.reserve(256);
        const int MAX_POINTS_PER_TILE = 2000;

        for (const auto& p : points) {
            if ((int)localPts.size() >= MAX_POINTS_PER_TILE) break;
            if (p.lat >= tMinLat - latRad && p.lat <= tMaxLat + latRad &&
                p.lon >= tMinLon - lonRad && p.lon <= tMaxLon + lonRad)
                localPts.push_back(p);
        }

        if (localPts.empty()) {
            tilesGenerated++;
            return;
        }

        vector<unsigned char> localImage(256 * 256 * 4);
        if (!render_tile(localPts, tMinLon, tMaxLon, tMinLat, tMaxLat, minVal, maxVal,
                        config.criterion, config.alpha, config.searchRadiusMeters, config.idwPower,
                        localImage, (ty - minTY) * (maxTX - minTX + 1) + (tx - minTX) + 1, tileTotal,
                        canceled, g_heatmap_status.progress)) {
            tilesGenerated++;
            return;
        }

        string path = "build/" + to_string(zoom) + "/" + to_string(tx) + "/" + to_string(ty) + ".png";

        {
            lock_guard<mutex> lock(localMutex);
            if (save_png(path, localImage, 256, 256)) {
                savedAny = true;
            }
        }

        tilesGenerated++;
    };

    for (int tx = minTX; tx <= maxTX; ++tx) {
        for (int ty = minTY; ty <= maxTY; ++ty) {
            getThreadPool().enqueue([&, tx, ty]() {
                processTile(tx, ty);
            });
        }
    }

    while (tilesGenerated.load() < tileTotal && !canceled.load()) {
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    this_thread::sleep_for(chrono::milliseconds(200));

    g_heatmap_status.generating.store(false);
    g_heatmap_status.progress.store(100);
    {
        lock_guard<mutex> lock(g_heatmap_status.mtx);
        g_heatmap_status.message = savedAny ? "Готово! Тайлов: " + to_string(tileTotal) : "Ошибка";
    }

    {
        lock_guard<mutex> lock(g_data.mtx);
        g_data.heatmap_min_lat = bboxMinLat;
        g_data.heatmap_max_lat = bboxMaxLat;
        g_data.heatmap_min_lon = bboxMinLon;
        g_data.heatmap_max_lon = bboxMaxLon;
        g_data.heatmap_earfcn = config.earfcn;
        g_data.heatmap_criterion = config.criterion;
        g_data.heatmap_zoom = zoom;
    }
    g_data.heatmap_ready = true;

    return savedAny;
}

void generate_heatmap_async(const string& db_conn, const HeatmapConfig& config) {
    thread([db_conn, config]() {
        generate_heatmap_tiles(db_conn, config);
    }).detach();
}

bool is_heatmap_generating() {
    return g_heatmap_status.generating.load();
}

int get_heatmap_progress() {
    return g_heatmap_status.progress.load();
}

string get_heatmap_message() {
    lock_guard<mutex> lock(g_heatmap_status.mtx);
    return g_heatmap_status.message;
}

GLuint load_heatmap_tile(int z, int x, int y) {
    string path = "build/" + to_string(z) + "/" + to_string(x) + "/" + to_string(y) + ".png";

    if (!filesystem::exists(path)) return 0;

    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return 0;

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    return texID;
}

void clear_heatmap_cache() {
    lock_guard<mutex> lock(g_heatCacheMtx);
    for (auto const& [path, texID] : g_heatTile_cache) {
        if (texID > 0) {
            GLuint id = texID;
            glDeleteTextures(1, &id);
        }
    }
    g_heatTile_cache.clear();
}

void generate_heatmap_thread(const string& db_conn, const string& criterion, const string& earfcn) {
    HeatmapConfig cfg;
    cfg.criterion = criterion;
    cfg.earfcn = earfcn;
    generate_heatmap_async(db_conn, cfg);
}

void reload_heatmap_texture() {
    cout << "[HEATMAP] reload_heatmap_texture is deprecated, use tile system" << endl;
}
