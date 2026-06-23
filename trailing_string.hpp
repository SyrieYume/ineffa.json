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
    static auto create(std::string_view sv) -> trailing_string* {
        std::byte* mem = allocator::allocate(sizeof(trailing_string) + sv.size() + 1);
        return new (mem) trailing_string(sv);
    }

  auto size() const noexcept -> unsigned {
        return size_;
    }

    auto data() const noexcept -> const char* __restrict {
        return std::launder(reinterpret_cast<const char*>(this + 1));
    }

    auto data() noexcept -> char* __restrict {
        return std::launder(reinterpret_cast<char*>(this + 1));
    }

    auto sv() const noexcept -> std::string_view {
        return std::string_view(data(), size());
    }

    ~trailing_string() noexcept {
        allocator::deallocate(this);
    }
};

}
