#include "index/index_key.h"

#include "index/bucket_page.h"

#include <cstdint>
#include <type_traits>

namespace minidbms {

Status IndexKey::Encode(
    const FieldValue& value,
    std::string* encoded_key
) {
    if (encoded_key == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Encoded index key output cannot be null"
        );
    }

    encoded_key->clear();

    if (std::holds_alternative<std::monostate>(value)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "NULL values are not indexed"
        );
    }

    if (const auto* integer = std::get_if<int32_t>(&value)) {
        encoded_key->push_back(static_cast<char>(1));

        const uint32_t raw = static_cast<uint32_t>(*integer);
        for (int shift = 0; shift < 32; shift += 8) {
            encoded_key->push_back(
                static_cast<char>((raw >> shift) & 0xFFU)
            );
        }

        return Status::OK();
    }

    if (const auto* text = std::get_if<std::string>(&value)) {
        if (text->size() + 1 > HashIndexBucketPage::MAX_KEY_LENGTH) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "VARCHAR value exceeds hash index key length"
            );
        }

        encoded_key->push_back(static_cast<char>(2));
        encoded_key->append(*text);
        return Status::OK();
    }

    if (const auto* boolean = std::get_if<bool>(&value)) {
        encoded_key->push_back(static_cast<char>(3));
        encoded_key->push_back(*boolean ? static_cast<char>(1)
                                        : static_cast<char>(0));
        return Status::OK();
    }

    return Status(
        StatusCode::INVALID_ARGUMENT,
        "Unsupported index key type"
    );
}

} // namespace minidbms
