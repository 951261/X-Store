#include "JSON_parser.h"
#include "../updater/cJSON.h"
#include "ui.h"
#include "OutputConsole.h"
#include <algorithm>
#include <string>
#include <cctype>

// https://stackoverflow.com/questions/3152241/case-insensitive-stdstring-find
/// Try to find in the Haystack the Needle - ignore case
bool findStringIC(const std::string & strHaystack, const std::string & strNeedle)
{
  auto it = std::search(
    strHaystack.begin(), strHaystack.end(),
    strNeedle.begin(),   strNeedle.end(),
    [](unsigned char ch1, unsigned char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
  );
  return (it != strHaystack.end() );
}

std::vector<GameEntry> parse_JSON_search_results(const char *JSON_text_buffer, const std::string searchString)
{
    std::vector<GameEntry> gamesList;
    std::string gameDownloadURLBase;

    cJSON *json = cJSON_Parse(JSON_text_buffer);

    if(json == NULL) {
        dprintf("JSON parsing failed with error : %s\n", cJSON_GetErrorPtr());
    }

    cJSON *files = cJSON_GetObjectItemCaseSensitive(json, "files");
    cJSON* alternate_locations = cJSON_GetObjectItemCaseSensitive(json, "alternate_locations");
    cJSON *workable_download_servers = cJSON_GetObjectItemCaseSensitive(alternate_locations, "workable");

    if(cJSON_IsArray(workable_download_servers)) {
        cJSON *first_workable_server = cJSON_GetArrayItem(workable_download_servers, 0);
        if(first_workable_server != NULL) {
            cJSON *server_domain = cJSON_GetObjectItemCaseSensitive(first_workable_server, "server");
            cJSON *dir_path = cJSON_GetObjectItemCaseSensitive(first_workable_server, "dir");
            
            if(cJSON_IsString(server_domain) && cJSON_IsString(dir_path) && server_domain->valuestring != NULL && dir_path->valuestring != NULL) {
                gameDownloadURLBase = std::string("https://") + std::string(server_domain->valuestring) + std::string(dir_path->valuestring);
            } else {
                log_printf("Server domain/path is not a string\n");
                cJSON_Delete(json); // clean up
                return gamesList;
            }
        } else {
            log_printf("workable is NULL\n");
            cJSON_Delete(json); // clean up
            return gamesList;
        }
    } else {
        log_printf("Workable is not an array\n");
        cJSON_Delete(json); // clean up
        return gamesList;
    }

    cJSON *fileName = NULL;
    cJSON_ArrayForEach(fileName, files)
    {
        cJSON *gameName = cJSON_GetObjectItemCaseSensitive(fileName, "name");
        cJSON *fileFormat = cJSON_GetObjectItemCaseSensitive(fileName, "format");
        if (cJSON_IsString(gameName) && (gameName->valuestring != NULL)
            && cJSON_IsString(fileFormat) && (fileFormat->valuestring != NULL))
        {
            GameEntry game;
            game.name = gameName->valuestring;
            game.downloadURL = gameDownloadURLBase + std::string("/") + UrlEncodeQuery(game.name); 

            if(findStringIC(game.name, searchString) != true || strcmp(fileFormat->valuestring, "ZIP") != 0) {
                // if the search text is not found in the game name, then continue to the next entry
                // Also, if the search result is in an unsupported format, continue
                continue;
            }

            gamesList.push_back(game); // append the game data to the vector
        }
    }

    cJSON_Delete(json); // clean up
    return gamesList;
}


