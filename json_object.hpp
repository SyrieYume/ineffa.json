#pragma once
#include <ranges>
#include <string_view>
#include <optional>
#include <string>
#include <cstdlib>
#include <cstdint>
#include "trailing_string.hpp"
#include "trailing_array.hpp"
#include "trailing_map.hpp"

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(void*) == 8);

namespace ineffa::json {

struct allocator {
    static constexpr unsigned min_alignment = alignof(std::max_align_t);

    template <class T = std::byte, unsigned alignment = alignof(T)>
    static auto allocate(std::size_t bytes_size) -> T* __restrict {
        if (T* mem = static_cast<T*>(std::malloc(bytes_size)))
            return std::assume_aligned<std::max(alignment, min_alignment)>(mem);
        else throw std::bad_alloc();
    }

    template <class T = std::byte, unsigned alignment = alignof(T)>
    static auto allocate_n(std::size_t n) -> T* __restrict {
        return allocate<T, alignment>(sizeof(T) * n);
    }

    template <class T = std::byte, unsigned alignment = alignof(T)>
    static auto reallocate(T* p, std::size_t new_bytes_size) -> T* __restrict {
        if (T* mem = static_cast<T*>(std::realloc(p, new_bytes_size)))
            return std::assume_aligned<std::max(alignment, min_alignment)>(mem);
        else throw std::bad_alloc();
    }

    template <class T = std::byte, unsigned alignment = alignof(T)>
    static auto reallocate_n(T* p, std::size_t n) -> T* __restrict {
        return reallocate<T, alignment>(p, sizeof(T) * n);
    }

    template <class T = std::byte, unsigned alignment = alignof(T)>
    static void deallocate(T* p) noexcept {
        std::free(p);
    }
};


enum object_type_tag_t : uint8_t {
    TYPE_NULL   = 0,

    TYPE_MAP    = 1,
    TYPE_ARRAY  = 2,
    TYPE_STRING = 3,
    
    TYPE_BOOL   = 4,
    TYPE_NUMBER = 5,
    TYPE_SSO_STRING = 6,
    
    TYPE_MASK   = 0b111
};


class alignas(8) object {
    uint64_t data_ = TYPE_NULL;
    static object nullobj;

    void set_ptr(void* ptr) noexcept {
        data_ = reinterpret_cast<uint64_t>(ptr) | type();
    }

    template <typename T>
    auto as_ptr() const noexcept -> T* {
        return std::launder(reinterpret_cast<T*>(data_ & ~uint64_t(TYPE_MASK)));
    }

public:
    using trivially_relocatable = void;
    using array_t = ineffa::trailing_array<object, allocator>;
    using string_t = ineffa::trailing_string<allocator>;
    using map_t = ineffa::trailing_map<object, allocator>;

    constexpr object(uint64_t val, uint8_t type_tag) noexcept : data_(val | type_tag) {}
    constexpr object() noexcept : data_(TYPE_NULL) {}
    constexpr object(int64_t val) noexcept : object(static_cast<double>(val)) {}
    constexpr object(int val) noexcept : object(static_cast<double>(val)) {}
    constexpr object(double val) noexcept : data_((std::bit_cast<uint64_t>(val) & ~uint64_t(TYPE_MASK)) | TYPE_NUMBER) {}
    constexpr object(bool val) noexcept : data_((uint64_t)val << 3 | TYPE_BOOL) {}
    object(const array_t* val) noexcept : data_(reinterpret_cast<uint64_t>(val) | TYPE_ARRAY) {}
    object(const string_t* val) noexcept : data_(reinterpret_cast<uint64_t>(val) | TYPE_STRING) {}
    object(const map_t* val) noexcept : data_(reinterpret_cast<uint64_t>(val) | TYPE_MAP) {}
    object(const char* s) : object(std::string_view(s)) {}

    object(std::string_view sv) {
        if (const unsigned len = sv.length(); len <= 7) {
            data_ = len << 4 | TYPE_SSO_STRING;
            std::memcpy((char*)&data_ + 1, sv.data(), len);
        }
        else data_ = reinterpret_cast<uint64_t>(string_t::create(sv)) | TYPE_STRING;
    }

    auto type() const noexcept -> uint8_t {
        return data_ & object_type_tag_t::TYPE_MASK;
    }

    auto as_int() const noexcept -> std::optional<int64_t> {
        return type() == TYPE_NUMBER ? std::optional(static_cast<int64_t>(*as_double())) : std::nullopt;
    } 

    auto as_bool() const noexcept -> std::optional<bool> {
        return type() == TYPE_BOOL ? std::optional((data_ & ~uint64_t(TYPE_MASK)) != 0) : std::nullopt;
    }

    auto as_double() const noexcept -> std::optional<double> {
        return type() == TYPE_NUMBER ? std::optional(std::bit_cast<double>(data_ & ~uint64_t(TYPE_MASK))) : std::nullopt;
    }

    auto as_string() const noexcept -> std::optional<std::string_view> {
        if (const uint8_t type = this->type(); type == TYPE_SSO_STRING)
            return std::string_view((const char*)&data_ + 1, data_ >> 4 & 0b1111);
        else if (type == TYPE_STRING)
            return as_ptr<const string_t>()->sv();
        else [[unlikely]] return std::nullopt;
    }

    template <typename T>
    auto value_or(T&& default_val) const {
        using D = std::decay_t<T>;
        if constexpr (std::is_same_v<D, bool>) return as_bool().value_or(default_val);
        else if constexpr (std::is_integral_v<D>) return static_cast<D>(as_int().value_or(default_val));
        else if constexpr (std::is_floating_point_v<D>) return static_cast<D>(as_double().value_or(default_val));
        else if constexpr (std::is_convertible_v<D, std::string_view>) {
            using Ret = std::conditional_t<std::is_same_v<D, std::string>, std::string, std::string_view>;
            if (auto opt = as_string()) return Ret(*opt);
            return Ret(std::forward<T>(default_val));
        }
        else static_assert(false, "unsupported type for value_or");
    }

    auto keys() const noexcept {
        if (type() == TYPE_MAP) [[likely]] {
            const auto* map = as_ptr<const map_t>();
            return std::span(map->keys(), map->size()) | std::views::transform(&map_t::key_string::sv);
        }
        else return std::span<const map_t::key_string>() | std::views::transform(&map_t::key_string::sv);
    }

    auto values() noexcept -> std::span<json::object> {
        if (const uint8_t type = this->type(); type == TYPE_ARRAY) 
            return as_ptr<array_t>()->span();

        else if (type == TYPE_MAP) {
            auto* map = as_ptr<map_t>();
            return std::span(map->values(), map->size());
        }

        else return {};
    }

    auto values() const noexcept -> std::span<const json::object> {
        return const_cast<json::object&>(*this).values();
    }

    auto key_values(this auto&& self) noexcept {
        using value_type = std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const json::object, json::object>;

        if (self.type() == TYPE_MAP) [[likely]] {
            auto* map = self.template as_ptr<map_t>();
            return std::views::zip(
                std::span<const map_t::key_string>(map->keys(), map->size()) | std::views::transform(&map_t::key_string::sv),
                std::span<value_type>(map->values(), map->size())
            );
        }

        else return std::views::zip(
            std::span<const map_t::key_string>() | std::views::transform(&map_t::key_string::sv), 
            std::span<value_type>()
        );
    }

    auto emplace_back(auto&& arg) -> json::object& {
        if (type() == TYPE_ARRAY) [[likely]]
            as_ptr<array_t>()->emplace_pack(this, &json::object::set_ptr, std::forward<decltype(arg)>(arg));
        return *this;
    }

    auto insert_or_assign(std::string_view key, auto&& arg) -> json::object& {
        if (type() == TYPE_MAP) [[likely]] {
            auto [ptr, inserted] = as_ptr<map_t>()->try_emplace(this, &json::object::set_ptr, std::forward<decltype(arg)>(arg));
            if (not inserted) {
                auto val = json::object(std::forward<decltype(arg)>(arg));
                std::destroy_at(ptr);
                std::construct_at(ptr, std::move(val));
            }
        }
        return *this;
    }

    auto remove(unsigned i) noexcept -> json::object& {
        if (type() == TYPE_ARRAY) [[likely]]
            as_ptr<array_t>()->remove(i);
        return *this;
    }

    auto remove(std::string_view key) noexcept -> json::object& {
        if (type() == TYPE_MAP) [[likely]] {
            auto* map = as_ptr<map_t>();
            map->remove(map->find_index(key));
        }
        return *this;
    }

    bool contains(std::string_view key) noexcept {
        return type() == TYPE_MAP ? as_ptr<const map_t>()->find_index(key) >= 0 : false;
    }

    object(object&& other) noexcept : data_(std::exchange(other.data_, TYPE_NULL)) {}

    object& operator=(auto&& val) requires std::constructible_from<json::object, decltype(val)> {
        if constexpr (std::is_same_v<std::decay_t<decltype(val)>, json::object>)
            assert(this != &val);
        std::destroy_at(this);
        std::construct_at(this, std::forward<decltype(val)>(val));
        return *this;
    }

    auto operator[](unsigned i) const noexcept -> const json::object& {
        if (type() != TYPE_ARRAY) [[unlikely]]
            return json::object::nullobj;

        if (auto* arr = as_ptr<array_t>(); i < arr->size())
            return arr->data()[i];

        else [[unlikely]] return json::object::nullobj;
    }

    auto operator[](unsigned i) noexcept -> json::object& {
        return const_cast<json::object&>(std::as_const(*this)[i]); 
    }

    auto operator[](std::string_view key) -> json::object& {
        if (const uint8_t type = this->type(); type == TYPE_NULL and data_ != 0)
            data_ = reinterpret_cast<uint64_t>(map_t::create()) | TYPE_MAP;

        else if (type != TYPE_MAP) [[unlikely]]
            return json::object::nullobj;

        auto* map = as_ptr<map_t>();
        auto [ptr, inserted] = map->try_emplace(this, &json::object::set_ptr, key, uint64_t(1 << 3), TYPE_NULL);
        return *ptr;
    }

    bool operator==(const auto&& rhs) const noexcept {
        using D = std::decay_t<decltype(rhs)>;
        
        if constexpr (std::is_same_v<D, object>) {
            uint8_t type1 = this->type();
            uint8_t type2 = rhs.type();

            if (type1 == TYPE_SSO_STRING || type1 == TYPE_STRING)
                return type2 == TYPE_SSO_STRING || type2 == TYPE_STRING ? *as_string() == *rhs.as_string() : false;
                
            if (type1 != type2)
                return false;

            return data_ == rhs.data_;
        }
        else if constexpr (std::is_same_v<D, bool>) {
            auto val = as_bool();
            return val ? *val == rhs : false;
        }
        else if constexpr (std::is_integral_v<D>) {
            auto val = as_int();
            return val ? *val == static_cast<int64_t>(rhs) : false;
        }
        else if constexpr (std::is_convertible_v<D, std::string_view>) {
            auto val = as_string();
            return val ? *val == std::string_view(rhs) : false;
        }
        else return false;
    }

    ~object() noexcept {
        switch(type()) {
            case TYPE_ARRAY: std::destroy_at(as_ptr<array_t>()); break;
            case TYPE_STRING: std::destroy_at(as_ptr<string_t>()); break;
            case TYPE_MAP: std::destroy_at(as_ptr<map_t>()); break;
            default: break;
        }
    }
};

inline json::object json::object::nullobj = json::object();

using map = json::object::map_t;
using array = json::object::array_t;
using string = json::object::string_t;

}
