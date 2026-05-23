/*
FILE : vimms_lair.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Parses the raw HTML from Vimm's Lair to display search results and download games
*/


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "vimms_lair.h"

void free_game_list(GameList *list)
{
    if (!list)
        return;
    for (size_t i = 0; i < list->count; i++)
    {
        free(list->items[i].name);
        free(list->items[i].link);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void free_media_list(MediaList *list)
{
    if (!list)
        return;
    for (size_t i = 0; i < list->count; i++)
    {
        free(list->items[i].id);
        free(list->items[i].disc);
        free(list->items[i].version);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int push_game(GameList *list, const char *name, const char *link)
{
    if (list->count == list->capacity)
    {
        size_t new_capacity = (list->capacity == 0) ? 8 : list->capacity * 2;
        GameEntry *new_items = (GameEntry *)realloc(list->items, new_capacity * sizeof(GameEntry));
        if (!new_items)
        {
            return 0;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count].name = strdup(name);
    list->items[list->count].link = strdup(link);

    if (!list->items[list->count].name || !list->items[list->count].link)
    {
        free(list->items[list->count].name);
        free(list->items[list->count].link);
        return 0;
    }

    list->count++;
    return 1;
}

static int push_media(MediaList *list, const char *id, const char *disc, const char *version)
{
    if (list->count == list->capacity)
    {
        size_t new_capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        MediaEntry *new_items = (MediaEntry *)realloc(list->items, new_capacity * sizeof(MediaEntry));
        if (!new_items)
            return 0;

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count].id = strdup(id);
    list->items[list->count].disc = strdup(disc && *disc ? disc : "1");
    list->items[list->count].version = strdup(version && *version ? version : "1.0");

    if (!list->items[list->count].id ||
        !list->items[list->count].disc ||
        !list->items[list->count].version)
    {
        free(list->items[list->count].id);
        free(list->items[list->count].disc);
        free(list->items[list->count].version);
        return 0;
    }

    list->count++;
    return 1;
}

static int string_starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int strncase_equal(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (a[i] == '\0' || b[i] == '\0')
            return a[i] == b[i];
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static const char *find_case_insensitive(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);

    if (needle_len == 0)
        return haystack;

    for (const char *p = haystack; *p; p++)
    {
        if (strncase_equal(p, needle, needle_len))
            return p;
    }

    return NULL;
}

static const char *find_case_insensitive_until(const char *haystack, const char *end, const char *needle)
{
    size_t needle_len = strlen(needle);

    if (needle_len == 0)
        return haystack;

    for (const char *p = haystack; *p && p + needle_len <= end; p++)
    {
        if (strncase_equal(p, needle, needle_len))
            return p;
    }

    return NULL;
}

static char *substr_dup(const char *start, const char *end)
{
    if (!start || !end || end < start)
        return NULL;
    size_t len = (size_t)(end - start);
    char *s = (char *)malloc(len + 1);
    if (!s)
        return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static void html_entity_decode(char *s)
{
    char *src = s;
    char *dst = s;

    while (*src)
    {
        if (strncmp(src, "&amp;", 5) == 0)
        {
            *dst++ = '&';
            src += 5;
        }
        else if (strncmp(src, "&quot;", 6) == 0)
        {
            *dst++ = '"';
            src += 6;
        }
        else if (strncmp(src, "&#39;", 5) == 0)
        {
            *dst++ = '\'';
            src += 5;
        }
        else if (strncmp(src, "&#039;", 6) == 0)
        {
            *dst++ = '\'';
            src += 6;
        }
        else if (strncmp(src, "&lt;", 4) == 0)
        {
            *dst++ = '<';
            src += 4;
        }
        else if (strncmp(src, "&gt;", 4) == 0)
        {
            *dst++ = '>';
            src += 4;
        }
        else if (strncmp(src, "&nbsp;", 6) == 0)
        {
            *dst++ = ' ';
            src += 6;
        }
        else
        {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

static void strip_html_tags(char *s)
{
    char *src = s;
    char *dst = s;
    int in_tag = 0;

    while (*src)
    {
        if (*src == '<')
        {
            in_tag = 1;
            src++;
        }
        else if (*src == '>')
        {
            in_tag = 0;
            src++;
        }
        else if (!in_tag)
        {
            *dst++ = *src++;
        }
        else
        {
            src++;
        }
    }

    *dst = '\0';
}

static void trim_whitespace(char *s)
{
    if (!s || !*s)
        return;

    char *start = s;
    while (*start && isspace((unsigned char)*start))
    {
        start++;
    }

    if (start != s)
    {
        memmove(s, start, strlen(start) + 1);
    }

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
    {
        s[--len] = '\0';
    }
}

static int is_valid_game_href(const char *href)
{
    if (!href)
        return 0;
    if (strncmp(href, "/vault/", 7) != 0)
        return 0;

    const char *p = href + 7;
    if (!isdigit((unsigned char)*p))
        return 0;

    while (*p)
    {
        if (!isdigit((unsigned char)*p))
            return 0;
        p++;
    }

    return 1;
}

static char *extract_anchor_href(const char *anchor_start, const char *tag_end)
{
    const char *p = anchor_start + 2;

    while (p < tag_end)
    {
        while (p < tag_end && isspace((unsigned char)*p))
            p++;

        const char *name_start = p;
        while (p < tag_end &&
               (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':'))
        {
            p++;
        }

        if (p == name_start)
        {
            p++;
            continue;
        }

        size_t name_len = (size_t)(p - name_start);
        while (p < tag_end && isspace((unsigned char)*p))
            p++;

        if (p >= tag_end || *p != '=')
            continue;

        p++;
        while (p < tag_end && isspace((unsigned char)*p))
            p++;

        if (name_len != 4 || !strncase_equal(name_start, "href", 4))
        {
            while (p < tag_end && !isspace((unsigned char)*p))
                p++;
            continue;
        }

        char quote = 0;
        if (p < tag_end && (*p == '"' || *p == '\''))
        {
            quote = *p;
            p++;
        }

        const char *value_start = p;
        const char *value_end = p;

        if (quote)
        {
            while (value_end < tag_end && *value_end != quote)
                value_end++;
        }
        else
        {
            while (value_end < tag_end &&
                   !isspace((unsigned char)*value_end) &&
                   *value_end != '>')
            {
                value_end++;
            }
        }

        return substr_dup(value_start, value_end);
    }

    return NULL;
}

static char *extract_tag_attribute(const char *tag_start, const char *tag_end, const char *attribute_name)
{
    const char *p = tag_start + 1;
    size_t attribute_len = strlen(attribute_name);

    while (p < tag_end)
    {
        while (p < tag_end && isspace((unsigned char)*p))
            p++;

        const char *name_start = p;
        while (p < tag_end &&
               (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':'))
        {
            p++;
        }

        if (p == name_start)
        {
            p++;
            continue;
        }

        size_t name_len = (size_t)(p - name_start);
        while (p < tag_end && isspace((unsigned char)*p))
            p++;

        if (p >= tag_end || *p != '=')
            continue;

        p++;
        while (p < tag_end && isspace((unsigned char)*p))
            p++;

        char quote = 0;
        if (p < tag_end && (*p == '"' || *p == '\''))
        {
            quote = *p;
            p++;
        }

        const char *value_start = p;
        const char *value_end = p;

        if (quote)
        {
            while (value_end < tag_end && *value_end != quote)
                value_end++;
        }
        else
        {
            while (value_end < tag_end &&
                   !isspace((unsigned char)*value_end) &&
                   *value_end != '>')
            {
                value_end++;
            }
        }

        if (name_len == attribute_len &&
            strncase_equal(name_start, attribute_name, attribute_len))
        {
            return substr_dup(value_start, value_end);
        }

        p = value_end;
        if (quote && p < tag_end)
            p++;
    }

    return NULL;
}

static int append_region_part(char **region, const char *part)
{
    if (!part || !*part)
        return 1;

    size_t old_len = *region ? strlen(*region) : 0;
    size_t part_len = strlen(part);
    size_t join_len = old_len ? 3 : 0;
    char *combined = (char *)realloc(*region, old_len + join_len + part_len + 1);
    if (!combined)
        return 0;

    if (old_len)
        memcpy(combined + old_len, " + ", 3);
    memcpy(combined + old_len + join_len, part, part_len);
    combined[old_len + join_len + part_len] = '\0';
    *region = combined;
    return 1;
}

static char *extract_region_from_cell(const char *td_start, const char *td_end)
{
    char *region = NULL;
    const char *p = td_start;

    while ((p = find_case_insensitive_until(p, td_end, "<img")) != NULL)
    {
        const char *tag_end = strchr(p, '>');
        if (!tag_end || tag_end > td_end)
            break;

        char *title = extract_tag_attribute(p, tag_end, "title");
        if (title)
        {
            html_entity_decode(title);
            trim_whitespace(title);
            if (!append_region_part(&region, title))
            {
                free(title);
                free(region);
                return NULL;
            }
            free(title);
        }

        p = tag_end + 1;
    }

    if (!region)
    {
        region = substr_dup(td_start, td_end);
        if (!region)
            return NULL;

        strip_html_tags(region);
        html_entity_decode(region);
        trim_whitespace(region);
    }

    return region;
}

static int is_search_result_type(const char *type)
{
    return strcmp(type, "Addon") == 0 ||
           strcmp(type, "DLC") == 0 ||
           strcmp(type, "Title Update") == 0 ||
           strcmp(type, "Xbox Live Arcade") == 0 ||
           strcmp(type, "Xbox Live Archade") == 0;
}

static char *extract_type_from_cell(const char *td_start, const char *td_end)
{
    const char *p = td_start;

    while ((p = find_case_insensitive_until(p, td_end, "redBorder")) != NULL)
    {
        const char *tag_start = p;
        while (tag_start > td_start && *tag_start != '<')
            tag_start--;

        if (*tag_start != '<')
        {
            p += 9;
            continue;
        }

        const char *tag_end = strchr(tag_start, '>');
        if (!tag_end || tag_end > td_end)
            break;

        char *title = extract_tag_attribute(tag_start, tag_end, "title");
        if (title)
        {
            html_entity_decode(title);
            trim_whitespace(title);
            if (is_search_result_type(title))
                return title;
            free(title);
        }

        p = tag_end + 1;
    }

    return NULL;
}

static char *append_type_and_region_to_name(const char *name, const char *type, const char *region)
{
    size_t name_len = strlen(name);
    size_t type_len = (type && *type) ? strlen(type) : 0;
    size_t region_len = (region && *region) ? strlen(region) : 0;
    size_t display_len = name_len + (type_len ? type_len + 3 : 0) + (region_len ? region_len + 3 : 0);
    char *display_name = (char *)malloc(display_len + 1);
    if (!display_name)
        return NULL;

    memcpy(display_name, name, name_len);
    size_t offset = name_len;

    if (type_len)
    {
        memcpy(display_name + offset, " (", 2);
        offset += 2;
        memcpy(display_name + offset, type, type_len);
        offset += type_len;
        display_name[offset++] = ')';
    }

    if (region_len)
    {
        memcpy(display_name + offset, " (", 2);
        offset += 2;
        memcpy(display_name + offset, region, region_len);
        offset += region_len;
        display_name[offset++] = ')';
    }

    display_name[offset] = '\0';
    return display_name;
}

static int parse_game_anchor(const char *anchor_start, GameList *out, const char *type, const char *region)
{
    const char *tag_end = strchr(anchor_start, '>');
    if (!tag_end)
        return 1;

    char *link = extract_anchor_href(anchor_start, tag_end);
    if (!link)
        return 1;

    html_entity_decode(link);

    if (!is_valid_game_href(link))
    {
        free(link);
        return 1;
    }

    const char *text_start = tag_end + 1;
    const char *text_end = find_case_insensitive(text_start, "</a>");
    if (!text_end)
    {
        free(link);
        return 0;
    }

    char *name = substr_dup(text_start, text_end);
    if (!name)
    {
        free(link);
        return 0;
    }

    strip_html_tags(name);
    html_entity_decode(name);
    trim_whitespace(name);

    if (!*name)
    {
        free(name);
        free(link);
        return 1;
    }

    char *display_name = append_type_and_region_to_name(name, type, region);
    if (!display_name)
    {
        free(name);
        free(link);
        return 0;
    }

    if (*display_name && !push_game(out, display_name, link))
    {
        free(display_name);
        free(name);
        free(link);
        return 0;
    }

    free(display_name);
    free(name);
    free(link);
    return 1;
}

static int parse_result_row(const char *row_start, const char *row_end, GameList *out)
{
    const char *td_start = find_case_insensitive(row_start, "<td");
    if (!td_start || td_start >= row_end)
        return 1;

    const char *td_end = find_case_insensitive(td_start, "</td>");
    if (!td_end || td_end > row_end)
        td_end = row_end;

    char *type = extract_type_from_cell(td_start, td_end);

    char *region = NULL;
    if (td_end + 5 < row_end)
    {
        const char *region_td_start = find_case_insensitive(td_end + 5, "<td");
        if (region_td_start && region_td_start < row_end)
        {
            const char *region_td_end = find_case_insensitive(region_td_start, "</td>");
            if (!region_td_end || region_td_end > row_end)
                region_td_end = row_end;
            region = extract_region_from_cell(region_td_start, region_td_end);
            if (!region)
            {
                free(type);
                return 0;
            }
        }
    }

    const char *p = td_start;
    while ((p = find_case_insensitive(p, "<a")) != NULL && p < td_end)
    {
        if (!parse_game_anchor(p, out, type, region))
        {
            free(type);
            free(region);
            return 0;
        }

        const char *next = strchr(p, '>');
        if (!next || next >= td_end)
            break;
        p = next + 1;
    }

    free(type);
    free(region);
    return 1;
}

static int copy_digits(const char *start, char *out, size_t out_size)
{
    if (!start || !out || out_size == 0 || !isdigit((unsigned char)*start))
        return 0;

    size_t len = 0;
    while (isdigit((unsigned char)start[len]))
    {
        if (len + 1 >= out_size)
            return 0;
        len++;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

static int copy_json_string_field(const char *start, const char *end, const char *field, char *out, size_t out_size)
{
    const char *p = find_case_insensitive_until(start, end, field);
    if (!p || !out || out_size == 0)
        return 0;

    p += strlen(field);
    while (p < end && isspace((unsigned char)*p))
        p++;

    if (p >= end || *p != ':')
        return 0;

    p++;
    while (p < end && isspace((unsigned char)*p))
        p++;

    if (p >= end || *p != '"')
        return 0;

    p++;
    size_t len = 0;
    while (p + len < end && p[len] && p[len] != '"')
    {
        if (len + 1 >= out_size)
            return 0;
        len++;
    }

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int copy_json_number_field(const char *start, const char *end, const char *field, char *out, size_t out_size)
{
    const char *p = find_case_insensitive_until(start, end, field);
    if (!p)
        return 0;

    p += strlen(field);
    while (p < end && isspace((unsigned char)*p))
        p++;

    if (p >= end || *p != ':')
        return 0;

    p++;
    while (p < end && isspace((unsigned char)*p))
        p++;

    return copy_digits(p, out, out_size);
}

static int parse_media_id_from_media_variable(const char *html, char *mediaId, size_t mediaIdSize)
{
    const char *media = find_case_insensitive(html, "let media");
    if (!media)
        return 0;

    const char *array_start = strchr(media, '[');
    if (!array_start)
        return 0;

    const char *id = find_case_insensitive(array_start, "\"ID\"");
    if (!id)
        return 0;

    const char *p = id + 4;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != ':')
        return 0;

    p++;
    while (*p && isspace((unsigned char)*p))
        p++;

    return copy_digits(p, mediaId, mediaIdSize);
}

static int parse_media_ids_from_media_variable(const char *html, MediaList *out)
{
    const char *media = find_case_insensitive(html, "let media");
    if (!media)
        return 1;

    const char *array_start = strchr(media, '[');
    if (!array_start)
        return 1;

    const char *array_end = strstr(array_start, "];");
    if (!array_end)
        return 1;

    const char *p = array_start;
    while ((p = find_case_insensitive(p, "\"ID\"")) != NULL && p < array_end)
    {
        p += 4;
        while (*p && isspace((unsigned char)*p))
            p++;

        if (*p != ':')
            continue;

        p++;
        while (*p && isspace((unsigned char)*p))
            p++;

        char id[32];
        if (!copy_digits(p, id, sizeof(id)))
            return 0;

        const char *entry_end = find_case_insensitive(p, "\"ID\"");
        if (!entry_end || entry_end > array_end)
            entry_end = array_end;

        char disc[16] = "1";
        char version[32] = "1.0";
        copy_json_number_field(p, entry_end, "\"SortOrder\"", disc, sizeof(disc));
        if (!copy_json_string_field(p, entry_end, "\"VersionString\"", version, sizeof(version)))
            copy_json_string_field(p, entry_end, "\"Version\"", version, sizeof(version));

        if (!push_media(out, id, disc, version))
            return 0;
    }

    return 1;
}

static int parse_media_id_from_hidden_input(const char *html, char *mediaId, size_t mediaIdSize)
{
    const char *media_id = find_case_insensitive(html, "mediaId");
    while (media_id)
    {
        const char *value = find_case_insensitive(media_id, "value");
        if (!value || value - media_id > 300)
        {
            media_id = find_case_insensitive(media_id + 7, "mediaId");
            continue;
        }

        const char *p = value + 5;
        while (*p && isspace((unsigned char)*p))
            p++;

        if (*p != '=')
        {
            media_id = find_case_insensitive(media_id + 7, "mediaId");
            continue;
        }

        p++;
        while (*p && isspace((unsigned char)*p))
            p++;

        if (*p == '"' || *p == '\'')
            p++;

        if (copy_digits(p, mediaId, mediaIdSize))
            return 1;

        media_id = find_case_insensitive(media_id + 7, "mediaId");
    }

    return 0;
}

int parse_vimm_media_id(const char *html, char *mediaId, size_t mediaIdSize)
{
    if (!html || !mediaId || mediaIdSize == 0)
        return 0;

    mediaId[0] = '\0';

    if (parse_media_id_from_media_variable(html, mediaId, mediaIdSize))
        return 1;

    return parse_media_id_from_hidden_input(html, mediaId, mediaIdSize);
}

int parse_vimm_media_ids(const char *html, MediaList *out)
{
    if (!html || !out)
        return 0;

    if (!parse_media_ids_from_media_variable(html, out))
        return 0;

    if (out->count == 0)
    {
        char id[32];
        if (parse_media_id_from_hidden_input(html, id, sizeof(id)))
        {
            if (!push_media(out, id, "1", "1.0"))
                return 0;
        }
    }

    return 1;
}

int parse_vimm_search_results(const char *html, GameList *out)
{
    const char *p = find_case_insensitive(html, "Search results for");

    if (!p)
        p = html;

    const char *end = find_case_insensitive(p, "id=\"showFilterTable\"");
    if (!end)
        end = p + strlen(p);

    while ((p = find_case_insensitive(p, "<tr")) != NULL && p < end)
    {
        const char *row_end = find_case_insensitive(p, "</tr>");
        if (!row_end || row_end > end)
            break;

        if (!parse_result_row(p, row_end, out))
            return 0;

        p = row_end + 5;
    }

    return 1;
}
