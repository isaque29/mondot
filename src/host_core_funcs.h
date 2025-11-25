#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <charconv>
#include <random>
#include <chrono>
#include <iostream>
#include <limits>
#include "value.h"
#include "host.h"
#include <thread>
#include <mutex>

namespace mondot_host
{
    static inline std::mutex io_mtx;

    static inline void format_value_to_string(const Value &v, std::string &out)
    {
        switch (v.tag) {
            case Tag::Number: {
                // buffer for double -> text
                char buf[64];
                int n = std::snprintf(buf, sizeof(buf), "%.15g", v.num);
                if (n > 0) out.append(buf, (size_t)n);
                else out.append("<badnum>");
                break;
            }
            case Tag::String: {
                out.append(*v.s);
                break;
            }
            case Tag::Boolean: {
                out.append(v.boolean ? "true" : "false");
                break;
            }
            case Tag::Nil: {
                out.append("nil");
                break;
            }
            default: {
                out.append("<val>");
                break;
            }
        }
    }

    static inline std::string fast_to_string(const Value &v)
    {
        switch (v.tag) {
            case Tag::Number: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%.15g", v.num);
                return std::string(buf, buf + n);
            }
            case Tag::String:
                return *v.s;
            case Tag::Boolean:
                return v.boolean ? "true" : "false";
            case Tag::Nil:
                return "nil";
            default:
                return "<val>";
        }
    }

    static inline void fast_print_multi(const std::vector<Value> &args, bool add_newline, bool do_flush)
    {
        std::string buf;
        if (args.empty() && add_newline)
            buf = "nil\n";
        else
        {
            buf.reserve(args.size() * 16 + 32);
            for (size_t i = 0; i < args.size(); ++i)
            {
                format_value_to_string(args[i], buf);
                if (i + 1 < args.size())
                    buf.push_back(' ');
            }
            if (add_newline) buf.push_back('\n');
        }

        {
            std::lock_guard<std::mutex> lk(io_mtx);
            std::cout.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            if (do_flush)
            {
                std::cout.flush();
                std::cerr.flush();
                std::fflush(nullptr);
            }
        }
    }

    static inline void register_core_host_functions(HostBridge &host)
    {
        // io.print : fast + flush
    host.register_function("io.print", [](const std::vector<Value> &args)->Value
    {
        fast_print_multi(args, /*add_newline=*/true, /*do_flush=*/true);
        return Value::make_nil();
    });

    // io.println : fast + flush
    host.register_function("io.println", [](const std::vector<Value> &args)->Value
    {
        fast_print_multi(args, /*add_newline=*/true, /*do_flush=*/true);
        return Value::make_nil();
    });

    // io.write : write without newline, no flush
    host.register_function("io.write", [](const std::vector<Value> &args)->Value
    {
        if (args.empty()) return Value::make_nil();
        std::string s;
        format_value_to_string(args[0], s);
        {
            std::lock_guard<std::mutex> lk(io_mtx);
            std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
        return Value::make_nil();
    });

    // io.writeln: write one value + newline (no extra flush)
    host.register_function("io.writeln", [](const std::vector<Value> &args)->Value
    {
        if (args.empty())
        {
            std::lock_guard<std::mutex> lk(io_mtx);
            std::cout.put('\n');
            return Value::make_nil();
        }
        
        std::string s;
        format_value_to_string(args[0], s);
        s.push_back('\n');
        {
            std::lock_guard<std::mutex> lk(io_mtx);
            std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
        return Value::make_nil();
    });

    // io.flush: explicit flush
    host.register_function("io.flush", [](const std::vector<Value> &args)->Value
    {
        std::lock_guard<std::mutex> lk(io_mtx);
        std::cout.flush();
        std::cerr.flush();
        std::fflush(nullptr);
        return Value::make_nil();
    });

    // io.set_auto_flush(on)
    host.register_function("io.set_auto_flush", [](const std::vector<Value> &args)->Value
    {
        bool on = false;
        if (!args.empty() && args[0].tag == Tag::Number) on = (args[0].num != 0.0);
        if (on)
        {
            std::cout.setf(std::ios::unitbuf);
            std::cerr.setf(std::ios::unitbuf);
        }
        else
        {
            std::cout.unsetf(std::ios::unitbuf);
            std::cerr.unsetf(std::ios::unitbuf);
        }
        return Value::make_nil();
    });

    // io.flush_and_exit(code)
    host.register_function("io.flush_and_exit", [](const std::vector<Value> &args)->Value
    {
        int code = 0;
        if (!args.empty() && args[0].tag == Tag::Number) code = static_cast<int>(args[0].num);
        
        {
            std::lock_guard<std::mutex> lk(io_mtx);
            std::cout.flush();
            std::cerr.flush();
            std::fflush(nullptr);
        }
        std::exit(code);
        return Value::make_nil();
    });

        host.register_function("strlen", [](const std::vector<Value> &args)->Value
        {
            if (!args.empty() && args[0].tag == Tag::String)
                return Value::make_number(static_cast<double>(args[0].s->size()));
            return Value::make_number(0.0);
        });

        // idea
        // host.register_function("len", [](const std::vector<Value> &args)->Value {
        //     if (!args.empty() && args[0].tag == Tag::String)
        //         return Value::make_number(static_cast<double>(args[0].s->size()));
        //     if (!args.empty() && args[0].tag == Tag::Array) {
        //         // if you have Tag::Array and vector in Value, adapt accordingly
        //         return Value::make_number(static_cast<double>(args[0].arr->size()));
        //     }
        //     return Value::make_number(0.0);
        // });

        host.register_function("str_char_at", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::String && args[1].tag == Tag::Number)
            {
                int idx = static_cast<int>(args[1].num);
                const std::string &s = *args[0].s;
                if (idx >= 0 && idx < static_cast<int>(s.size()))
                {
                    std::string r;
                    r.reserve(1);
                    r.push_back(s[idx]);
                    return Value::make_string(std::move(r));
                }
            }
            return Value::make_string(std::string());
        });

        // host.register_function("not", [](const std::vector<Value> &args)->Value
        // {
        //     if (!args.empty() && args[0].tag == Tag::Boolean)
        //         return Value::make_boolean(!(args[0].boolean));
        //     return Value::make_boolean(true); // TODO: KEEP LIKE THIS?
        // });

        host.register_function("add", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2)
            {
                const Value &a = args[0];
                const Value &b = args[1];
                if (a.tag == Tag::Number && b.tag == Tag::Number)
                    return Value::make_number(a.num + b.num);
                if (a.tag == Tag::String && b.tag == Tag::String)
                {
                    const std::string &sa = *a.s;
                    const std::string &sb = *b.s;
                    std::string out;
                    out.reserve(sa.size() + sb.size());
                    out.append(sa);
                    out.append(sb);
                    return Value::make_string(std::move(out));
                }
                std::string out = fast_to_string(a);
                out.append(fast_to_string(b));
                return Value::make_string(std::move(out));
            }
            return Value::make_number(0.0);
        });

        host.register_function("sub", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
                return Value::make_number(args[0].num - args[1].num);
            return Value::make_number(0.0);
        });

        host.register_function("mul", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
                return Value::make_number(args[0].num * args[1].num);
            return Value::make_number(0.0);
        });

        host.register_function("div", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
            {
                double d = args[1].num;
                if (d != 0.0) return Value::make_number(args[0].num / d);
            }
            return Value::make_number(0.0);
        });

        host.register_function("lt", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
                return Value::make_number(args[0].num < args[1].num ? 1.0 : 0.0);
            return Value::make_number(0.0);
        });
        host.register_function("gt", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
                return Value::make_number(args[0].num > args[1].num ? 1.0 : 0.0);
            return Value::make_number(0.0);
        });
        host.register_function("eq", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2) {
                const Value &a = args[0], &b = args[1];
                if (a.tag != b.tag) return Value::make_number(0.0);
                switch (a.tag) {
                    case Tag::Number: return Value::make_number(a.num == b.num ? 1.0 : 0.0);
                    case Tag::String: return Value::make_number((*a.s == *b.s) ? 1.0 : 0.0);
                    case Tag::Boolean:   return Value::make_number(a.boolean == b.boolean ? 1.0 : 0.0);
                    case Tag::Nil:    return Value::make_number(1.0); // nil == nil
                    default: return Value::make_number(0.0);
                }
            }
            return Value::make_number(0.0);
        });

        host.register_function("neq", [](const std::vector<Value> &args)->Value
        {
            if (args.size() < 2) return Value::make_number(0.0);
            const Value &a = args[0];
            const Value &b = args[1];
            if (a.tag != b.tag) return Value::make_number(1.0);
            switch (a.tag)
            {
                case Tag::Number: return Value::make_number(a.num != b.num ? 1.0 : 0.0);
                case Tag::String: return Value::make_number((*a.s != *b.s) ? 1.0 : 0.0);
                case Tag::Boolean:   return Value::make_number(a.boolean != b.boolean ? 1.0 : 0.0);
                case Tag::Nil:    return Value::make_number(0.0); // nil != nil -> false
                default:          return Value::make_number(1.0); // treat other types as not equal
            }
        });

        // bit shifts (integer semantics via cast), fast path for numbers
        host.register_function("shift", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
            {
                int64_t a = static_cast<int64_t>(args[0].num);
                int64_t b = static_cast<int64_t>(args[1].num);
                if (b >= 0 && b < 63) return Value::make_number(static_cast<double>(a << b));
            }
            return Value::make_number(0.0);
        });

        host.register_function("bitwise", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::Number && args[1].tag == Tag::Number)
            {
                int64_t a = static_cast<int64_t>(args[0].num);
                int64_t b = static_cast<int64_t>(args[1].num);
                if (b >= 0 && b < 63) return Value::make_number(static_cast<double>(a >> b));
            }
            return Value::make_number(0.0);
        });

        host.register_function("to_string", [](const std::vector<Value> &args)->Value
        {
            if (args.empty()) return Value::make_string(std::string("nil"));
            return Value::make_string(fast_to_string(args[0]));
        });
    }

    static inline void register_extra_host_functions(HostBridge &host)
    {
        // input: read line from stdin (blocking) - returns string
        host.register_function("input", [](const std::vector<Value> &args)->Value
        {
            std::string line;
            if (!std::getline(std::cin, line)) return Value::make_string(std::string());
            return Value::make_string(std::move(line));
        });

        // sleep_ms
        host.register_function("sleep_ms", [](const std::vector<Value> &args)->Value
        {
            if (!args.empty() && args[0].tag == Tag::Number)
            {
                int ms = static_cast<int>(args[0].num);
                if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
            return Value::make_nil();
        });

        // time_ms - current time in milliseconds since epoch
        host.register_function("time_ms", [](const std::vector<Value> &args)->Value
        {
            using namespace std::chrono;
            auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            return Value::make_number(static_cast<double>(now));
        });

        // random: random double in [0,1)
        static std::mt19937_64 rng((unsigned)std::chrono::high_resolution_clock::now()
            .time_since_epoch()
            .count());
        host.register_function("rand", [](const std::vector<Value> &args)->Value
        {
            thread_local std::mt19937_64 rng_local((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::make_number(dist(rng_local));
        });

        // string substring: substr(s, start, len_opt)
        host.register_function("substr", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::String && args[1].tag == Tag::Number)
            {
                const std::string &s = *args[0].s;
                int start = static_cast<int>(args[1].num);
                if (start < 0) start = 0;
                if (start >= (int)s.size()) return Value::make_string(std::string());
                size_t len = s.size() - start;
                if (args.size() >= 3 && args[2].tag == Tag::Number)
                {
                    int l = static_cast<int>(args[2].num);
                    if (l >= 0) len = static_cast<size_t>(std::min<int>(l, static_cast<int>(len)));
                }
                return Value::make_string(s.substr(static_cast<size_t>(start), len));
            }
            return Value::make_string(std::string());
        });

        // index_of: index_of(s, sub) returns first index or -1
        host.register_function("index_of", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::String && args[1].tag == Tag::String)
            {
                const std::string &s = *args[0].s, &sub = *args[1].s;
                size_t pos = s.find(sub);
                if (pos == std::string::npos) return Value::make_number(-1.0);
                return Value::make_number(static_cast<double>(pos));
            }
            return Value::make_number(-1.0);
        });

        // simple file read helper: read_file(path) -> string (useful in scripting)
        host.register_function("read_file", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 1 && args[0].tag == Tag::String)
            {
                const std::string &path = *args[0].s;
                std::string out;
                FILE *f = std::fopen(path.c_str(), "rb");
                if (!f) return Value::make_string(std::string());
                std::fseek(f, 0, SEEK_END);
                long sz = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                if (sz > 0) {
                    out.resize((size_t)sz);
                    size_t read = std::fread(&out[0], 1, (size_t)sz, f);
                    (void)read;
                }
                std::fclose(f);
                return Value::make_string(std::move(out));
            }
            return Value::make_string(std::string());
        });

        // write_file(path, content) -> bool as number (1/0)
        host.register_function("write_file", [](const std::vector<Value> &args)->Value
        {
            if (args.size() >= 2 && args[0].tag == Tag::String && args[1].tag == Tag::String)
            {
                const std::string &path = *args[0].s;
                const std::string &content = *args[1].s;
                FILE *f = std::fopen(path.c_str(), "wb");
                if (!f) return Value::make_number(0.0);
                
                size_t wrote = std::fwrite(content.data(), 1, content.size(), f);
                std::fclose(f);
                return Value::make_number(wrote == content.size() ? 1.0 : 0.0);
            }
            return Value::make_number(0.0);
        });
    }
}
