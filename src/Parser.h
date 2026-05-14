#ifndef PARSER_H
#define PARSER_H

#include <string>

std::string find_val(std::string json_text, std::string key);
void parse_json_to_data(std::string raw);

#endif