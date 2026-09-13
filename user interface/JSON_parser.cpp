#include "JSON_parser.h"
#include "../updater/cJSON.h"
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

    cJSON *json = cJSON_Parse(JSON_text_buffer);

    if(json == NULL) {
        dprintf("JSON parsing failed with error : %s\n", cJSON_GetErrorPtr());
    }

    cJSON *gameData = NULL;
    cJSON_ArrayForEach(gameData, json)
    {
        cJSON *gameName = cJSON_GetObjectItemCaseSensitive(gameData, "game_name");
        if (cJSON_IsString(gameName) && (gameName->valuestring != NULL))
        {
            GameEntry game;
            game.name = gameName->valuestring;
            char* tmpStr = cJSON_PrintUnformatted(gameData);
            game.JSONData = tmpStr;
            cJSON_free(tmpStr);

            if(findStringIC(game.name, searchString) != true) {
                // if the search text is not found in the game name, then continue to the next entry
                continue;
            }
            
            cJSON *regions = cJSON_GetObjectItemCaseSensitive(gameData, "region");
            if (cJSON_GetArraySize(regions))
            {
                game.name += " (";

                cJSON *region;
                bool multiRegion = false;
                cJSON_ArrayForEach(region, regions)
                {
                    if(multiRegion) game.name += ", ";

                    if (cJSON_IsString(region) && (region->valuestring != NULL))
                    {
                        game.name += region->valuestring;
                    }

                    multiRegion = true;
                }
                game.name += ")";
            }

            cJSON *version = cJSON_GetObjectItemCaseSensitive(gameData, "version");
            if (cJSON_IsString(version) && (version->valuestring != NULL))
            {
                game.name += " v";
                game.name += version->valuestring;
            }

            cJSON *digitalType = cJSON_GetObjectItemCaseSensitive(gameData, "media_type"); // only applies to XBOX-360 Digital downloads (e.g. XBLA, DLC, Title Update, etc...)
            if (digitalType != NULL && cJSON_IsString(digitalType) && (digitalType->valuestring != NULL)) {
                game.name += " ";
                game.name += digitalType->valuestring;
            }

            gamesList.push_back(game); // append the game data to the vector
        }
    }

    cJSON_Delete(json); // clean up
    return gamesList;
}

// takes the list of game versions (e.g. disk 1 v1.0, disk 1 v1.1, etc...) and returns a vector of game disk versions
std::vector<MediaEntry> parse_JSON_disk_versions(const GameEntry game)
{
    std::vector<MediaEntry> mediaEntries;

    cJSON *json = cJSON_Parse(game.JSONData.c_str());

    cJSON *discs = cJSON_GetObjectItemCaseSensitive(json, "discs");
    cJSON *disc;
    cJSON_ArrayForEach(disc, discs) {
        MediaEntry mediaEntry;

        cJSON *downloadFields = cJSON_GetObjectItemCaseSensitive(disc, "download_fields");

        cJSON *discId = cJSON_GetObjectItemCaseSensitive(downloadFields, "media_id");
        cJSON *discNumber = cJSON_GetObjectItemCaseSensitive(disc, "disc");
        cJSON *discVesion = cJSON_GetObjectItemCaseSensitive(disc, "version");

        if (cJSON_IsString(discId) && (discId->valuestring != NULL)) mediaEntry.id = discId->valuestring;
        if (cJSON_IsString(discNumber) && (discNumber->valuestring != NULL)) mediaEntry.disc = discNumber->valuestring;
        if (cJSON_IsString(discVesion) && (discVesion->valuestring != NULL)) mediaEntry.version = discVesion->valuestring;
        
        mediaEntries.push_back(mediaEntry);
    }

    cJSON_Delete(json);
	return mediaEntries;
    
}



