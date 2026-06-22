#pragma once
#include <bit>
#include <string_view>
#include <limits>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cassert>

namespace ineffa {

template <class allocator>
class tiny_string {
    static constexpr int sso_buffer_size = 15;

    alignas(char*) char data_[sso_buffer_size + 1];

    struct heap_mode_layout {
        char* ptr;
        uint32_t size;
        // char padding[sizeof data_ - sizeof ptr - 8];
        uint32_t capacity;
    };
    
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof data_ == sizeof(heap_mode_layout));

public:
    using trivially_relocatable = void;

    tiny_string() noexcept: data_{0} {
        data_[sso_buffer_size] = sso_buffer_size;
    }

    tiny_string(std::string_view sv) : data_{0} {
        assert(sv.size() <= std::numeric_limits<int32_t>::max());
        
        if (uint32_t len = sv.length(); len <= sso_buffer_size) {
            std::memcpy(data_, sv.data(), len);
            data_[sso_buffer_size] = uint8_t(sso_buffer_size - len);
        }

        else {
            heap_mode_layout* heap_mode_data = new (data_) heap_mode_layout {
                .ptr = (char*)allocator::allocate(len + 1),
                .size = len,
                .capacity = len | 1 << 31
            };

            std::memcpy(heap_mode_data->ptr, sv.data(), len);
            heap_mode_data->ptr[len] = '\0';
        }
    }

    tiny_string(const char* str) : tiny_string(std::string_view(str ? str : "")) {}

    tiny_string(const tiny_string& other) : tiny_string(std::string_view(other)) {}

    tiny_string(tiny_string&& other) noexcept {
        std::memcpy(data_, other.data_, sizeof data_);
        std::fill_n(other.data_, sso_buffer_size + 1, 0);
        other.data_[sso_buffer_size] = sso_buffer_size;
    }
    
    constexpr auto is_sso_mode() const noexcept -> bool {
        return ((uint8_t)data_[sso_buffer_size] & 0b1000'0000) == 0;
    }

    auto data() const noexcept -> const char* {
        return is_sso_mode() ? data_ : std::launder((heap_mode_layout*)data_)->ptr;
    }

    auto size() const noexcept -> uint32_t {
        return is_sso_mode() ? sso_buffer_size - data_[sso_buffer_size] : std::launder((heap_mode_layout*)data_)->size;
    }

    auto capacity() const noexcept -> uint32_t {
        return is_sso_mode() ? sso_buffer_size : std::launder((heap_mode_layout*)data_)->capacity & ~(1 << 31);
    }

    auto sv() const noexcept -> std::string_view {
        return std::string_view(data(), size());
    }

    ~tiny_string() noexcept {
        if (not is_sso_mode())
            allocator::deallocate(std::launder((heap_mode_layout*)data_)->ptr);
    }

    auto operator=(tiny_string other) noexcept -> tiny_string& {
        char tmp[sizeof data_];
        std::memcpy(tmp, other.data_, sizeof data_);
        std::memcpy(other.data_, data_, sizeof data_);
        std::memcpy(data_, tmp, sizeof data_);
        return *this;
    }

    operator std::string_view() const noexcept {
        return sv();
    }
};

}
