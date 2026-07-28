#ifndef MINI_DBMS_QUERY_LITERAL_H
#define MINI_DBMS_QUERY_LITERAL_H

#include "catalog/schema.h"
#include "common/status.h"
#include "storage/record_codec.h"

#include <string>

namespace minidbms {

enum class LiteralKind {
    NUMBER,
    STRING,
    IDENTIFIER,
    NULL_VALUE
};

struct SQLLiteral {
    LiteralKind kind{LiteralKind::IDENTIFIER};
    std::string text;
};

Status ConvertLiteral(
    const Column& column,
    const SQLLiteral& literal,
    bool allow_null,
    FieldValue* value
);

} // namespace minidbms

#endif // MINI_DBMS_QUERY_LITERAL_H
