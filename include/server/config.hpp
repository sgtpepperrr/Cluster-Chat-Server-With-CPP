#ifndef CHAT_CONFIG_H
#define CHAT_CONFIG_H

#include <string>

// Load key=value entries from .env into process environment (without overriding existing env vars).
void loadEnvFile(const std::string& path = ".env");

std::string getEnvOrDefault(const char* key, const std::string& defaultValue);
int getEnvIntOrDefault(const char* key, int defaultValue);

#endif // CHAT_CONFIG_H
