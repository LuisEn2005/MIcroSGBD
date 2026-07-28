#include "index/bucket_page.h"

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

Status HashIndexBucketPage::Init() {
    if (page_ == nullptr || page_->GetPageId() == INVALID_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash bucket requires a valid page"
        );
    }

    char* data = page_->GetData();
    std::memset(data, 0, PAGE_SIZE);

    WriteValue<uint32_t>(data, MAGIC_OFFSET, MAGIC);
    WriteValue<uint16_t>(data, ENTRY_COUNT_OFFSET, 0);
    WriteValue<uint16_t>(data, CAPACITY_OFFSET, ENTRY_CAPACITY);
    WriteValue<PageId>(data, OVERFLOW_PAGE_ID_OFFSET, INVALID_PAGE_ID);
    WriteValue<uint32_t>(data, RESERVED_OFFSET, 0);

    page_->SetDirty(true);
    return Status::OK();
}

bool HashIndexBucketPage::IsInitialized() const {
    if (page_ == nullptr) {
        return false;
    }

    const char* data = page_->GetData();

    return ReadValue<uint32_t>(data, MAGIC_OFFSET) == MAGIC &&
           ReadValue<uint16_t>(data, CAPACITY_OFFSET) == ENTRY_CAPACITY &&
           ReadValue<uint16_t>(data, ENTRY_COUNT_OFFSET) <= ENTRY_CAPACITY;
}

uint16_t HashIndexBucketPage::GetEntryCount() const {
    if (!IsInitialized()) {
        return 0;
    }

    return ReadValue<uint16_t>(
        page_->GetData(),
        ENTRY_COUNT_OFFSET
    );
}

uint16_t HashIndexBucketPage::GetCapacity() const {
    return ENTRY_CAPACITY;
}

PageId HashIndexBucketPage::GetOverflowPageId() const {
    if (!IsInitialized()) {
        return INVALID_PAGE_ID;
    }

    return ReadValue<PageId>(
        page_->GetData(),
        OVERFLOW_PAGE_ID_OFFSET
    );
}

Status HashIndexBucketPage::SetOverflowPageId(PageId page_id) {
    if (!IsInitialized()) {
        return Status::IOError(
            "Hash bucket page is not initialized"
        );
    }

    if (page_id == 0 || page_id < INVALID_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid overflow page id"
        );
    }

    WriteValue<PageId>(
        page_->GetData(),
        OVERFLOW_PAGE_ID_OFFSET,
        page_id
    );

    page_->SetDirty(true);
    return Status::OK();
}

std::size_t HashIndexBucketPage::GetEntryOffset(
    uint16_t entry_index
) const {
    return HEADER_SIZE +
           static_cast<std::size_t>(entry_index) * ENTRY_SIZE;
}

Status HashIndexBucketPage::ValidateKey(
    const std::string& key
) const {
    if (key.size() > MAX_KEY_LENGTH) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index key exceeds maximum length"
        );
    }

    return Status::OK();
}

Status HashIndexBucketPage::ReadEntry(
    uint16_t entry_index,
    std::string* key,
    RecordID* value
) const {
    if (key == nullptr || value == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Entry outputs cannot be null"
        );
    }

    if (!IsInitialized() || entry_index >= GetEntryCount()) {
        return Status::NotFound("Hash bucket entry does not exist");
    }

    const char* data = page_->GetData();
    const std::size_t entry_offset = GetEntryOffset(entry_index);

    const uint16_t key_length = ReadValue<uint16_t>(
        data,
        entry_offset + ENTRY_KEY_LENGTH_OFFSET
    );

    if (key_length > MAX_KEY_LENGTH) {
        return Status::IOError("Corrupted hash bucket key length");
    }

    key->assign(
        data + entry_offset + ENTRY_KEY_OFFSET,
        data + entry_offset + ENTRY_KEY_OFFSET + key_length
    );

    value->page_id = ReadValue<PageId>(
        data,
        entry_offset + ENTRY_PAGE_ID_OFFSET
    );

    value->slot_id = ReadValue<SlotId>(
        data,
        entry_offset + ENTRY_SLOT_ID_OFFSET
    );

    return Status::OK();
}

Status HashIndexBucketPage::WriteEntry(
    uint16_t entry_index,
    const std::string& key,
    RecordID value
) {
    Status status = ValidateKey(key);
    if (!status.ok()) {
        return status;
    }

    if (!IsInitialized() || entry_index >= ENTRY_CAPACITY) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash bucket entry index is out of range"
        );
    }

    char* data = page_->GetData();
    const std::size_t entry_offset = GetEntryOffset(entry_index);

    std::memset(data + entry_offset, 0, ENTRY_SIZE);

    WriteValue<uint16_t>(
        data,
        entry_offset + ENTRY_KEY_LENGTH_OFFSET,
        static_cast<uint16_t>(key.size())
    );

    if (!key.empty()) {
        std::memcpy(
            data + entry_offset + ENTRY_KEY_OFFSET,
            key.data(),
            key.size()
        );
    }

    WriteValue<PageId>(
        data,
        entry_offset + ENTRY_PAGE_ID_OFFSET,
        value.page_id
    );

    WriteValue<SlotId>(
        data,
        entry_offset + ENTRY_SLOT_ID_OFFSET,
        value.slot_id
    );

    page_->SetDirty(true);
    return Status::OK();
}

void HashIndexBucketPage::ClearEntry(uint16_t entry_index) {
    if (entry_index >= ENTRY_CAPACITY) {
        return;
    }

    std::memset(
        page_->GetData() + GetEntryOffset(entry_index),
        0,
        ENTRY_SIZE
    );

    page_->SetDirty(true);
}

Status HashIndexBucketPage::Insert(
    const std::string& key,
    RecordID value,
    bool* inserted
) {
    if (inserted != nullptr) {
        *inserted = false;
    }

    Status status = ValidateKey(key);
    if (!status.ok()) {
        return status;
    }

    if (!IsInitialized()) {
        return Status::IOError("Hash bucket page is not initialized");
    }

    if (value.page_id <= 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index requires a valid RecordID"
        );
    }

    const uint16_t entry_count = GetEntryCount();

    for (uint16_t index = 0; index < entry_count; ++index) {
        std::string existing_key;
        RecordID existing_value;

        status = ReadEntry(index, &existing_key, &existing_value);
        if (!status.ok()) {
            return status;
        }

        if (existing_key == key && existing_value == value) {
            return Status::OK();
        }
    }

    if (entry_count >= ENTRY_CAPACITY) {
        return Status::OutOfMemory("Hash bucket page is full");
    }

    status = WriteEntry(entry_count, key, value);
    if (!status.ok()) {
        return status;
    }

    WriteValue<uint16_t>(
        page_->GetData(),
        ENTRY_COUNT_OFFSET,
        static_cast<uint16_t>(entry_count + 1)
    );

    page_->SetDirty(true);

    if (inserted != nullptr) {
        *inserted = true;
    }

    return Status::OK();
}

Status HashIndexBucketPage::GetValue(
    const std::string& key,
    std::vector<RecordID>* result
) const {
    if (result == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index result cannot be null"
        );
    }

    Status status = ValidateKey(key);
    if (!status.ok()) {
        return status;
    }

    if (!IsInitialized()) {
        return Status::IOError("Hash bucket page is not initialized");
    }

    bool found = false;
    const uint16_t entry_count = GetEntryCount();

    for (uint16_t index = 0; index < entry_count; ++index) {
        std::string existing_key;
        RecordID existing_value;

        status = ReadEntry(index, &existing_key, &existing_value);
        if (!status.ok()) {
            return status;
        }

        if (existing_key == key) {
            result->push_back(existing_value);
            found = true;
        }
    }

    if (!found) {
        return Status::NotFound("Hash key was not found in bucket");
    }

    return Status::OK();
}

Status HashIndexBucketPage::Remove(
    const std::string& key,
    const std::optional<RecordID>& value,
    uint32_t* removed_count
) {
    if (removed_count != nullptr) {
        *removed_count = 0;
    }

    Status status = ValidateKey(key);
    if (!status.ok()) {
        return status;
    }

    if (!IsInitialized()) {
        return Status::IOError("Hash bucket page is not initialized");
    }

    uint16_t entry_count = GetEntryCount();
    uint32_t removed = 0;
    uint16_t index = 0;

    while (index < entry_count) {
        std::string existing_key;
        RecordID existing_value;

        status = ReadEntry(index, &existing_key, &existing_value);
        if (!status.ok()) {
            return status;
        }

        const bool key_matches = existing_key == key;
        const bool value_matches =
            !value.has_value() || existing_value == value.value();

        if (!key_matches || !value_matches) {
            ++index;
            continue;
        }

        const uint16_t last_index =
            static_cast<uint16_t>(entry_count - 1);

        if (index != last_index) {
            std::string last_key;
            RecordID last_value;

            status = ReadEntry(last_index, &last_key, &last_value);
            if (!status.ok()) {
                return status;
            }

            status = WriteEntry(index, last_key, last_value);
            if (!status.ok()) {
                return status;
            }
        }

        ClearEntry(last_index);
        --entry_count;
        ++removed;

        if (value.has_value()) {
            break;
        }
    }

    if (removed == 0) {
        return Status::NotFound("Hash key/RID entry was not found");
    }

    WriteValue<uint16_t>(
        page_->GetData(),
        ENTRY_COUNT_OFFSET,
        entry_count
    );

    page_->SetDirty(true);

    if (removed_count != nullptr) {
        *removed_count = removed;
    }

    return Status::OK();
}

} // namespace minidbms
