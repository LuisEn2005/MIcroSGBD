#ifndef MINI_DBMS_QUERY_RESULT_H
#define MINI_DBMS_QUERY_RESULT_H

#include "storage/record_codec.h"

#include <string>
#include <vector>

namespace minidbms {

struct QueryResult {
    std::vector<std::string> column_names;
    std::vector<std::vector<FieldValue>> rows;

    void Reset() {
        column_names.clear();
        rows.clear();
    }
};

inline std::string FieldValueToString(const FieldValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "NULL";
    }

    if (const auto* integer = std::get_if<int32_t>(&value)) {
        return std::to_string(*integer);
    }

    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }

    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }

    return "";
}

} // namespace minidbms

#endif // MINI_DBMS_QUERY_RESULT_H
