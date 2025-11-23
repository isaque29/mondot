#include "value.h"
#include <sstream>

using namespace std;

Value Value::make_nil()
{
    return Value();
}
Value Value::make_number(double n)
{
    Value v;
    v.tag = Tag::Number;
    v.num = n;
    return v;
}
Value Value::make_string(const string &str)
{
    Value v;
    v.tag = Tag::String;
    v.s = make_shared<string>(str);
    return v;
}
Value Value::make_rule(const Rule &rule)
{
    Value v;
    v.tag = Tag::Rule;
    v.r = make_shared<Rule>(rule);
    return v;
}

string value_to_string(const Value &v)
{
    switch(v.tag)
    {
        case Tag::Nil: return "nil";
        case Tag::Number:
        {
            ostringstream oss;
            oss << v.num;
            return oss.str();
        }
        case Tag::String: return v.s ? *v.s : string("(null)");
        case Tag::Rule: return string("Rule(") + (v.r ? to_string(v.r->id) : string("0")) + ")";
    }
    return string("?");
}
