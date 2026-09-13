#ifndef VIMMS_LAIR_H
#define VIMMS_LAIR_H

#include <string.h>






typedef struct
{
    MediaEntry *items;
    size_t count;
    size_t capacity;
} MediaList;


int parse_vimm_search_results(const char *html, GameList *out);
int parse_vimm_media_ids(const char *html, MediaList *out);
int parse_vimm_media_id(const char *html, char *mediaId, size_t mediaIdSize);
void free_game_list(GameList *list);
void free_media_list(MediaList *list);


#endif
