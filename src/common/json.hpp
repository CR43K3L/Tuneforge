#pragma once
//
// Mini-JSON autonome (parse + serialisation), sans dependance externe.
// Les objets conservent l'ordre d'insertion : les fichiers generes restent
// lisibles et diffables a la main.
//
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tf {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(int v) : type_(Type::Number), num_(v) {}
    Json(int64_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(uint32_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(uint64_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
    Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Accesseurs tolerants : renvoient la valeur par defaut si le type differe.
    bool        as_bool(bool def = false) const;
    double      as_number(double def = 0.0) const;
    int64_t     as_int(int64_t def = 0) const;
    uint32_t    as_u32(uint32_t def = 0) const;
    std::string as_string(std::string def = {}) const;

    // --- Tableau -----------------------------------------------------------
    void   push(Json v);
    size_t size() const;
    const Json& at(size_t i) const;
    const std::vector<Json>& items() const { return arr_; }
    std::vector<Json>&       items() { return arr_; }

    // --- Objet -------------------------------------------------------------
    void        set(std::string key, Json v);
    bool        has(std::string_view key) const;
    const Json* find(std::string_view key) const;
    const Json& operator[](std::string_view key) const;  // Null si absent
    const std::vector<std::pair<std::string, Json>>& fields() const { return obj_; }
    std::vector<std::pair<std::string, Json>>&       fields() { return obj_; }

    // --- (De)serialisation -------------------------------------------------
    std::string dump(int indent = 2) const;
    static std::optional<Json> parse(std::string_view text, std::string* error = nullptr);

private:
    void dump_to(std::string& out, int indent, int depth) const;
    static void escape(std::string& out, std::string_view s);

    Type                                      type_ = Type::Null;
    bool                                      bool_ = false;
    double                                    num_ = 0.0;
    std::string                               str_;
    std::vector<Json>                         arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    static const Json kNull;
};

} // namespace tf
