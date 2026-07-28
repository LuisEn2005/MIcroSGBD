#include "index/hash_index_header_page.h"

#include <cstring>

namespace minidbms {
namespace {

template <typename T>
T ReadValue(const char* data, std::size_t offset) {
    T value{};
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteValue(char* data, std::size_t offset, const T& value) {
    std::memcpy(data + offset, &value, sizeof(T));
}

} // namespace

Status HashIndexHeaderPage::Init(
    uint16_t bucket_count,
    uint16_t max_key_length
) {
    if (page_ == nullptr || page_->GetPageId() == INVALID_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index header requires a valid page"
        );
    }

    if (bucket_count == 0 || bucket_count > MaxBucketCount()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid hash index bucket count"
        );
    }

    if (max_key_length == 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index key length must be greater than zero"
        );
    }

    char* data = page_->GetData();
    std::memset(data, 0, PAGE_SIZE);

    WriteValue<uint32_t>(data, MAGIC_OFFSET, MAGIC);
    WriteValue<uint16_t>(data, VERSION_OFFSET, VERSION);
    WriteValue<uint16_t>(data, BUCKET_COUNT_OFFSET, bucket_count);
    WriteValue<uint16_t>(data, MAX_KEY_LENGTH_OFFSET, max_key_length);
    WriteValue<uint16_t>(data, RESERVED_OFFSET, 0);

    for (uint16_t index = 0; index < bucket_count; ++index) {
        WriteValue<PageId>(
            data,
            DIRECTORY_OFFSET + static_cast<std::size_t>(index) * sizeof(PageId),
            INVALID_PAGE_ID
        );
    }

    page_->SetDirty(true);
    return Status::OK();
}

bool HashIndexHeaderPage::IsInitialized() const {
    if (page_ == nullptr) {
        return false;
    }

    const char* data = page_->GetData();

    if (ReadValue<uint32_t>(data, MAGIC_OFFSET) != MAGIC ||
        ReadValue<uint16_t>(data, VERSION_OFFSET) != VERSION) {
        return false;
    }

    const uint16_t bucket_count =
        ReadValue<uint16_t>(data, BUCKET_COUNT_OFFSET);

    return bucket_count > 0 && bucket_count <= MaxBucketCount();
}

uint16_t HashIndexHeaderPage::GetBucketCount() const {
    if (!IsInitialized()) {
        return 0;
    }

    return ReadValue<uint16_t>(
        page_->GetData(),
        BUCKET_COUNT_OFFSET
    );
}

uint16_t HashIndexHeaderPage::GetMaxKeyLength() const {
    if (!IsInitialized()) {
        return 0;
    }

    return ReadValue<uint16_t>(
        page_->GetData(),
        MAX_KEY_LENGTH_OFFSET
    );
}

Status HashIndexHeaderPage::GetBucketPageId(
    uint16_t bucket_index,
    PageId* bucket_page_id
) const {
    if (bucket_page_id == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Bucket page output cannot be null"
        );
    }

    if (!IsInitialized()) {
        return Status::IOError(
            "Hash index header page is not initialized"
        );
    }

    const uint16_t bucket_count = GetBucketCount();
    if (bucket_index >= bucket_count) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Bucket index is out of range"
        );
    }

    *bucket_page_id = ReadValue<PageId>(
        page_->GetData(),
        DIRECTORY_OFFSET +
            static_cast<std::size_t>(bucket_index) * sizeof(PageId)
    );

    return Status::OK();
}

Status HashIndexHeaderPage::SetBucketPageId(
    uint16_t bucket_index,
    PageId bucket_page_id
) {
    if (!IsInitialized()) {
        return Status::IOError(
            "Hash index header page is not initialized"
        );
    }

    const uint16_t bucket_count = GetBucketCount();
    if (bucket_index >= bucket_count) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Bucket index is out of range"
        );
    }

    if (bucket_page_id == 0 || bucket_page_id < INVALID_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid bucket page id"
        );
    }

    WriteValue<PageId>(
        page_->GetData(),
        DIRECTORY_OFFSET +
            static_cast<std::size_t>(bucket_index) * sizeof(PageId),
        bucket_page_id
    );

    page_->SetDirty(true);
    return Status::OK();
}

} // namespace minidbms
