#include <xtl.h>
#pragma comment(lib, "xboxkrnl.lib")

#include <string>
#include <map>
#include <sstream>
#include <vector>
#include <fstream>
#include <iterator>

#include "ia.h"

#include "downloadFile.h"
#include "OutputConsole.h"
#include "updater/cJSON.h"
#include "user interface/ui.h"
#include "settings.h"
#include "file-stuff.h"

enum XEKEY_OBFUSCATE
{
    XEKEY_OBFUSCATE_CONSOLE = 0,
    XEKEY_OBFUSCATE_ROAM    = 1
};

extern "C"
{
    // Kernel ordinal 596.
    // LONG represents the 32-bit NTSTATUS: negative means failure.
    NTSYSAPI LONG NTAPI XeKeysObfuscate(
        XEKEY_OBFUSCATE keySelector,
        const BYTE* input,
        DWORD inputSize,
        BYTE* output,
        DWORD* outputSize);

    // Kernel ordinal 597. Returns TRUE after successful authentication.
    NTSYSAPI BOOL NTAPI XeKeysUnObfuscate(
        XEKEY_OBFUSCATE keySelector,
        const BYTE* input,
        DWORD inputSize,
        BYTE* output,
        DWORD* outputSize);
}

static const DWORD CONSOLE_BLOB_OVERHEAD = 0x18;

static BOOL ConsoleBuffersOverlap(
    const void* a, DWORD aSize,
    const void* b, DWORD bSize)
{
    const UINT_PTR aAddress = reinterpret_cast<UINT_PTR>(a);
    const UINT_PTR bAddress = reinterpret_cast<UINT_PTR>(b);

    return aAddress <= bAddress
        ? bAddress - aAddress < aSize
        : aAddress - bAddress < bSize;
}

// Returns TRUE on success; bytesWritten is zero on failure.
// Requires nonempty input and a separate output buffer.
static BOOL EncryptForThisConsole(
    const void* plaintext,
    DWORD plaintextSize,
    void* ciphertext,
    DWORD ciphertextCapacity,
    DWORD* bytesWritten)
{
    if (!bytesWritten)
        return FALSE;
    *bytesWritten = 0;

    if (!plaintext || !ciphertext || plaintextSize == 0 ||
        plaintextSize > MAXDWORD - CONSOLE_BLOB_OVERHEAD)
        return FALSE;

    const DWORD required = plaintextSize + CONSOLE_BLOB_OVERHEAD;

    if (ciphertextCapacity < required ||
        ConsoleBuffersOverlap(
            plaintext, plaintextSize, ciphertext, required))
        return FALSE;

    DWORD actual = ciphertextCapacity;
    const LONG status = XeKeysObfuscate(
        XEKEY_OBFUSCATE_CONSOLE,
        static_cast<const BYTE*>(plaintext), plaintextSize,
        static_cast<BYTE*>(ciphertext), &actual);

    if (status < 0 || actual != required)
    {
        // The native routine may have copied plaintext into this buffer.
        SecureZeroMemory(ciphertext, required);
        return FALSE;
    }

    *bytesWritten = actual;
    return TRUE;
}

// Returns TRUE only after successful decryption and authentication.
// bytesWritten is zero on failure.
static BOOL DecryptForThisConsole(
    const void* ciphertext,
    DWORD ciphertextSize,
    void* plaintext,
    DWORD plaintextCapacity,
    DWORD* bytesWritten)
{
    if (!bytesWritten)
        return FALSE;
    *bytesWritten = 0;

    if (!ciphertext || !plaintext ||
        ciphertextSize <= CONSOLE_BLOB_OVERHEAD)
        return FALSE;

    const DWORD required = ciphertextSize - CONSOLE_BLOB_OVERHEAD;

    if (plaintextCapacity < required ||
        ConsoleBuffersOverlap(
            ciphertext, ciphertextSize, plaintext, required))
        return FALSE;

    DWORD actual = plaintextCapacity;
    const BOOL authenticated = XeKeysUnObfuscate(
        XEKEY_OBFUSCATE_CONSOLE,
        static_cast<const BYTE*>(ciphertext), ciphertextSize,
        static_cast<BYTE*>(plaintext), &actual);

    if (!authenticated || actual != required)
    {
        // Never expose unauthenticated plaintext.
        SecureZeroMemory(plaintext, required);
        return FALSE;
    }

    *bytesWritten = actual;
    return TRUE;
}


static bool EncryptConsoleText(
    const std::string& text,
    std::vector<BYTE>& ciphertext)
{
    ciphertext.clear();

    if (text.size() > MAXDWORD - CONSOLE_BLOB_OVERHEAD - 1)
        return false;

    // Include the terminating '\0'.
    const DWORD inputSize = static_cast<DWORD>(text.size()) + 1;
    std::vector<BYTE> result(inputSize + CONSOLE_BLOB_OVERHEAD);
    DWORD written = 0;

    if (!EncryptForThisConsole(
            text.c_str(), inputSize,
            &result[0], static_cast<DWORD>(result.size()), &written))
        return false;

    result.resize(written);
    ciphertext.swap(result);
    return true;
}

static bool DecryptConsoleText(
    const std::vector<BYTE>& ciphertext,
    std::string& text)
{
    text.clear();

    if (ciphertext.size() <= CONSOLE_BLOB_OVERHEAD ||
        ciphertext.size() > MAXDWORD)
        return false;

    const DWORD outputSize =
        static_cast<DWORD>(ciphertext.size()) - CONSOLE_BLOB_OVERHEAD;

    std::string result(outputSize, '\0');
    DWORD written = 0;

    // The binary helper erases output if authentication fails.
    if (!DecryptForThisConsole(
            &ciphertext[0], static_cast<DWORD>(ciphertext.size()),
            &result[0], outputSize, &written))
        return false;

    if (written == 0 || result[written - 1] != '\0')
    {
        SecureZeroMemory(&result[0], result.size());
        return false;
    }

    result.resize(written - 1); // Remove the stored terminator.
    text.swap(result);
    return true;
}


static std::vector<std::string> stringSplit(std::string const& string, const char splitterChar) {
    std::stringstream stringStream(string);
    std::string segment;
    std::vector<std::string> stringList;

    while(std::getline(stringStream, segment, splitterChar))
    {
        stringList.push_back(segment);
    }

    return stringList;
}

static std::string substringBeforeFirst(std::string const& string, char charToSplitAt)
{
    std::string::size_type pos = string.find(charToSplitAt);
    if (pos != std::string::npos)
    {
        return string.substr(0, pos);
    }
    else
    {
        return string;
    }
}

static std::string substringAfterFirst(std::string const& string, char charToSplitAt)
{
    std::string::size_type pos = string.find(charToSplitAt);
    if (pos != std::string::npos)
    {
        return string.substr(pos + 1);
    }
    else
    {
        return string;
    }
}

static std::string getCookie(std::string const& cookieHeaderData, std::string const& desiredCookieName) {
    const std::vector<std::string> cookieList = stringSplit(cookieHeaderData, '\n');

    for(int i = 0; i < cookieList.size(); i++) {
        const std::string mainCookie = substringBeforeFirst(cookieList[i], ';');

        const std::string cookieName = substringBeforeFirst(mainCookie, '=');
        const std::string cookieValue = substringAfterFirst(mainCookie, '=');

        if(cookieName == desiredCookieName) {
            return cookieValue;
        }
    }

    return std::string();
}

static std::pair<std::string, std::string> get_csrf_token() {
    const std::string URL = "https://archive.org/services/csrf-token";
    
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    std::string returnToken;
    std::string returnCookie;

    const HttpResponseInfo httpStatus = HTTP_GET(URL, headers);

    if(httpStatus.status_code != 200) {
        dprintf("Failed to GET %s\n", URL.c_str());
        dprintf("%s\n", httpStatus.error_message.c_str());
        return std::make_pair(returnToken, returnCookie);
    }

    if(httpStatus.headers.count("set-cookie") <= 0) {
        dprintf("Failed to find cookies\n");
        return std::make_pair(returnToken, returnCookie);
    }

	returnCookie = getCookie(httpStatus.headers.find("set-cookie")->second, "ia-csrf");

    cJSON *json = cJSON_Parse(httpStatus.body.c_str());
    cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "success");

    if(!cJSON_IsTrue(status)) {
        dprintf("Failed to get CSRF token\n");
        cJSON_Delete(json);
        return std::make_pair(returnToken, returnCookie);
    }

    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "value");
    cJSON *token = cJSON_GetObjectItemCaseSensitive(value, "token");

    if(cJSON_IsString(token) && token->valuestring != NULL) {
        returnToken.assign(token->valuestring);
    } else {
        dprintf("Failed to parse token\n");
    }

    cJSON_Delete(json);
    return std::make_pair(returnToken, returnCookie);
}

static std::string generateLoginPayload(const std::string& email, const std::string& password, const std::string& token) {
    return "{\"username\":\"" + email + "\",\"password\":\"" + password + "\",\"remember\":\"true\",\"t\":\"" + token + "\"}";
}

// Source - https://stackoverflow.com/q/40052857
// Posted by Eduardo Lúcio, modified by community. See post 'Timeline' for change history
// Retrieved 2026-10-04, License - CC BY-SA 3.0

bool writeFileBytesFromVector(const char* filename, std::vector<unsigned char> const& fileBytes){
    std::ofstream file(filename, std::ios::out|std::ios::binary);
    if(!file.is_open()) {
        return false;
    }
    std::copy(fileBytes.cbegin(), fileBytes.cend(),
        std::ostream_iterator<unsigned char>(file));
    return true;
}

std::vector<unsigned char> readFileBytesToVector(const char* filename) {

    // open the file:
    std::ifstream file(filename, std::ios::binary);

    if(!file.is_open()) {
        return std::vector<unsigned char>();
    }

    // Stop eating new lines in binary mode!!!
    file.unsetf(std::ios::skipws);

    // get its size:
    std::streampos fileSize;

    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if(static_cast<std::streamoff>(fileSize) < 0) {
        return std::vector<unsigned char>();
    }

    // reserve capacity
    std::vector<unsigned char> vec;
    vec.reserve(fileSize);

    // read the data:
    vec.insert(vec.begin(),
               std::istream_iterator<BYTE>(file),
               std::istream_iterator<BYTE>());

    return vec;

}


bool saveAuthCookie(std::string cookie) {
    std::vector<BYTE> encryptedAuthCookie;

    if(!EncryptConsoleText(cookie, encryptedAuthCookie)) {
        dprintf("Cookie encryption failed\n");
        return false;
    }

    return writeFileBytesFromVector(COOKIE_AUTH_FILE, encryptedAuthCookie);
}

void cacheLoginCredentials(std::string email, std::string password) {
    std::vector<unsigned char> encryptedEmail;
    std::vector<unsigned char> encryptedPassword;
    
    if(!EncryptConsoleText(email, encryptedEmail) || !EncryptConsoleText(password, encryptedPassword)) {
        dprintf("Failed to encrypt username/password\n");
        return;
    }

    customForceMkdir("game:\\login_cache");

    writeFileBytesFromVector(USERNAME_CACHE_FILE_PATH, encryptedEmail);
    writeFileBytesFromVector(PASSWORD_CACHE_FILE_PATH, encryptedPassword);
}

std::string loadLoginUsernameCache() {
    std::vector<unsigned char> encryptedEmail = readFileBytesToVector(USERNAME_CACHE_FILE_PATH);
    std::string email;

    if(encryptedEmail.size() < 10) {
        return email;
    }

    DecryptConsoleText(encryptedEmail, email);

    return email;
}

std::string loadLoginPasswordCache() {
    std::vector<unsigned char> encryptedPassword = readFileBytesToVector(PASSWORD_CACHE_FILE_PATH);
    std::string password;

    if(encryptedPassword.size() < 10) {
        return password;
    }

    DecryptConsoleText(encryptedPassword, password);

    return password;
}

bool fileExists(const std::string path) {
    log_printf("Searching for file %s\n", path.c_str());
    std::ifstream file(path);

    if (!file.is_open()) {
        log_printf("Failed to open file %s.\n", path.c_str());
        return false;
    }
    file.close();

    return true;
}

bool IA_login() {
    const std::string loginURL = "https://archive.org/services/account/login/";

    std::pair<std::string, std::string> CSRF_token_and_cookie = get_csrf_token();
    std::string CSRF_token = CSRF_token_and_cookie.first;
    std::string CSRF_cookie = CSRF_token_and_cookie.second;

    if(CSRF_token.empty()) {
        dprintf("Failed to get CSRF token\n");
        return false;
    }

    if(CSRF_cookie.empty()) {
        dprintf("Failed to get CSRF cookie\n");
        return false;
    }

    std::string username;
    std::string password;
    std::string usernameDefault = "";
    std::string passwordDefault = "";

    if(fileExists(USERNAME_CACHE_FILE_PATH)) {
        usernameDefault = loadLoginUsernameCache();
    }

    if(fileExists(PASSWORD_CACHE_FILE_PATH)) {
        passwordDefault = loadLoginPasswordCache();
    }

    std::wstring usernameDefaultW = std::wstring(usernameDefault.begin(), usernameDefault.end());
    std::wstring passwordDefaultW = std::wstring(passwordDefault.begin(), passwordDefault.end());
    

    if(OpenKeyboardToString(0, &username, L"Login email", L"Enter your Internet Archive account email address", usernameDefaultW.c_str()) != ERROR_SUCCESS) {
        dprintf("Failed to get Username\n");
        return false;
    }

    // Wait for keyboard to close
    Sleep(1500);

    if(OpenKeyboardToString(0, &password, L"Login password", L"Enter your Internet Archive account password", passwordDefaultW.c_str()) != ERROR_SUCCESS) {
        dprintf("Failed to get Password\n");
        return false;
    }

    std::string payload = generateLoginPayload(username, password, CSRF_token);

    std::map<std::string, std::string> headers;
    headers["Cookie"] = "ia-csrf=" + CSRF_cookie;
    headers["Accept"] = "application/json";
    headers["content-type"] = "application/json";
    headers["X-CSRF-Token"] = CSRF_token;
    headers["Origin"] = "https://archive.org";
    headers["Referer"] = "https://archive.org/login";

    HttpResponseInfo response = HTTP_POST(loginURL, headers, payload);

    if(response.status_code != 200) {
        dprintf("Failed to login, %s", response.error_message.c_str());
        return false;
    }

    dprintf("Logged into the Internet Archive succesfully\n");

    if(!saveAuthCookie(getCookie(response.headers["set-cookie"], "logged-in-sig"))) {
        dprintf("Failed to save auth cookie\n");
    }

    cacheLoginCredentials(username, password);

    return true;
}

std::string getAuthCookie() {
    std::vector<unsigned char> encryptedAuthCookie = readFileBytesToVector(COOKIE_AUTH_FILE);
    std::string authCookie;

    if(encryptedAuthCookie.empty() || !DecryptConsoleText(encryptedAuthCookie, authCookie)) {
        return std::string();
    }

    return authCookie;
}

std::string getUsernameCookie() {
    std::string username = loadLoginUsernameCache();
    return UrlEncodeQuery(username);
}

std::map<std::string, std::string> constructIAHeaders() {
    std::map<std::string, std::string> headers;
    std::string usernameCookie = getUsernameCookie();

    if (S3KeysExists()) {
        auto s3keys = getS3KeysFromFile();
        headers["Authorization"] = "LOW " + s3keys.first + ":" + s3keys.second;
    }

    if(!usernameCookie.empty() && !getAuthCookie().empty()) {
        headers["referer"] = "https://archive.org/";
        headers["cookie"] = "logged-in-sig=" + getAuthCookie() + "; logged-in-user=" + usernameCookie;
    } else if(!S3KeysExists()) {
        // No authentication method found
        return std::map<std::string, std::string>();
    }
    
    return headers;
}

std::pair<std::string, std::string> getS3KeysFromFile() {
    std::pair<std::string, std::string> keys;

    // Open the text file for reading
    std::ifstream f(IA_S3_KEYS_FILE_PATH);

    // Check if the file was opened successfully
    if (!f.is_open()) {
        log_printf("Note: failed to open " IA_S3_KEYS_FILE_PATH " file!\n");
        return keys;
    }

    std::string s;

    // Read each line from the file
    if(!std::getline(f, s)) {
        dprintf("Error: failed to read first line from " IA_S3_KEYS_FILE_PATH " file");
        return keys;
    }
    keys.first = s;

    // Read each line from the file
    if(!std::getline(f, s)) {
        dprintf("Error: failed to read second line from " IA_S3_KEYS_FILE_PATH " file");
        keys = std::pair<std::string, std::string>();
        return keys;
    }
    keys.second = s;

    // Close the file
    f.close();

    return keys;
}

bool S3KeysExists() {
    auto keys = getS3KeysFromFile();
    return (!keys.first.empty()) && (!keys.second.empty());
}
