// header file to store all generic settings, buffer sizes, path lengths, etc

#ifndef SETTINGS_H
#define SETTINGS_H

#define CURRENT_VERSION "0.2.13"

// Verbose debugging
// #define VERBOSE_DEBUG

// Should be large enough (I hope)
#define MAX_TEXT_LENGTH 512


#define FATX_SAFE_FOLDER_NAME_LEN 40

#define COOKIE_AUTH_FILE "game:\\cookie.bin"
#define USERNAME_CACHE_FILE_PATH "game:\\login_cache\\email.encrypted.bin"
#define PASSWORD_CACHE_FILE_PATH "game:\\login_cache\\password.encrypted.bin"

#endif