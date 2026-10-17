#ifndef EDITOR_JSON_H
#define EDITOR_JSON_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace editor {

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() : type_(Null), bool_(false), number_(0) {}

    static Json fromBool(bool value);
    static Json fromNumber(double value);
    static Json fromText(const std::string& value);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool is(Type t) const { return type_ == t; }

    bool boolean(bool fallback = false) const;
    long integer(long fallback = 0) const;
    std::string text(const std::string& fallback = std::string()) const;

    size_t size() const;
    const Json& at(size_t index) const;

    bool has(const std::string& key) const;
    const Json& get(const std::string& key) const;

    const std::string& keyAt(size_t index) const;
    const Json& valueAt(size_t index) const;

    void push(const Json& value);
    void set(const std::string& key, const Json& value);

    static Json parse(const std::string& text, std::string& error);

    std::string write(int depth = 0) const;

private:
    Type type_;
    bool bool_;
    double number_;
    std::string text_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json> > members_;
};

}

#endif
