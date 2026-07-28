#ifndef MINI_DBMS_RECORD_CODEC_H
#define MINI_DBMS_RECORD_CODEC_H

#include "catalog/schema.h"
#include "common/status.h"
#include "storage/record.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace minidbms {

using FieldValue = std::variant<std::monostate, int32_t, std::string, bool>;

class RecordCodec {
public:
    static Status Serialize(
        const Schema& schema,
        const std::vector<FieldValue>& values,
        Record* record
    );

    static Status Deserialize(
        const Schema& schema,
        const Record& record,
        std::vector<FieldValue>* values
    );

    static Status GetValue(
        const Schema& schema,
        const Record& record,
        uint32_t column_index,
        FieldValue* value
    );

    static Status Project(
        const Schema& input_schema,
        const Record& input_record,
        const std::vector<uint32_t>& column_indices,
        Record* output_record
    );
};

} // namespace minidbms

#endif // MINI_DBMS_RECORD_CODEC_H
