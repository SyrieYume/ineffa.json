#pragma once
#include <functional>
#include <memory>
#include <span>
#include <cstring>

namespace ineffa {

template <class T, class allocator>
class trailing_array {
    unsigned size_ = 0;
    unsigned capacity_ = 0;

    trailing_array(unsigned size, unsigned capacity) noexcept : size_(size), capacity_(capacity) {}

public:
    static auto create(unsigned capacity = 4) -> trailing_array* {
        void *mem = allocator::allocate(sizeof(trailing_array) + capacity * sizeof(T));
        return new (mem) trailing_array(0, capacity);
    }

    static auto create(T* __restrict data, unsigned data_size) -> trailing_array*
        requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        void *mem = allocator::allocate(sizeof(trailing_array) + data_size * sizeof(T));
        trailing_array* arr = new (mem) trailing_array(data_size, data_size);
        std::memcpy((void*)arr->data(), (void*)data, data_size * sizeof(T));
        return arr;
    }

    auto size() const noexcept -> unsigned {
        return size_;
    }

    auto capacity() const noexcept -> unsigned {
        return capacity_;
    }

    auto data() noexcept -> T* __restrict {
        return std::launder(reinterpret_cast<T*>(this + 1));
    }

    auto data() const noexcept -> const T* __restrict {
        return std::launder(reinterpret_cast<const T*>(this + 1));
    }

    auto recapacity(auto* owner, std::invocable<decltype(owner), trailing_array*> auto set_ptr_fn, unsigned new_capacity) -> trailing_array*
        requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        new_capacity = std::max(size_, new_capacity);

        auto* new_arr = (decltype(this))allocator::reallocate(this, sizeof(trailing_array) + new_capacity * sizeof(T));
        new_arr->capacity_ = new_capacity;
        
        std::invoke(set_ptr_fn, owner, new_arr); 
        return new_arr;
    }

    auto emplace_pack(auto* owner, std::invocable<decltype(owner), trailing_array*> auto set_ptr_fn, auto&& arg) -> trailing_array* {
        trailing_array* self = this;

        if (size_ == capacity_) [[unlikely]]
            self = recapacity(owner, std::move(set_ptr_fn), capacity_ == 0 ? 4 : capacity_ * 3 / 2);
        
        std::construct_at(self->data() + self->size_, std::forward<decltype(arg)>(arg));
        self->size_++;
        return self;
    }

    void remove(unsigned i) noexcept
        requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        if (i >= size_) [[unlikely]]
            return;

        std::destroy_at(data() + i);

        if (i != --size_)
            std::memmove((void*)(data() + i), (void*)(data() + i + 1), sizeof(T) * (size_ - i));
    }

    auto span() noexcept -> std::span<T> {
        return std::span(data(), size());
    }

    auto span() const noexcept -> std::span<const T> {
        return std::span(data(), size());
    }

    ~trailing_array() noexcept {
        std::destroy_n(data(), size());
        allocator::deallocate(this);
    }
};

}
