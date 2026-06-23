#pragma once
#include <expected>
#include <charconv>
#include "json_object.hpp"

namespace ineffa::json {

struct parser_error {
    const char* msg;
    int line, col;
};

namespace details {

template <class T>
    requires std::is_trivially_copyable_v<T> || requires { typename T::trivially_relocatable; }
struct parser_stack {
    T* __restrict data = nullptr;
    unsigned capacity = 0;
    unsigned size = 0;

    parser_stack(unsigned init_capacity = 64) :
        data(json::allocator::allocate_n<T>(init_capacity)),
        capacity(init_capacity),
        size(0)
    {}

    void reserve(unsigned new_capacity) {
        if (new_capacity > capacity) [[unlikely]] {
            new_capacity = std::max(new_capacity, capacity * 3 / 2);
            data = json::allocator::reallocate_n<T>(data, new_capacity);
            capacity = new_capacity;
        }
    }

    void append(const T* __restrict src, unsigned src_size) noexcept {
        reserve(size + src_size);
        std::memcpy(data + size, src, src_size * sizeof(T));
        size += src_size;
    }

    void uncheck_push(const T val) noexcept {
        data[size++] = std::move(val);
    }

    void emplace_back(auto&&... args) {
        reserve(size + 1);
        std::construct_at(data + size, std::forward<decltype(args)>(args)...);
        size++;
    }

    void clear() noexcept {
        std::destroy_n(data, size);
        size = 0;
    }

    ~parser_stack() noexcept {
        std::destroy_n(data, size);
        json::allocator::deallocate(data);
    }
}; // struct ineffa::json::details::parser_stack

struct parser {
    const char* current;
    const char* end;
    details::parser_stack<json::object::map_t::key_string> keys_buffer;
    details::parser_stack<json::object> values_buffer;
    details::parser_stack<char> string_buffer;
    const char* error;

    static constexpr int max_depth = 128;

    void init(std::string_view content) noexcept {
        error = "";
        current = content.data();
        end = content.data() + content.length();
        keys_buffer.clear();
        values_buffer.clear();
    }

    void skip_space() noexcept {
        while (current < end) {
            if (char c = *current; c == ' ' || (c >= '\t' && c <= '\r'))
                current++;

            else if (c == '/') [[unlikely]] {
                if (current + 1 < end and current[1] == '/')
                    for (current += 2; current < end and *current != '\n'; current++);
                else [[unlikely]] break;
            }

            else break;
        }
    }

    bool match(char expected) noexcept {
        return *current == expected ? (++current, true) : false;
    }

    bool parse_string() {
        const char* last_parsed_end = current;

        for (;;current++) {
            if (current == end) [[unlikely]]
                return (error = "unterminated string", false);

            if (match('"'))
                break;

            else if(match('\\')) {
                if (last_parsed_end < current - 1)
                    string_buffer.append(last_parsed_end, current - 1 - last_parsed_end);

                if (current == end) [[unlikely]]
                    return (error = "unterminated string", false);

                string_buffer.reserve(string_buffer.size + 4);
                switch (*current) {
                    case '\\': string_buffer.uncheck_push('\\'); break;
                    case '/':  string_buffer.uncheck_push('/');  break;
                    case '"':  string_buffer.uncheck_push('"');  break;
                    case 'f':  string_buffer.uncheck_push('\f'); break;
                    case 'n':  string_buffer.uncheck_push('\n'); break;
                    case 'r':  string_buffer.uncheck_push('\r'); break;
                    case 't':  string_buffer.uncheck_push('\t'); break;
                    case 'b':  string_buffer.uncheck_push('\b'); break;
                    case 'u': {
                        auto read_hex4 = [&]() -> uint32_t {
                            if (current + 4 >= end) [[unlikely]] return ~0u;
                            uint32_t u = 0;
                            for (int i = 0; i < 4; i++) {
                                char c = *++current;
                                u = (u << 4) | (
                                        c >= '0' and c <= '9' ? c - '0' :
                                        c >= 'a' and c <= 'f' ? c - 'a' + 10 :
                                        c >= 'A' and c <= 'F' ? c - 'A' + 10  : ~0u);
                            }
                            return u;
                        };

                        uint32_t u = read_hex4();

                        if (u == ~0u) [[unlikely]]
                            return (error = "invalid unicode hex", false);

                        else if (u >= 0xD800 && u <= 0xDBFF) {
                            if (current + 2 < end && current[1] == '\\' and current[2] == 'u') {
                                current += 2;

                                if (uint32_t u2 = read_hex4(); u2 >= 0xDC00 and u2 <= 0xDFFF)
                                    u = 0x10000 + (((u - 0xD800) << 10) | (u2 - 0xDC00));

                                else [[unlikely]] return (error = "invalid surrogate pair", false);
                            }
                        }

                        if (u <= 0x7F) {
                            string_buffer.uncheck_push(u);
                        }
                        else if (u <= 0x7FF) {
                            string_buffer.uncheck_push(0xC0 | (u >> 6));
                            string_buffer.uncheck_push(0x80 | (u & 0x3F));
                        }
                        else if (u <= 0xFFFF) {
                            string_buffer.uncheck_push(0xE0 | (u >> 12));
                            string_buffer.uncheck_push(0x80 | ((u >> 6) & 0x3F));
                            string_buffer.uncheck_push(0x80 | (u & 0x3F));
                        }
                        else {
                            string_buffer.uncheck_push(0xF0 | (u >> 18));
                            string_buffer.uncheck_push(0x80 | ((u >> 12) & 0x3F));
                            string_buffer.uncheck_push(0x80 | ((u >> 6) & 0x3F));
                            string_buffer.uncheck_push(0x80 | (u & 0x3F));
                        }
                        break;
                    }
                    default: return (error = "invalid escape character", false);
                }

                last_parsed_end = current + 1;
            }
        }

        if (last_parsed_end < current - 1)
            string_buffer.append(last_parsed_end, current - 1 - last_parsed_end);

        return true;
    }

    bool parse_key() {
        if (match('"')) {
            if (not parse_string()) [[unlikely]] return false;
            keys_buffer.emplace_back(std::string_view(string_buffer.data, string_buffer.size));
            string_buffer.clear();
        }

        else {
            const char* start = current;
            while (current < end) {
                if (const char c = *current; c == ':') break;
                else if (c == '\n' || c == '\r') [[unlikely]]
                    return (error = "unexpected newline in unquoted key", false);
                else current++;
            }

            if (current == start) [[unlikely]]
                return (error = "empty or invalid key", false);

            keys_buffer.emplace_back(std::string_view(start, current - start));
        }

        return true;
    }

    bool parse_value(const int depth = 0) {
        if (match('[')) {
            if (depth > max_depth) [[unlikely]]
                return (error = "exceeded max depth", false);

            const unsigned start_buffer_size = values_buffer.size;

            while (true) {
                if (skip_space(); current == end) [[unlikely]]
                    return (error = "unexpected end of array", false);

                if (match(']')) break;

                else if (not parse_value(depth + 1)) [[unlikely]] return false;

                if (skip_space(); current == end) [[unlikely]]
                    return (error = "unexpected end of array", false);
                
                match(',');
            }

            unsigned values_count = values_buffer.size - start_buffer_size;
            auto* arr = json::object::array_t::create(values_buffer.data + start_buffer_size, values_count);
            values_buffer.size = start_buffer_size;
            values_buffer.emplace_back(arr);
        }

        else if (match('{')) {
            if (depth > max_depth) [[unlikely]]
                return (error = "exceeded max depth", false);

            const unsigned start_keys_size = keys_buffer.size;
            const unsigned start_values_size = values_buffer.size;

            while (true) {
                if (skip_space(); current == end) [[unlikely]]
                    return (error = "unexpected end of map", false);
 
                if (match('}')) break;

                if (not parse_key()) [[unlikely]] return false;

                if (skip_space(); current == end) [[unlikely]]
                    return (error = "unexpected end of map", false);

                match(':');

                if (skip_space(); current == end) [[unlikely]]
                    return (error = "unexpected end of map", false);

                if (not parse_value(depth + 1)) [[unlikely]] return false;

               if (skip_space(); current == end) [[unlikely]]
                    return (error = "unexpected end of map", false);
 
                match(',');
            }

            unsigned values_count = values_buffer.size - start_values_size;
            auto* map = json::object::map_t::create(keys_buffer.data + start_keys_size, values_buffer.data + start_values_size, values_count);
            keys_buffer.size = start_keys_size;
            values_buffer.size = start_values_size;
            values_buffer.emplace_back(map);
        }

        else if (match('"')) {
            if (not parse_string()) [[unlikely]] return false;
            values_buffer.emplace_back(std::string_view(string_buffer.data, string_buffer.size));
            string_buffer.clear();
        }

        else if (char c = *current; (c >= '0' and c <= '9') or c == '-') {
            double val;
            auto [ptr, ec] = std::from_chars(current, end, val);
            if (ec == std::errc()) {
                values_buffer.emplace_back(val);
                current = ptr;
            }
            else [[unlikely]] return (error = "illegal number", false);
        }

        else if (c == 't') {
            if (end - current >= 4 and current[1] == 'r' and current[2] == 'u' and current[3] == 'e') {
                values_buffer.emplace_back(true);
                current += 4;
            }
            else [[unlikely]] return (error = "unexpected character", false);
        }

        else if (c == 'f') {
            if (end - current >= 5 and current[1] == 'a' and current[2] == 'l' and current[3] == 's' and current[4] == 'e') {
                values_buffer.emplace_back(false);
                current += 5;
            }
            else [[unlikely]] return (error = "unexpected character", false);
        }

        else if (c == 'n') {
            if (end - current >= 4 and current[1] == 'u' and current[2] == 'l' and current[3] == 'l') {
                values_buffer.emplace_back(uint64_t(1 << 3), json::TYPE_NULL);
                current += 4;
            }
            else [[unlikely]] return (error = "unexpected character", false);
        }

        else [[unlikely]] return (error = "unexpected character", false);

        return true;
    }
}; // struct ineffa::json::details::struct parser

} // namespace ineffa::json::details

inline auto parse(std::string_view content) noexcept -> std::expected<json::object, json::parser_error> {
    alignas(64) details::parser parser;
    parser.init(content);
    parser.skip_space();
    if (parser.current == parser.end) 
        return json::object(1 << 3 | json::TYPE_NULL);

    bool is_success = parser.parse_value();
    if (is_success)
        return std::move(parser.values_buffer.data[0]);

    std::string_view p = std::string_view(content.data(), parser.current - content.data());
    int line = std::count(p.begin(), p.end(), '\n') + 1;
    int col = p.size() - p.find_last_of('\n');

    return std::unexpected(json::parser_error {
        .msg = parser.error,
        .line = line,
        .col = col
    });
}

} // namesapce ineffa::json
