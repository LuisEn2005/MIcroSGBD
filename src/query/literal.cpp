#include "query/literal.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace minidbms {

Status ConvertLiteral(
    const Column& column,
    const SQLLiteral& literal,
    bool allow_null,
    FieldValue* value
) {
    if (value == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Literal output cannot be null"
        );
    }

    if (literal.kind == LiteralKind::NULL_VALUE) {
        if (!allow_null) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "NULL is not supported in this predicate"
            );
        }

        *value = std::monostate{};
        return Status::OK();
    }

    try {
        switch (column.type) {
            case TypeId::INTEGER: {
                if (literal.kind != LiteralKind::NUMBER) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "INTEGER values must use an unquoted numeric literal"
                    );
                }

                std::size_t consumed = 0;
                const long long parsed = std::stoll(
                    literal.text,
                    &consumed
                );

                if (consumed != literal.text.size() ||
                    parsed < std::numeric_limits<int32_t>::min() ||
                    parsed > std::numeric_limits<int32_t>::max()) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "Invalid INTEGER literal"
                    );
                }

                *value = static_cast<int32_t>(parsed);
                return Status::OK();
            }

            case TypeId::VARCHAR:
                if (literal.kind != LiteralKind::STRING) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "VARCHAR values must be quoted"
                    );
                }

                if (column.length > 0 &&
                    literal.text.size() > column.length) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "VARCHAR literal exceeds declared length"
                    );
                }

                *value = literal.text;
                return Status::OK();

            case TypeId::BOOLEAN: {
                if (literal.kind != LiteralKind::IDENTIFIER &&
                    literal.kind != LiteralKind::NUMBER) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "BOOLEAN values must be true, false, 1, or 0"
                    );
                }

                std::string normalized = literal.text;
                std::transform(
                    normalized.begin(),
                    normalized.end(),
                    normalized.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(
                            std::tolower(character)
                        );
                    }
                );

                if (normalized == "true" || normalized == "1") {
                    *value = true;
                    return Status::OK();
                }

                if (normalized == "false" || normalized == "0") {
                    *value = false;
                    return Status::OK();
                }

                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid BOOLEAN literal"
                );
            }
        }
    } catch (const std::exception&) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid typed literal"
        );
    }

    return Status(
        StatusCode::INVALID_ARGUMENT,
        "Unsupported literal type"
    );
}

} // namespace minidbms
