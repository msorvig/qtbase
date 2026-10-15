// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
//
// Emscripten val.h compatibility shim for nuscripten/WASI builds.
// Maps emscripten::val API to nuscripten::val.

#ifndef QT_EMSCRIPTEN_VAL_SHIM_H
#define QT_EMSCRIPTEN_VAL_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/val.h>
#else

#include <val.h>  // nuscripten val
#include <string>
#include <vector>
#include <type_traits>
#include <cstdio>

namespace emscripten {

// EM_VAL is emscripten's opaque handle type for val objects in EM_JS
using EM_VAL = int;

// emscripten::val wraps nuscripten::val and adds the emscripten-specific API:
// - Constructors from primitives: val(42), val("string")
// - Template type conversion: val.as<int>(), val.as<std::string>()
// - call<ReturnType>(): val.call<int>("method"), val.call<void>("method")
// - undefined(), null(), object(), array(), module_property()

class val {
    nuscripten::val v_;

public:
    // Default: undefined
    val() : v_() {}

    // From nuscripten::val
    val(const nuscripten::val& nv) : v_(nv) {}
    val(nuscripten::val&& nv) : v_(std::move(nv)) {}

    // From primitives (emscripten-style constructors)
    val(int value) : v_(nuscripten::val::number(value)) {}
    val(unsigned int value) : v_(nuscripten::val::number(static_cast<int>(value))) {}
    val(long value) : v_(nuscripten::val::number(static_cast<int>(value))) {}
    val(unsigned long value) : v_(nuscripten::val::number(static_cast<int>(value))) {}
    val(long long value) : v_(nuscripten::val::number(static_cast<int>(value))) {}
    val(unsigned long long value) : v_(nuscripten::val::number(static_cast<int>(value))) {}
    val(double value) : v_(nuscripten::val::number(value)) {}
    val(float value) : v_(nuscripten::val::number(static_cast<double>(value))) {}
    val(bool value) : v_(value ? nuscripten::val::number(1) : nuscripten::val::number(0)) {}
    val(const char* str) : v_(nuscripten::val::string(str)) {}
    val(const std::string& str) : v_(nuscripten::val::string(str)) {}

    // Copy/move
    val(const val& other) = default;
    val(val&& other) = default;
    val& operator=(const val& other) = default;
    val& operator=(val&& other) = default;

    // Static constructors
    static val global(const char* name) {
        return val(nuscripten::val::global(name));
    }
    static val global(const std::string& name) {
        return val(nuscripten::val::global(name));
    }
    static val undefined() { return val(); }
    static val null() { return val(); } // TODO: proper null
    static val object() { return global("Object").new_(); }
    static val array() { return global("Array").new_(); }
    // array from existing JS iterable
    static val array(const val& contents) {
        return global("Array").call_impl("from", contents);
    }
    // array from C++ vector of vals
    static val array(const std::vector<val>& items) {
        val arr = array();
        for (size_t i = 0; i < items.size(); ++i)
            arr.call_impl("push", items[i]);
        return arr;
    }
    // array from iterator range
    template<typename It>
    static val array(It begin, It end) {
        val arr = array();
        for (auto it = begin; it != end; ++it)
            arr.call_impl("push", val(*it));
        return arr;
    }
    static val module_property(const char* name) {
        return val(nuscripten::val::instance()[name]);
    }

    // Property access
    val operator[](const char* name) const {
        return val(v_[name]);
    }
    val operator[](const std::string& name) const {
        return val(v_[name]);
    }
    val operator[](const val& key) const {
        return val(v_[key.v_]);
    }
    val operator[](int index) const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", index);
        return val(v_[buf]);
    }

    // Property setting
    void set(const char* name, const val& value) { v_.set(name, value.v_); }
    void set(const std::string& name, const val& value) { v_.set(name, value.v_); }
    void set(const char* name, const char* value) { v_.set(name, value); }
    void set(const char* name, const std::string& value) { v_.set(name, value.c_str()); }
    void set(const std::string& name, const std::string& value) { v_.set(name, value); }
    void set(const char* name, int value) { v_.set(name, value); }
    void set(const val& key, const val& value) {
        // Use JS bracket notation: obj[key] = value
        // nuscripten doesn't have set-by-val, so convert key to string
        v_.set(key.v_.toString(), value.v_);
    }
    void set(int index, const val& value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", index);
        v_.set(buf, value.v_);
    }

    // Template type conversion (emscripten-style as<T>())
    template<typename T>
    T as() const {
        if constexpr (std::is_same_v<T, int>) return v_.asInt();
        else if constexpr (std::is_same_v<T, unsigned int>) return static_cast<unsigned int>(v_.asInt());
        else if constexpr (std::is_same_v<T, long>) return static_cast<long>(v_.asInt());
        else if constexpr (std::is_same_v<T, unsigned long>) return static_cast<unsigned long>(v_.asInt());
        else if constexpr (std::is_same_v<T, long long>) return static_cast<long long>(v_.asInt());
        else if constexpr (std::is_same_v<T, short>) return static_cast<short>(v_.asInt());
        else if constexpr (std::is_same_v<T, unsigned short>) return static_cast<unsigned short>(v_.asInt());
        else if constexpr (std::is_same_v<T, double>) return v_.asDouble();
        else if constexpr (std::is_same_v<T, float>) return static_cast<float>(v_.asDouble());
        else if constexpr (std::is_same_v<T, bool>) return v_.asInt() != 0;
        else if constexpr (std::is_same_v<T, std::string>) return v_.toString();
        else if constexpr (std::is_same_v<T, val>) return *this;
        else static_assert(!sizeof(T), "Unsupported type for val::as<T>()");
    }

    // Method calls with return type template: call<void>("m"), call<val>("m", arg), call<int>("m")
    template<typename Ret, typename... Args>
    Ret call(const char* name, Args&&... args) const {
        val result = call_impl(name, std::forward<Args>(args)...);
        if constexpr (std::is_void_v<Ret>) return;
        else return result.as<Ret>();
    }

    // Type checks
    bool isNull() const { return false; } // TODO
    bool isUndefined() const { return false; } // TODO
    bool isTrue() const { return as<bool>(); }
    bool isFalse() const { return !as<bool>(); }
    bool isNumber() const { return false; } // TODO
    bool isString() const { return false; } // TODO
    bool isArray() const {
        return global("Array").call_impl("isArray", *this).as<bool>();
    }

    val typeOf() const { return val(); } // TODO

    bool hasOwnProperty(const char* name) const {
        return call_impl("hasOwnProperty", val(name)).as<bool>();
    }

    void delete_(const char*) const {} // TODO
    void delete_(const std::string& name) const { delete_(name.c_str()); }

    // Strict equality (===)
    bool equals(const val& other) const {
        // Approximate: compare string representation
        return v_.toString() == other.v_.toString();
    }

    // Constructor calls: new this(args...)
    val new_() const { return val(v_.new_()); }

    template<typename... Args>
    val new_(Args&&... args) const {
        return new_impl(val(std::forward<Args>(args))...);
    }

    // Call as function: val(args...) — equivalent to JS: val(arg1, arg2, ...)
    // Implemented as val.call("call", null, args...) since JS fn.call(null, ...) == fn(...)
    val operator()() const {
        return val(v_.call("call", nuscripten::val()));
    }
    template<typename... Args>
    val operator()(Args&&... args) const {
        nuscripten::val null_this;  // undefined 'this'
        return val(v_.call("call", null_this, val(std::forward<Args>(args)).v_...));
    }

    // Truthiness
    explicit operator bool() const { return as<bool>(); }

    // Equality (approximate)
    bool operator==(const val& other) const {
        return v_.toString() == other.v_.toString();
    }
    bool operator!=(const val& other) const { return !(*this == other); }

    // Await (JSPI)
    val await() const { return val(v_.await()); }

    // Handle-based ownership (emscripten-specific)
    static val take_ownership(EM_VAL handle) {
        // In emscripten, this takes ownership of a handle from EM_JS.
        // For nuscripten compat, return undefined.
        (void)handle;
        return val();
    }
    EM_VAL as_handle() const {
        return static_cast<EM_VAL>(v_.handle());
    }

    // Access underlying nuscripten::val
    const nuscripten::val& nuscripten_val() const { return v_; }
    nuscripten::val& nuscripten_val() { return v_; }

    // For internal use by the compat layer
    int handle() const { return v_.handle(); }

private:
    // Internal call implementations
    val call_impl(const char* name) const {
        return val(v_.call(name));
    }

    template<typename First, typename... Rest>
    val call_impl(const char* name, First&& first, Rest&&... rest) const {
        if constexpr (std::is_same_v<std::decay_t<First>, val>) {
            if constexpr (sizeof...(Rest) == 0) {
                return val(v_.call(name, first.v_));
            } else {
                return call_impl_vals(name, first, val(std::forward<Rest>(rest))...);
            }
        } else {
            return call_impl(name, val(std::forward<First>(first)), std::forward<Rest>(rest)...);
        }
    }

    // Multi-arg call with all vals
    template<typename... Vals>
    val call_impl_vals(const char* name, const val& first, const Vals&... rest) const {
        return val(v_.call(name, first.v_, rest.v_...));
    }

    // new_ with val args
    template<typename... Vals>
    val new_impl(const val& first, const Vals&... rest) const {
        return val(v_.new_(first.v_, rest.v_...));
    }
};

// typed_memory_view: creates a JS typed array view into wasm memory
template<typename T>
val typed_memory_view(size_t length, const T* data) {
    return val(nuscripten::val::typed_array(const_cast<T*>(data), length * sizeof(T)));
}

// vecFromJSArray
template<typename T>
std::vector<T> vecFromJSArray(const val& v) {
    int length = v["length"].as<int>();
    std::vector<T> result;
    result.reserve(length);
    for (int i = 0; i < length; ++i)
        result.push_back(v[i].as<T>());
    return result;
}

// async() tag for EMSCRIPTEN_BINDINGS
inline int async() { return 0; }

} // namespace emscripten

#endif // __EMSCRIPTEN__
#endif // QT_EMSCRIPTEN_VAL_SHIM_H
