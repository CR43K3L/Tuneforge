#include "common/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tf {

const Json Json::kNull{};

// ---------------------------------------------------------------------------
// Accesseurs
// ---------------------------------------------------------------------------
bool Json::as_bool(bool def) const {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Number) return num_ != 0.0;
    return def;
}

double Json::as_number(double def) const {
    if (type_ == Type::Number) return num_;
    if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
    return def;
}

int64_t Json::as_int(int64_t def) const {
    if (type_ == Type::Number) return static_cast<int64_t>(num_);
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    return def;
}

uint32_t Json::as_u32(uint32_t def) const {
    if (type_ != Type::Number) return def;
    if (num_ < 0.0) return static_cast<uint32_t>(static_cast<int64_t>(num_));
    return static_cast<uint32_t>(num_);
}

std::string Json::as_string(std::string def) const {
    if (type_ == Type::String) return str_;
    return def;
}

// ---------------------------------------------------------------------------
// Tableau / objet
// ---------------------------------------------------------------------------
void Json::push(Json v) {
    if (type_ != Type::Array) { type_ = Type::Array; arr_.clear(); }
    arr_.push_back(std::move(v));
}

size_t Json::size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

const Json& Json::at(size_t i) const {
    if (type_ != Type::Array || i >= arr_.size()) return kNull;
    return arr_[i];
}

void Json::set(std::string key, Json v) {
    if (type_ != Type::Object) { type_ = Type::Object; obj_.clear(); }
    for (auto& kv : obj_) {
        if (kv.first == key) { kv.second = std::move(v); return; }
    }
    obj_.emplace_back(std::move(key), std::move(v));
}

const Json* Json::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (auto& kv : obj_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

bool Json::has(std::string_view key) const { return find(key) != nullptr; }

const Json& Json::operator[](std::string_view key) const {
    const Json* p = find(key);
    return p ? *p : kNull;
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------
void Json::escape(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void Json::dump_to(std::string& out, int indent, int depth) const {
    const bool pretty = indent > 0;
    const std::string pad  = pretty ? std::string(static_cast<size_t>(indent) * (depth + 1), ' ') : "";
    const std::string pad0 = pretty ? std::string(static_cast<size_t>(indent) * depth, ' ') : "";

    switch (type_) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            if (!std::isfinite(num_)) { out += "null"; break; }
            if (num_ == static_cast<double>(static_cast<int64_t>(num_)) &&
                std::fabs(num_) < 9.0e15) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(static_cast<int64_t>(num_)));
                out += buf;
            } else {
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.10g", num_);
                out += buf;
            }
            break;
        }
        case Type::String: escape(out, str_); break;
        case Type::Array: {
            if (arr_.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                if (pretty) { out += '\n'; out += pad; }
                arr_[i].dump_to(out, indent, depth + 1);
            }
            if (pretty) { out += '\n'; out += pad0; }
            out += ']';
            break;
        }
        case Type::Object: {
            if (obj_.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                if (pretty) { out += '\n'; out += pad; }
                escape(out, obj_[i].first);
                out += pretty ? ": " : ":";
                obj_[i].second.dump_to(out, indent, depth + 1);
            }
            if (pretty) { out += '\n'; out += pad0; }
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    out.reserve(256);
    dump_to(out, indent, 0);
    return out;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
namespace {

struct Parser {
    std::string_view s;
    size_t           i = 0;
    std::string      err;

    void skip_ws() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
            // Commentaires // et /* */ toleres : pratique pour les profils edites a la main.
            if (c == '/' && i + 1 < s.size()) {
                if (s[i + 1] == '/') {
                    while (i < s.size() && s[i] != '\n') ++i;
                    continue;
                }
                if (s[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
                    i = (i + 1 < s.size()) ? i + 2 : s.size();
                    continue;
                }
            }
            break;
        }
    }

    bool fail(const char* m) {
        if (err.empty()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s (offset %zu)", m, i);
            err = buf;
        }
        return false;
    }

    void encode_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(uint32_t& out) {
        if (i + 4 > s.size()) return fail("sequence \\u tronquee");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9')      out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("chiffre hexadecimal invalide");
        }
        return true;
    }

    bool parse_string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("chaine attendue");
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (i >= s.size()) return fail("echappement tronque");
            char e = s[i++];
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() &&
                        s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    encode_utf8(out, cp);
                    break;
                }
                default: return fail("echappement inconnu");
            }
        }
        return fail("chaine non terminee");
    }

    bool parse_value(Json& out, int depth) {
        if (depth > 64) return fail("imbrication trop profonde");
        skip_ws();
        if (i >= s.size()) return fail("fin de document inattendue");

        char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::object();
            skip_ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                skip_ws();
                std::string key;
                if (!parse_string(key)) return false;
                skip_ws();
                if (i >= s.size() || s[i] != ':') return fail("':' attendu");
                ++i;
                Json v;
                if (!parse_value(v, depth + 1)) return false;
                out.set(std::move(key), std::move(v));
                skip_ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("',' ou '}' attendu");
            }
        }
        if (c == '[') {
            ++i;
            out = Json::array();
            skip_ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                Json v;
                if (!parse_value(v, depth + 1)) return false;
                out.push(std::move(v));
                skip_ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("',' ou ']' attendu");
            }
        }
        if (c == '"') {
            std::string str;
            if (!parse_string(str)) return false;
            out = Json(std::move(str));
            return true;
        }
        if (s.compare(i, 4, "true") == 0)  { i += 4; out = Json(true);  return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; out = Json(false); return true; }
        if (s.compare(i, 4, "null") == 0)  { i += 4; out = Json();      return true; }

        // Nombre
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        bool digits = false;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
                                s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) {
            if (s[i] >= '0' && s[i] <= '9') digits = true;
            ++i;
        }
        if (!digits) return fail("valeur invalide");
        std::string num(s.substr(start, i - start));
        out = Json(std::strtod(num.c_str(), nullptr));
        return true;
    }
};

} // namespace

std::optional<Json> Json::parse(std::string_view text, std::string* error) {
    Parser p{text};
    Json   root;
    if (!p.parse_value(root, 0)) {
        if (error) *error = p.err;
        return std::nullopt;
    }
    p.skip_ws();
    if (p.i != text.size()) {
        if (error) *error = "donnees residuelles apres la valeur racine";
        return std::nullopt;
    }
    return root;
}

} // namespace tf
