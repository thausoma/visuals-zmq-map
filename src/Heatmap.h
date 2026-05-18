#ifndef HEATMAP_H
#define HEATMAP_H

#include <string>
#include <vector>
#include <atomic>
#include <future>
#include <mutex>
#include <GL/glew.h>

struct HeatmapConfig {
    std::string criterion = "RSRP";
    std::string earfcn = "38100";
    float searchRadiusMeters = 35.0f;
    float idwPower = 2.0f;
    float alpha = 0.85f;
    bool useAllPCIs = true;
    std::vector<int> selectedPCIs;
    int zoom = 15;
    double bboxMinLon = 0, bboxMaxLon = 0, bboxMinLat = 0, bboxMaxLat = 0;
};

struct HeatmapStatus {
    std::atomic<bool> generating{false};
    std::atomic<int> progress{0};
    std::string message;
    std::mutex mtx;
};

extern HeatmapStatus g_heatmap_status;

void generate_heatmap_async(const std::string& db_conn, const HeatmapConfig& config);

bool generate_heatmap_tiles(const std::string& db_conn, const HeatmapConfig& config);

bool is_heatmap_generating();
int get_heatmap_progress();
std::string get_heatmap_message();

std::vector<int> get_available_pcis(const std::string& db_conn, const std::string& earfcn);

GLuint load_heatmap_tile(int z, int x, int y);

void clear_heatmap_cache();

double lon_to_tile_x(double lon, int z);
double lat_to_tile_y(double lat, int z);
double tile_x_to_lon(double x, int z);
double tile_y_to_lat(double y, int z);

#endif