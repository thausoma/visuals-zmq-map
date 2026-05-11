#ifndef DATABASE_H
#define DATABASE_H

#include <string>

void load_history();

void migrate_json_to_sql();

void load_from_sql();

#endif