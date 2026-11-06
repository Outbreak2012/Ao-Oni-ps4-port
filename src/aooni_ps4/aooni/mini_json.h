// Mini JSON parser header-only para Ao Oni PS4
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstring>

struct JVal {
    enum Type { T_NULL, T_BOOL, T_NUM, T_STR, T_ARR, T_OBJ } type = T_NULL;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JVal> a;
    std::map<std::string, JVal> o;

    int asInt(int def = 0) const { return type == T_NUM ? (int)n : def; }
    double asNum(double def = 0.0) const { return type == T_NUM ? n : def; }
    bool asBool(bool def = false) const { return type == T_BOOL ? b : def; }
    const std::string& asStr() const { return s; }
    const JVal& operator[](size_t i) const {
        static JVal nullVal;
        return (type == T_ARR && i < a.size()) ? a[i] : nullVal;
    }
    const JVal& get(const char* k) const {
        static JVal nullVal;
        if (type != T_OBJ) return nullVal;
        auto it = o.find(k);
        return it != o.end() ? it->second : nullVal;
    }
};

struct JParser {
    const char* p;
    const char* end;

    void skipWS() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    }

    std::string parseStr() {
        std::string out;
        if (p >= end || *p != '"') return out;
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                char c = *p++;
                if (c == 'n') out.push_back('\n');
                else if (c == 'r') out.push_back('\r');
                else if (c == 't') out.push_back('\t');
                else if (c == '"' || c == '\\' || c == '/') out.push_back(c);
                else if (c == 'u' && p + 4 <= end) {
                    char hex[5] = { p[0], p[1], p[2], p[3], 0 };
                    unsigned int cp = (unsigned int)strtoul(hex, nullptr, 16);
                    p += 4;
                    if (cp < 0x80) out.push_back((char)cp);
                    else if (cp < 0x800) {
                        out.push_back((char)(0xC0 | (cp >> 6)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (cp >> 12)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                }
            } else {
                out.push_back(*p++);
            }
        }
        if (p < end && *p == '"') p++;
        return out;
    }

    JVal parseVal() {
        skipWS();
        JVal v;
        if (p >= end) return v;
        char c = *p;
        if (c == '"') {
            v.type = JVal::T_STR;
            v.s = parseStr();
        } else if (c == '[') {
            v.type = JVal::T_ARR;
            p++;
            skipWS();
            if (p < end && *p == ']') { p++; return v; }
            while (p < end) {
                v.a.push_back(parseVal());
                skipWS();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; break; }
                break;
            }
        } else if (c == '{') {
            v.type = JVal::T_OBJ;
            p++;
            skipWS();
            if (p < end && *p == '}') { p++; return v; }
            while (p < end) {
                skipWS();
                std::string k = parseStr();
                skipWS();
                if (p < end && *p == ':') p++;
                v.o[k] = parseVal();
                skipWS();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                break;
            }
        } else if (c == 't' && p + 4 <= end && strncmp(p, "true", 4) == 0) {
            v.type = JVal::T_BOOL; v.b = true; p += 4;
        } else if (c == 'f' && p + 5 <= end && strncmp(p, "false", 5) == 0) {
            v.type = JVal::T_BOOL; v.b = false; p += 5;
        } else if (c == 'n' && p + 4 <= end && strncmp(p, "null", 4) == 0) {
            v.type = JVal::T_NULL; p += 4;
        } else {
            v.type = JVal::T_NUM;
            char* next = nullptr;
            v.n = strtod(p, &next);
            if (next == p) p++;
            else p = next;
        }
        return v;
    }
};

inline bool loadJsonFile(const char* path, JVal& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::string buf;
    char chunk[16384];
    size_t n = 0;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buf.append(chunk, n);
    fclose(f);
    if (buf.empty()) return false;
    JParser parser{ buf.data(), buf.data() + buf.size() };
    out = parser.parseVal();
    return true;
}

