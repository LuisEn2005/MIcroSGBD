#include "storage/record_codec.h"

#include <cstring>
#include <limits>

namespace minidbms {
namespace {

std::size_t NullBitmapSize(std::size_t column_count) {
    return (column_count + 7U) / 8U;
}

bool IsNull(const std::vector<uint8_t>& bitmap, std::size_t index) {
    return (bitmap[index / 8U] & static_cast<uint8_t>(1U << (index % 8U))) != 0;
}

void SetNull(std::vector<uint8_t>* bitmap, std::size_t index) {
    (*bitmap)[index / 8U] |= static_cast<uint8_t>(1U << (index % 8U));
}

template <typename T>
void AppendValue(std::vector<char>* output, const T& value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    output->insert(output->end(), bytes, bytes + sizeof(T));
}

template <typename T>
T ReadValue(const char* data) {
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

Status ValidateOffset(uint16_t offset, std::size_t payload_size) {
    if (offset > payload_size) {
        return Status::IOError("Corrupted record column offset");
    }
    return Status::OK();
}

} // namespace

Status RecordCodec::Serialize(
    const Schema& schema,
    const std::vector<FieldValue>& values,
    Record* record
) {
    if (record == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record output cannot be null"
        );
    }

    const auto& columns = schema.GetColumns();
    if (values.size() != columns.size()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Value count does not match schema"
        );
    }

    if (columns.size() > std::numeric_limits<uint16_t>::max()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Schema has too many columns"
        );
    }

    const uint16_t column_count = static_cast<uint16_t>(columns.size());
    const std::size_t bitmap_size = NullBitmapSize(column_count);
    const std::size_t header_size =
        sizeof(uint16_t) + bitmap_size + column_count * sizeof(uint16_t);

    if (header_size > std::numeric_limits<uint16_t>::max()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record header is too large"
        );
    }

    std::vector<uint8_t> null_bitmap(bitmap_size, 0);
    std::vector<uint16_t> offsets(column_count, 0);
    std::vector<char> payload;

    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (payload.size() > std::numeric_limits<uint16_t>::max()) {
            return Status::OutOfMemory("Record payload is too large");
        }

        offsets[index] = static_cast<uint16_t>(payload.size());
        const FieldValue& value = values[index];

        if (std::holds_alternative<std::monostate>(value)) {
            SetNull(&null_bitmap, index);
            continue;
        }

        switch (columns[index].type) {
            case TypeId::INTEGER: {
                const auto* integer_value = std::get_if<int32_t>(&value);
                if (integer_value == nullptr) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "INTEGER column received an incompatible value"
                    );
                }
                AppendValue(&payload, *integer_value);
                break;
            }

            case TypeId::BOOLEAN: {
                const auto* boolean_value = std::get_if<bool>(&value);
                if (boolean_value == nullptr) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "BOOLEAN column received an incompatible value"
                    );
                }
                payload.push_back(*boolean_value ? 1 : 0);
                break;
            }

            case TypeId::VARCHAR: {
                const auto* string_value = std::get_if<std::string>(&value);
                if (string_value == nullptr) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "VARCHAR column received an incompatible value"
                    );
                }

                if (columns[index].length > 0 &&
                    string_value->size() > columns[index].length) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "VARCHAR value exceeds the declared length"
                    );
                }

                payload.insert(
                    payload.end(),
                    string_value->begin(),
                    string_value->end()
                );
                break;
            }
        }
    }

    const std::size_t total_size = header_size + payload.size();
    if (total_size > std::numeric_limits<uint16_t>::max()) {
        return Status::OutOfMemory("Serialized record is too large");
    }

    std::vector<char> serialized(total_size, 0);
    std::size_t cursor = 0;

    std::memcpy(serialized.data() + cursor, &column_count, sizeof(column_count));
    cursor += sizeof(column_count);

    if (!null_bitmap.empty()) {
        std::memcpy(serialized.data() + cursor, null_bitmap.data(), bitmap_size);
        cursor += bitmap_size;
    }

    if (!offsets.empty()) {
        std::memcpy(
            serialized.data() + cursor,
            offsets.data(),
            offsets.size() * sizeof(uint16_t)
        );
        cursor += offsets.size() * sizeof(uint16_t);
    }

    if (!payload.empty()) {
        std::memcpy(serialized.data() + cursor, payload.data(), payload.size());
    }

    record->SetData(serialized.data(), static_cast<uint32_t>(serialized.size()));
    return Status::OK();
}

Status RecordCodec::Deserialize(
    const Schema& schema,
    const Record& record,
    std::vector<FieldValue>* values
) {
    if (values == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Values output cannot be null"
        );
    }

    const char* data = record.GetData();
    const std::size_t record_size = record.GetSize();

    if (data == nullptr || record_size < sizeof(uint16_t)) {
        return Status::IOError("Record is too small to contain a header");
    }

    const uint16_t stored_column_count = ReadValue<uint16_t>(data);
    const auto& columns = schema.GetColumns();

    if (stored_column_count != columns.size()) {
        return Status::IOError("Record column count does not match schema");
    }

    const std::size_t bitmap_size = NullBitmapSize(stored_column_count);
    const std::size_t header_size =
        sizeof(uint16_t) +
        bitmap_size +
        stored_column_count * sizeof(uint16_t);

    if (header_size > record_size) {
        return Status::IOError("Corrupted record header");
    }

    std::vector<uint8_t> null_bitmap(bitmap_size, 0);
    std::vector<uint16_t> offsets(stored_column_count, 0);

    std::size_t cursor = sizeof(uint16_t);
    if (bitmap_size > 0) {
        std::memcpy(null_bitmap.data(), data + cursor, bitmap_size);
        cursor += bitmap_size;
    }

    if (stored_column_count > 0) {
        std::memcpy(
            offsets.data(),
            data + cursor,
            stored_column_count * sizeof(uint16_t)
        );
    }

    const char* payload = data + header_size;
    const std::size_t payload_size = record_size - header_size;

    uint16_t previous_offset = 0;
    for (uint16_t offset : offsets) {
        Status status = ValidateOffset(offset, payload_size);
        if (!status.ok()) {
            return status;
        }
        if (offset < previous_offset) {
            return Status::IOError("Record column offsets are not ordered");
        }
        previous_offset = offset;
    }

    values->clear();
    values->reserve(stored_column_count);

    for (std::size_t index = 0; index < columns.size(); ++index) {
        const std::size_t start = offsets[index];
        const std::size_t end =
            index + 1 < offsets.size() ? offsets[index + 1] : payload_size;

        if (end < start || end > payload_size) {
            return Status::IOError("Corrupted record column bounds");
        }

        if (IsNull(null_bitmap, index)) {
            values->emplace_back(std::monostate{});
            continue;
        }

        const std::size_t field_size = end - start;
        const char* field_data = payload + start;

        switch (columns[index].type) {
            case TypeId::INTEGER:
                if (field_size != sizeof(int32_t)) {
                    return Status::IOError("Corrupted INTEGER field");
                }
                values->emplace_back(ReadValue<int32_t>(field_data));
                break;

            case TypeId::BOOLEAN:
                if (field_size != sizeof(uint8_t)) {
                    return Status::IOError("Corrupted BOOLEAN field");
                }
                values->emplace_back(field_data[0] != 0);
                break;

            case TypeId::VARCHAR:
                values->emplace_back(std::string(field_data, field_size));
                break;
        }
    }

    return Status::OK();
}

Status RecordCodec::GetValue(
    const Schema& schema,
    const Record& record,
    uint32_t column_index,
    FieldValue* value
) {
    if (value == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Value output cannot be null"
        );
    }

    std::vector<FieldValue> values;
    Status status = Deserialize(schema, record, &values);
    if (!status.ok()) {
        return status;
    }

    if (column_index >= values.size()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Column index is out of range"
        );
    }

    *value = values[column_index];
    return Status::OK();
}

Status RecordCodec::Project(
    const Schema& input_schema,
    const Record& input_record,
    const std::vector<uint32_t>& column_indices,
    Record* output_record
) {
    if (output_record == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Projected record output cannot be null"
        );
    }

    std::vector<FieldValue> input_values;
    Status status = Deserialize(input_schema, input_record, &input_values);
    if (!status.ok()) {
        return status;
    }

    std::vector<Column> projected_columns;
    std::vector<FieldValue> projected_values;
    projected_columns.reserve(column_indices.size());
    projected_values.reserve(column_indices.size());

    const auto& input_columns = input_schema.GetColumns();
    for (uint32_t index : column_indices) {
        if (index >= input_columns.size()) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Projection column index is out of range"
            );
        }
        projected_columns.push_back(input_columns[index]);
        projected_values.push_back(input_values[index]);
    }

    Record projected;
    status = Serialize(
        Schema(std::move(projected_columns)),
        projected_values,
        &projected
    );

    if (!status.ok()) {
        return status;
    }

    projected.SetRecordID(input_record.GetRecordID());
    *output_record = std::move(projected);
    return Status::OK();
}

} // namespace minidbms
