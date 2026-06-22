#pragma once
#include <string_view>
#include <cstring>

namespace ineffa {

template <class allocator>
class trailing_string {
    const unsigned size_ = 0;

    trailing_string(std::string_view sv) noexcept : size_(sv.size()) {
        std::memcpy(data(), sv.data(), sv.size());
        data()[sv.size()] = '\0';
    }

public:
    auto size() const noexcept -> unsigned {
        return size_;
    }

    auto data() const noexcept -> const char* __restrict {
        return std::launder(reinterpret_cast<const char* __restrict>(this) + 4);
    }

    auto data() noexcept -> char* __restrict {
        return std::launder(reinterpret_cast<char* __restrict>(this) + 4);
    }

    static auto create(std::string_view sv) -> trailing_string* {
        void* mem = allocator::allocate(4 + sv.size() + 1);
        return new (mem) trailing_string(sv);
    }

    auto sv() const noexcept -> std::string_view {
        return std::string_view(data(), size());
    }

    ~trailing_string() noexcept {
        allocator::deallocate(this);
    }
};

}
