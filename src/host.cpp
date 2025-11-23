#include "host.h"

Rule HostBridge::create_rule(const std::string &type) {
    uint32_t id = next_rule_id.fetch_add(1);
    return Rule{(uint16_t)1, id};
}
void HostBridge::release_rule(const Rule &r) {
    // noop for now
}
void HostBridge::register_function(const std::string &name, HostFn fn) {
    functions[name] = std::move(fn);
}
bool HostBridge::has_function(const std::string &name) const {
    return functions.find(name) != functions.end();
}
Value HostBridge::call_function(const std::string &name, const std::vector<Value> &args) const {
    auto it = functions.find(name);
    if(it != functions.end()) return it->second(args);
    return Value::make_nil();
}

// define the global instance
HostBridge GLOBAL_HOST;
