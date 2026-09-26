#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <string>
#include <vector>

typedef struct
{
    std::string name;
    std::string downloadURL;
} GameEntry;

typedef struct
{
    std::string id;
    std::string disc;
    std::string version;
} MediaEntry;


std::vector<GameEntry> parse_JSON_search_results(const char* JSON_text_buffer, const std::string searchString);

#endif