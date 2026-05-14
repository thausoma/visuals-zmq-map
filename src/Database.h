#ifndef DATABASE_H
#define DATABASE_H

#include <string>

void init_database();
void sync_all_data();
void save_packet(const std::string& raw_json);
void migrate_json_to_sql();

#endif