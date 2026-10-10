#include "agent/tool_result_budget.hpp"
#include <algorithm>

namespace agent {

std::string utf8_prefix(std::string_view text, size_t bytes) {
    size_t end = std::min(text.size(), bytes);
    while (end < text.size() && end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return std::string(text.substr(0, end));
}

JsonValue bound_tool_content(JsonValue content, size_t max_bytes) {
    if (to_json_string(content).size() <= max_bytes)
        return content;

    // Trim only payload fields: paths, exit codes and continuation metadata stay intact.
    JsonValue* text = nullptr;
    if (content.is_string()) {
        text = &content;
    } else if (content.is_object()) {
        for (const auto* key : {"content", "stdout", "stderr", "output"}) {
            if (content.contains(key) && content[key].is_string() &&
                (!text || content[key].get_ref<const std::string&>().size() >
                              text->get_ref<const std::string&>().size()))
                text = &content[key];
        }
    }
    if (text) {
        const auto original = text->get<std::string>();
        const std::string marker = "\n[truncated]\n";
        if (content.is_object())
            content["truncated"] = true;
        auto project = [&](size_t keep) {
            // Keep a diagnostic tail for command output, but file pages keep a prefix.
            const bool tail = content.is_object() && !content.contains("path") && keep >= 4;
            const size_t tail_bytes = tail ? keep / 4 : 0;
            size_t tail_start = original.size() - tail_bytes;
            while (tail_start < original.size() &&
                   (static_cast<unsigned char>(original[tail_start]) & 0xc0) == 0x80)
                ++tail_start;
            *text = utf8_prefix(original, keep - tail_bytes) + marker + original.substr(tail_start);
        };
        size_t low = 0, high = original.size();
        project(0);
        if (to_json_string(content).size() <= max_bytes) {
            while (low < high) {
                const size_t mid = low + (high - low + 1) / 2;
                project(mid);
                if (to_json_string(content).size() <= max_bytes)
                    low = mid;
                else
                    high = mid - 1;
            }
            project(low);
            return content;
        }
        // Multiple streams may be oversized. Shrink the largest progressively so
        // another stream gets a share rather than discarding one stream outright.
        if (original.size() > 32)
            project(original.size() / 2);
        else
            *text = "";
        if (to_json_string(content).size() <= max_bytes)
            return content;
        if (content.is_object() && !original.empty())
            return bound_tool_content(std::move(content), max_bytes);
    }
    if (content.is_array()) {
        size_t used =
            to_json_string(JsonValue{{"items", JsonValue::array()}, {"truncated", true}}).size();
        size_t count = 0;
        for (const auto& item : content) {
            const size_t bytes = to_json_string(item).size() + (count > 0 ? 1 : 0);
            if (bytes > max_bytes - std::min(used, max_bytes))
                break;
            used += bytes;
            ++count;
        }
        content.erase(content.begin() + static_cast<JsonValue::difference_type>(count),
                      content.end());
        JsonValue result{{"items", std::move(content)}, {"truncated", true}};
        if (to_json_string(result).size() <= max_bytes)
            return result;
    }
    JsonValue marker = "[tool result truncated]";
    return to_json_string(marker).size() <= max_bytes ? marker : JsonValue(0);
}

} // namespace agent
