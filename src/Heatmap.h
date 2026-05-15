#ifndef HEATMAP_H
#define HEATMAP_H

#include <string>

void generate_heatmap_thread(const std::string& db_conn, const std::string& criterion, const std::string& earfcn);


void reload_heatmap_texture();

#endif