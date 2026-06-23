#pragma once
#include <charconv>
#include "json_object.hpp"

namespace ineffa::json {

template <bool pretty = true>
void stringfy_to(std::string& out, const json::object& obj, const int indent = 2, const int current_indent = 0) {
    auto dump_string_to = [](std::string& out, std::string_view sv) {
        out.reserve(out.size() + sv.size() + 2);
        out += '"';

        std::ptrdiff_t last_pos = 0;
        for (std::ptrdiff_t i = 0; i < std::ssize(sv); i++) {
            unsigned char c = sv[i];

            if (c < 0x20 or c == '"' or c == '\\') {
                if (i > last_pos)
                    out.append(sv.data() + last_pos, i - last_pos);

                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: [[unlikely]] {
                        static constexpr char hex_digits[] = "0123456789abcdef";
                        const char buf[] = {'\\', 'u', '0', '0', hex_digits[c >> 4], hex_digits[c & 0x0F]};
                        out.append(buf, sizeof buf);
                        break;
                    }
                }

                last_pos = i + 1;
            }
        }

        if (std::ssize(sv) > last_pos)
            out.append(sv.data() + last_pos, std::ssize(sv) - last_pos);

        out += '"';
    };

    switch (obj.type()) {
        case TYPE_BOOL: out += *obj.as_bool() ? "true" : "false"; break;

        case TYPE_NUMBER:
            out.resize_and_overwrite(out.size() + 32, [&obj, old_size=out.size()](char* buf, size_t buf_size) {
                auto [ptr, ec] = std::to_chars(buf + old_size, buf + buf_size, *obj.as_double(), std::chars_format::general, 14);
                if (ec == std::errc())
                    return size_t(ptr - buf);
                else [[unlikely]] return old_size;
            });
            break;

        case TYPE_SSO_STRING:
        case TYPE_STRING:
            dump_string_to(out, *obj.as_string()); 
            break; 

        case TYPE_ARRAY:
            out += '[';
            if (auto vals = obj.values(); not vals.empty()) {
                for (const auto& [i, item] : vals | std::ranges::views::enumerate) {
                    if (i > 0) out += ',';
                    if constexpr (pretty) {
                        out += '\n';
                        out.append(current_indent + indent, ' ');
                    }
                    json::stringfy_to<pretty>(out, item, indent, current_indent + indent);
                }
                if constexpr (pretty) {
                    out += '\n';
                    out.append(current_indent, ' ');
                }
            }
            out += ']';
            break;

        case TYPE_MAP: {
            out += '{';
            bool first = true;
            for (auto&& [key, val] : obj.key_values()) {
                if (not first) out += ',';
                else first = false;

                if constexpr (pretty) {
                    out += '\n';
                    out.append(current_indent + indent, ' ');
                }

                dump_string_to(out, key);
                out += indent >= 0 ? ": " : ":";
                json::stringfy_to<pretty>(out, val, indent, current_indent + indent);
            }

            if (not first and indent >= 0) {
                out += '\n';
                out.append(current_indent, ' ');
            }
            out += '}';
            break;
        }

        case TYPE_NULL:
        default: out += "null"; break;
    }
}

template <bool pretty = true>
auto stringfy(const json::object& obj, const int indent = 2) -> std::string {
    std::string result;
    json::stringfy_to<pretty>(result, obj, indent, 0);
    return result;
}

}
