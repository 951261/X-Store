#ifndef IA_H
#define IA_H

#include <string>
#include <map>

bool IA_login();
std::string getAuthCookie();
std::string getUsernameCookie();

std::map<std::string, std::string> constructIAHeaders();

#endif