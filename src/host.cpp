#include "host.h"
#include "host_manifest.h"

Rule HostBridge::create_rule(const std::string &type)
{
    uint32_t id = next_rule_id.fetch_add(1);
    return Rule{(uint16_t)1, id};
}
void HostBridge::release_rule(const Rule &r)
{
    // TODO
}
void HostBridge::register_function(const std::string &name, HostFn fn)
{
    functions[name] = std::move(fn);
    HostManifest::register_name(name);
}
bool HostBridge::has_function(const std::string &name) const
{
    return functions.find(name) != functions.end();
}
Value HostBridge::call_function(const std::string &name, const std::vector<Value> &args) const
{
    auto it = functions.find(name);
    if(it != functions.end()) return it->second(args);
    return Value::make_nil();
}

HostBridge GLOBAL_HOST;
