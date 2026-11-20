// ============================================================================
//  json.h - A tiny hand-written JSON writer.
//  Keeping it here means the project has no third-party web libraries:
//  everything between the browser and the C++ algorithms is written by us.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

class Json {
public:
    Json& beginObject() { return open('{'); }
    Json& endObject()   { return close('}'); }
    Json& beginArray()  { return open('['); }
    Json& endArray()    { return close(']'); }

    Json& key(const std::string& k) {
        beforeValue();
        out_ += '"';
        out_ += escape(k);
        out_ += "\":";
        pendingValue_ = true;
        return *this;
    }

    Json& value(const std::string& v) {
        beforeValue();
        out_ += '"';
        out_ += escape(v);
        out_ += '"';
        return *this;
    }
    Json& value(const char* v) { return value(std::string(v)); }
    Json& value(double v) {
        beforeValue();
        if (!std::isfinite(v)) { out_ += "null"; return *this; }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.10g", v);
        out_ += buf;
        return *this;
    }
    Json& value(int v) {
        beforeValue();
        out_ += std::to_string(v);
        return *this;
    }
    Json& value(bool v) {
        beforeValue();
        out_ += v ? "true" : "false";
        return *this;
    }

    // Convenience:  j.add("name", loc.name)  ===  j.key("name").value(loc.name)
    template <typename T>
    Json& add(const std::string& k, const T& v) { return key(k).value(v); }

    const std::string& str() const { return out_; }

private:
    struct Level { int members = 0; };

    Json& open(char c) {
        beforeValue();
        out_ += c;
        levels_.push_back(Level{});
        return *this;
    }
    Json& close(char c) {
        if (!levels_.empty()) levels_.pop_back();
        out_ += c;
        return *this;
    }
    void beforeValue() {
        if (pendingValue_) { pendingValue_ = false; return; }
        if (levels_.empty()) return;
        if (levels_.back().members > 0) out_ += ',';
        levels_.back().members++;
    }

    static std::string escape(const std::string& in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (char c : in) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    }

    std::string out_;
    std::vector<Level> levels_;
    bool pendingValue_ = false;
};
