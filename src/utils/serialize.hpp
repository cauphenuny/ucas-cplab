#include <string>
#include <sstream>
#include <type_traits>
#include <utility>

template<typename T>
using string_constructible = std::is_constructible<std::string, T>;

template<typename T, typename = void>
struct has_toString : std::false_type {};

template<typename T>
struct has_toString<T, std::void_t<decltype(std::declval<T>().toString())>>
    : std::true_type {};

template<typename T, typename = void>
struct has_free_toString : std::false_type {};

template<typename T>
struct has_free_toString<T, std::void_t<decltype(toString(std::declval<T>()))>>
    : std::true_type {};

template<typename T, typename = void>
struct has_to_string : std::false_type {};

template<typename T>
struct has_to_string<T, std::void_t<decltype(std::to_string(std::declval<T>()))>>
    : std::true_type {};

template<typename T, typename = void>
struct has_stream : std::false_type {};

template<typename T>
struct has_stream<T,
    std::void_t<decltype(std::declval<std::ostringstream&>() << std::declval<T>())>>
    : std::true_type {};

template<typename T>
struct is_serializable : std::bool_constant<
    string_constructible<T>::value ||
    has_free_toString<T>::value ||
    has_toString<T>::value ||
    has_to_string<T>::value ||
    has_stream<T>::value
> {};

template<typename T>
std::string serialize(const T& val) {
    if constexpr (string_constructible<T>::value) {
        return "\"" + std::string(val) + "\"";
    } 
    else if constexpr (has_free_toString<T>::value) {
        return toString(val);
    }
    else if constexpr (has_toString<T>::value) {
        return val.toString();
    }
    else if constexpr (has_to_string<T>::value) {
        return std::to_string(val);
    }
    else if constexpr (has_stream<T>::value) {
        std::ostringstream oss;
        oss << val;
        return std::move(oss).str();
    }
    else {
        static_assert(!sizeof(T), "can not convert to string");
    }
}

template<typename T>
constexpr bool is_serializable_v = is_serializable<T>::value;

template <typename T> std::string serialize(const std::unique_ptr<T>& ptr) {
    if constexpr (is_serializable_v<T>) {
        return ptr ? serialize(*ptr) : "nullptr";
    } else {
        return serialize(ptr.get());
    }
}

