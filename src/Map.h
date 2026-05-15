#ifndef MAP_H
#define MAP_H

#include <GL/glew.h>

void render_map_window();

extern GLuint g_heatmap_texture;
void reload_heatmap_texture();

#endif