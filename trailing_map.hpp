#pragma once
#include "tiny_string.hpp"
#include <functional>
#include <memory>

namespace ineffa {

// It is recommended to insert frequently accessed elements first
template <class T, class allocator>
class alignas(16) trailing_map {
    unsigned size_;
    unsigned capacity_;

    trailing_map(unsigned size, unsigned capacity) noexcept : size_(size), capacity_(capacity) {}

public:
    using key_string = ineffa::tiny_string<allocator>;

    static auto create(unsigned capacity = 4) -> trailing_map* {
        void *mem = allocator::allocate(sizeof(trailing_map) + capacity * (sizeof(key_string) + sizeof(T)), std::align_val_t(16));
        return new (mem) trailing_map(0, capacity);
    }

    static auto create(key_string* __restrict keys_data, T* __restrict values_data, unsigned data_size) -> trailing_map*
        requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; } 
    {
        void *mem = allocator::allocate(sizeof(trailing_map) + data_size * (sizeof(key_string) + sizeof(T)), std::align_val_t(16));
        trailing_map* map = new (mem) trailing_map(data_size, data_size);
        std::memcpy((void*)map->keys(), (void*)keys_data, data_size * sizeof(key_string));
        std::memcpy((void*)map->values(), (void*)values_data, data_size * sizeof(T));
        return map;
    }

    auto find_index(const std::string_view target_key) const noexcept -> int {
        const key_string* __restrict keys = std::assume_aligned<16>(this->keys());
        const int n = size();

        if (target_key.length() <= 15) {
           alignas(16) const key_string target_key_bin = target_key;
            for (int i = 0; i < n; i++)
                if (std::memcmp((void*)(keys + i), (void*)&target_key_bin, 16) == 0) return i;
        }
        else {
            for (int i = 0; i < n; i++)
                if (keys[i].sv() == target_key) return i;
        }
        return -1;
    }

    auto try_emplace(auto* owner, std::invocable<decltype(owner), trailing_map*> auto set_ptr_fn, std::string_view key, auto&&... value_args) 
        -> std::pair<T*, bool> requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        if (int index = find_index(key); index >= 0)
            return { values() + index, false };

        auto* self = this;
        if (size_ == capacity_)
            self = recapacity(owner, std::move(set_ptr_fn), capacity_ == 0 ? 4 : capacity_ * 3 / 2);

        key_string key_data = key_string(key);
        std::construct_at(self->values() + self->size_, std::forward<decltype(value_args)>(value_args)...);
        std::construct_at(self->keys() + self->size_, std::move(key_data));
        return { self->values() + self->size_++, true };
    }

    auto try_emplace(auto* owner, std::invocable<decltype(owner), trailing_map*> auto set_ptr_fn, key_string&& key, auto&&... value_args) 
        -> std::pair<T*, bool> requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        if (int index = find_index(key); index >= 0)
            return { values() + index, false };

        auto* self = this;
        if (size_ == capacity_)
            self = recapacity(owner, std::move(set_ptr_fn), capacity_ == 0 ? 4 : capacity_ * 3 / 2);

        std::construct_at(self->values() + self->size_, std::forward<decltype(value_args)>(value_args)...);
        std::construct_at(self->keys() + self->size_, std::move(key));
        return { self->values() + self->size_++, true };
    }

    void remove(unsigned i) noexcept
        requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        if (i >= size_) [[unlikely]]
            return;

        key_string* keys = std::assume_aligned<16>(this->keys());
        T* values = this->values();

        std::destroy_at(keys + i);
        std::destroy_at(values + i);

        if (i != size_ - 1) {
            std::memmove((void*)(keys + i),(void*)(keys + i + 1), sizeof(key_string) * (size_ - 1 - i));
            std::memmove((void*)(values + i), (void*)(values + i + 1), sizeof(T) * (size_ - 1 - i));
        }
        size_--;
    }

    auto recapacity(auto* owner, std::invocable<decltype(owner), trailing_map*> auto set_ptr_fn, unsigned new_capacity) -> trailing_map*
        requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
    {
        new_capacity = std::max(size_, new_capacity);

        auto* new_map = (decltype(this))allocator::reallocate(this, sizeof(trailing_map) + new_capacity * (sizeof(key_string) + sizeof(T)), std::align_val_t(16));
        std::memmove((void*)(new_map->keys() + new_capacity), (void*)(new_map->values()), sizeof(T) * new_map->size_);
        new_map->capacity_ = new_capacity;
        
        std::invoke(set_ptr_fn, owner, new_map); 
        return new_map;
    }

    auto size() const noexcept -> unsigned {
        return size_;
    }    

    auto capacity() const noexcept -> unsigned {
        return capacity_;
    }

    auto keys() noexcept -> key_string* {
        return std::launder(reinterpret_cast<key_string*>(this + 1));
    }

    auto values() noexcept -> T* {
        return std::launder(reinterpret_cast<T*>((char*)(this + 1) + capacity_ * sizeof(key_string)));
    }

    auto keys() const noexcept -> const key_string* {
        return std::launder(reinterpret_cast<const key_string*>(this + 1));
    }

    auto values() const noexcept -> const T* {
        return std::launder(reinterpret_cast<const T*>((const char*)(this + 1) + capacity_ * sizeof(key_string)));
    }

    ~trailing_map() noexcept {
        std::destroy_n(keys(), size());
        std::destroy_n(values(), size());
        allocator::deallocate(this, std::align_val_t(16));
    }
};

}
