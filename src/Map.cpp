#include "Map.h"
#include "TelemetryData.h"

#include <imgui.h>
#include <implot.h>
#include <GL/glew.h>
#include <curl/curl.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <thread>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

static unordered_map<string, GLuint> tile_cache;

double lon2tile(double lon, int z) { return (lon + 180.0) / 360.0 * (1 << z); }
double lat2tile(double lat, int z) { return (1.0 - asinh(tan(lat * M_PI / 180.0)) / M_PI) / 2.0 * (1 << z); }
double tilex2lon(int x, int z) { return x / (double)(1 << z) * 360.0 - 180.0; }
double tiley2lat(int y, int z) {
    double n = M_PI - 2.0 * M_PI * y / (double)(1 << z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

double calculate_distance(double lat1, double lon1, double lat2, double lon2) {
    double dy = (lat2 - lat1) * 111139.0;
    double dx = (lon2 - lon1) * 111139.0 * cos(lat1 * M_PI / 180.0);
    return sqrt(dx * dx + dy * dy);
}

GLuint LoadTexture(const char* filename) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data) return 0;

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return texture;
}


void thread_loader(int z, int x, int y) {
    string dir = "tiles/" + to_string(z) + "/" + to_string(x);
    filesystem::create_directories(dir); 
    string path = dir + "/" + to_string(y) + ".png";

    CURL *curl = curl_easy_init();
    if(curl) {
        FILE *fp = fopen(path.c_str(), "wb");
        if (fp) {
            string url = "https://tile.openstreetmap.org/" + to_string(z) + "/" + to_string(x) + "/" + to_string(y) + ".png";
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "TelemetryMonitor/1.0 thausoma");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                cerr << "[MAP] Curl failed: " << curl_easy_strerror(res) << endl;
            }
            fclose(fp);
        }
        curl_easy_cleanup(curl);
    }
}


void render_map_window() {
    ImGui::Begin("Live Map View");

    static bool curl_inited = false;
    if (!curl_inited) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_inited = true;
    }

    if (ImPlot::BeginPlot("##MainMap", ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("Longitude", "Latitude");
        ImPlot::SetupAxesLimits(82.8, 83.1, 54.9, 55.1, ImGuiCond_FirstUseEver);

        ImPlotRect limits = ImPlot::GetPlotLimits();
        
        int zoom = 15;
        double width = abs(limits.X.Max - limits.X.Min);
        if (width > 0) {
            zoom = (int)floor(log2(360.0 / width * 2));
            if (zoom < 1) zoom = 1;
            if (zoom > 18) zoom = 18; 
        }

        if (tile_cache.size() > 500) {
            for (auto const& [path, texID] : tile_cache) {
                if (texID > 0) {
                    GLuint id = texID;
                    glDeleteTextures(1, &id);
                }
            }
            tile_cache.clear();
        }


        if (limits.X.Min > -180 && limits.X.Max < 180 && limits.Y.Min > -90 && limits.Y.Max < 90) {
            int x_start = (int)floor(lon2tile(limits.X.Min, zoom));
            int x_end   = (int)floor(lon2tile(limits.X.Max, zoom));
            int y_start = (int)floor(lat2tile(limits.Y.Max, zoom)); 
            int y_end   = (int)floor(lat2tile(limits.Y.Min, zoom));

            if (abs(x_end - x_start) < 20 && abs(y_end - y_start) < 20) {
                for (int x = x_start; x <= x_end; ++x) {
                    for (int y = y_start; y <= y_end; ++y) {
                        string path = "tiles/" + to_string(zoom) + "/" + to_string(x) + "/" + to_string(y) + ".png";
                        GLuint texID = 0;

                        if (tile_cache.count(path)) {
                            texID = tile_cache[path];
                            if (texID == 0 && filesystem::exists(path) && filesystem::file_size(path) > 0) {
                                texID = LoadTexture(path.c_str());
                                tile_cache[path] = texID;
                            }
                        } else {
                            if (filesystem::exists(path) && filesystem::file_size(path) > 0) {
                                texID = LoadTexture(path.c_str());
                                tile_cache[path] = texID;
                            } else {
                                tile_cache[path] = 0;
                                thread(thread_loader, zoom, x, y).detach();
                            }
                        }

                        if (texID > 0) {
                            double l_lon = tilex2lon(x, zoom);
                            double r_lon = tilex2lon(x + 1, zoom);
                            double b_lat = tiley2lat(y + 1, zoom);
                            double t_lat = tiley2lat(y, zoom);
                            
                            ImPlot::PlotImage(path.c_str(), (void*)(intptr_t)texID, 
                                              ImPlotPoint(l_lon, b_lat), 
                                              ImPlotPoint(r_lon, t_lat));
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            
            static std::vector<double> filtered_x;
            static std::vector<double> filtered_y;
            static std::vector<double> filtered_t;
            
            filtered_x.clear();
            filtered_y.clear();
            filtered_t.clear();

            for (size_t i = 0; i < g_data.history_time.size(); ++i) {
                double t = g_data.history_time[i];
                if (t >= g_data.view_min_time && t <= g_data.view_max_time) {
                    filtered_x.push_back(g_data.history_lon[i]);
                    filtered_y.push_back(g_data.history_lat[i]);
                    filtered_t.push_back(t);
                }
            }

            if (!filtered_x.empty()) {
                ImDrawList* draw_list = ImPlot::GetPlotDrawList();

                if (filtered_x.size() > 1) {
                    for (size_t i = 0; i < filtered_x.size() - 1; ++i) {
                        double delta_t = filtered_t[i+1] - filtered_t[i];
                        
                        if (delta_t <= 25.0 && delta_t > 0) { // 25 секунд (были ли пакеты)
                            
                            double dist = calculate_distance(filtered_y[i], filtered_x[i], 
                                                           filtered_y[i+1], filtered_x[i+1]);
                            double velocity = dist / delta_t;

                            
                            if (velocity < 160.0) { // 160 м/с ~ 380 км/ч
                                ImVec2 p1 = ImPlot::PlotToPixels(ImPlotPoint(filtered_x[i], filtered_y[i]));
                                ImVec2 p2 = ImPlot::PlotToPixels(ImPlotPoint(filtered_x[i+1], filtered_y[i+1]));
                                draw_list->AddLine(p1, p2, IM_COL32(50, 100, 255, 200), 3.0f);
                            }
                        }
                    }
                }

                ImVec2 last_pos_px = ImPlot::PlotToPixels(ImPlotPoint(filtered_x.back(), filtered_y.back()));
                draw_list->AddCircleFilled(last_pos_px, 6.0f, IM_COL32(255, 255, 0, 255));
                draw_list->AddCircle(last_pos_px, 6.5f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
            }
        }

        ImPlot::EndPlot();
    }
    ImGui::End();
}