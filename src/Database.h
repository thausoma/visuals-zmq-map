#ifndef DATABASE_H
#define DATABASE_H

#include <string>
extern const std::string DB_CONN;
void init_database();
void sync_all_data();
void save_packet(const std::string& raw_json);
void migrate_json_to_sql();

extern const std::string DB_CONN;

#endif