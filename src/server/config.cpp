#include "config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>

using namespace std;

namespace
{
string trimCopy(const string& s)
{
    size_t begin = 0;
    while (begin < s.size() && isspace(static_cast<unsigned char>(s[begin])))
    {
        ++begin;
    }

    size_t end = s.size();
    while (end > begin && isspace(static_cast<unsigned char>(s[end - 1])))
    {
        --end;
    }

    return s.substr(begin, end - begin);
}
} // namespace

void loadEnvFile(const string& path)
{
    static once_flag once;
    call_once(once, [&]() {
        ifstream ifs(path);
        if (!ifs.is_open())
        {
            return;
        }

        string line;
        while (getline(ifs, line))
        {
            string trimmed = trimCopy(line);
            if (trimmed.empty() || trimmed[0] == '#')
            {
                continue;
            }

            if (trimmed.rfind("export ", 0) == 0)
            {
                trimmed = trimCopy(trimmed.substr(7));
            }

            size_t pos = trimmed.find('=');
            if (pos == string::npos)
            {
                continue;
            }

            string key = trimCopy(trimmed.substr(0, pos));
            string value = trimCopy(trimmed.substr(pos + 1));
            if (key.empty())
            {
                continue;
            }

            if (value.size() >= 2)
            {
                if ((value.front() == '"' && value.back() == '"') ||
                    (value.front() == '\'' && value.back() == '\''))
                {
                    value = value.substr(1, value.size() - 2);
                }
            }

            setenv(key.c_str(), value.c_str(), 0);
        }
    });
}

string getEnvOrDefault(const char* key, const string& defaultValue)
{
    const char* value = getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }
    return value;
}

int getEnvIntOrDefault(const char* key, int defaultValue)
{
    const char* value = getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }
    return atoi(value);
}
