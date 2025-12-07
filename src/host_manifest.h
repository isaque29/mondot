#ifndef MONDOT_HOST_MANIFEST_H
#define MONDOT_HOST_MANIFEST_H

#include <unordered_set>
#include <string>

struct HostManifest
{
    static inline std::unordered_set<std::string> names;

    static void register_name(const std::string &n)
    {
        names.insert(n);
    }
    static bool has(const std::string &n)
    {
        return names.find(n) != names.end();
    }
};

#endif
