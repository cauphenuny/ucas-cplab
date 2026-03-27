#pragma once

#define FMT_HEADER_ONLY
#include "utils/match.hpp"

#include <fmt/format.h>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace fmt_indent {

struct IndentState {
    int level = 0;
    [[nodiscard]] auto indent() const {
        return std::string(level * 2, ' ');
    }
    void push() {
        level++;
    }
    void pop() {
        level--;
    }
    struct IndentStateGuard {
        IndentState* host;
        IndentStateGuard(IndentState* h) : host(h) {
            host->push();
        }
        ~IndentStateGuard() {
            host->pop();
        }
    };
    auto guard() {
        return IndentStateGuard(this);
    }
};

inline thread_local IndentState state;

}  // namespace fmt_indent

template <typename T> auto serialize(const T& val) -> std::string;

template <typename T> using string_constructible = std::is_constructible<std::string, T>;

template <typename T, typename = void> struct has_toString : std::false_type {};

template <typename T>
struct has_toString<T, std::void_t<decltype(std::declval<T>().toString())>> : std::true_type {};

template <typename T> auto toString(const std::vector<T>& vec) -> std::string {
    if (vec.empty()) return "[]";

    auto& state = fmt_indent::state;
    auto indent = state.indent();
    auto guard = state.guard();
    std::string result = "[\n";
    for (size_t i = 0; i < vec.size(); i++) {
        result += fmt::format("{}  [{}]: {}\n", indent, i, vec[i]);
    }
    result += indent + "]";
    return result;
}

template <typename T> auto toString(const std::unique_ptr<T>& ptr) -> std::string {
    if constexpr (has_toString<T>::value) {
        return ptr ? ptr->toString() : "nullptr";
    } else {
        return serialize(ptr.get());
    }
}

template <typename... Ts> std::string toString(const std::variant<Ts...>& var) {
    return match(var, [](const auto& x) { return serialize(x); });
}

template <typename T> std::string toString(const std::optional<T>& opt) {
    if (opt.has_value()) {
        return serialize(opt.value());
    } else {
        return "nullopt";
    }
}

template <typename T, typename = void> struct has_free_toString : std::false_type {};

template <typename T>
struct has_free_toString<T, std::void_t<decltype(toString(std::declval<T>()))>> : std::true_type {};

template <typename T, typename = void> struct has_to_string : std::false_type {};

template <typename T>
struct has_to_string<T, std::void_t<decltype(std::to_string(std::declval<T>()))>> : std::true_type {
};

template <typename T, typename = void> struct has_stream : std::false_type {};

template <typename T>
struct has_stream<T,
                  std::void_t<decltype(std::declval<std::ostringstream&>() << std::declval<T>())>>
    : std::true_type {};

template <typename T>
struct is_TO_STRING : std::bool_constant<string_constructible<T>::value ||
                                            has_free_toString<T>::value || has_toString<T>::value ||
                                            has_to_string<T>::value || has_stream<T>::value> {};

template <typename T> auto serialize(const T& val) -> std::string {
    if constexpr (string_constructible<T>::value) {
        return "\"" + std::string(val) + "\"";
    } else if constexpr (has_free_toString<T>::value) {
        return toString(val);
    } else if constexpr (has_toString<T>::value) {
        return val.toString();
    } else if constexpr (has_to_string<T>::value) {
        return std::to_string(val);
    } else if constexpr (has_stream<T>::value) {
        std::ostringstream oss;
        oss << val;
        return std::move(oss).str();
    } else {
        static_assert(!sizeof(T), "can not convert to string");
    }
}

template <typename T> constexpr bool is_TO_STRING_v = is_TO_STRING<T>::value;

// NOTE: export .toString() or toString() to fmt formatter
namespace fmt {
template <typename T>
struct formatter<T, std::enable_if_t<has_toString<T>::value || has_free_toString<T>::value, char>> {
    constexpr auto parse(fmt::format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext> auto format(const T& v, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", serialize(v));
    }
};
}  // namespace fmt

template <typename T, typename... Args>
std::string serializeFields(const char* names, const T& var, const Args&... rest) {
    auto& state = fmt_indent::state;
    std::string indent(state.level * 2, ' ');

    while (*names == ' ') names++;

    // Find field name end, skipping commas inside template angle brackets
    const char* end = names;
    int angle_depth = 0;
    while (*end) {
        if (*end == '<')
            angle_depth++;
        else if (*end == '>')
            angle_depth--;
        else if (*end == ',' && angle_depth == 0)
            break;
        end++;
    }

    std::string_view field_name(names, end - names);
    while (!field_name.empty() && field_name.back() == ' ') field_name.remove_suffix(1);

    std::string result;
    result += indent;
    result.append(field_name.data(), field_name.size());
    result += ": ";
    result += serialize(var);
    result += ",\n";

    if constexpr (sizeof...(rest) > 0) {
        if (*end == ',') result += serializeFields(end + 1, rest...);
    }
    return result;
}

#define TO_STRING(ClassName, ...)                                   \
    [[nodiscard]] std::string toString() const {                       \
        auto& state = fmt_indent::state;                               \
        auto indent = state.indent();                                  \
        auto guard = state.guard();                                    \
        std::string body = serializeFields(#__VA_ARGS__, __VA_ARGS__); \
        return #ClassName " {\n" + body + indent + "}";                \
    }

#define EMPTY_TO_STRING(ClassName)\
    [[nodiscard]] std::string toString() const { return #ClassName " {}"; }

#define DELEGATE_TO_STRING(ClassName, field) \
    [[nodiscard]] std::string toString() const { return fmt::format(#ClassName ": {}", field); }