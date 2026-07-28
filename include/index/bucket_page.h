#ifndef MINI_DBMS_BUCKET_PAGE_H
#define MINI_DBMS_BUCKET_PAGE_H

#include "common/status.h"
#include "common/types.h"
#include "storage/page.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace minidbms {

class HashIndexBucketPage {
public:
    static constexpr uint16_t MAX_KEY_LENGTH = 64;

    explicit HashIndexBucketPage(Page* page) : page_(page) {}

    Status Init();
    bool IsInitialized() const;

    uint16_t GetEntryCount() const;
    uint16_t GetCapacity() const;

    PageId GetOverflowPageId() const;
    Status SetOverflowPageId(PageId page_id);

    Status Insert(
        const std::string& key,
        RecordID value,
        bool* inserted = nullptr
    );

    Status GetValue(
        const std::string& key,
        std::vector<RecordID>* result
    ) const;

    Status Remove(
        const std::string& key,
        const std::optional<RecordID>& value,
        uint32_t* removed_count = nullptr
    );

private:
    static constexpr uint32_t MAGIC = 0x314B4248U; // "HBK1"

    static constexpr std::size_t MAGIC_OFFSET = 0;
    static constexpr std::size_t ENTRY_COUNT_OFFSET = 4;
    static constexpr std::size_t CAPACITY_OFFSET = 6;
    static constexpr std::size_t OVERFLOW_PAGE_ID_OFFSET = 8;
    static constexpr std::size_t RESERVED_OFFSET = 12;
    static constexpr std::size_t HEADER_SIZE = 16;

    static constexpr std::size_t ENTRY_KEY_LENGTH_OFFSET = 0;
    static constexpr std::size_t ENTRY_RESERVED_OFFSET = 2;
    static constexpr std::size_t ENTRY_KEY_OFFSET = 4;
    static constexpr std::size_t ENTRY_PAGE_ID_OFFSET =
        ENTRY_KEY_OFFSET + MAX_KEY_LENGTH;
    static constexpr std::size_t ENTRY_SLOT_ID_OFFSET =
        ENTRY_PAGE_ID_OFFSET + sizeof(PageId);
    static constexpr std::size_t ENTRY_TRAILING_RESERVED_OFFSET =
        ENTRY_SLOT_ID_OFFSET + sizeof(SlotId);
    static constexpr std::size_t ENTRY_SIZE =
        ENTRY_TRAILING_RESERVED_OFFSET + sizeof(uint16_t);

    static constexpr uint16_t ENTRY_CAPACITY =
        static_cast<uint16_t>((PAGE_SIZE - HEADER_SIZE) / ENTRY_SIZE);

    std::size_t GetEntryOffset(uint16_t entry_index) const;

    Status ValidateKey(const std::string& key) const;
    Status ReadEntry(
        uint16_t entry_index,
        std::string* key,
        RecordID* value
    ) const;
    Status WriteEntry(
        uint16_t entry_index,
        const std::string& key,
        RecordID value
    );
    void ClearEntry(uint16_t entry_index);

    Page* page_;
};

} // namespace minidbms

#endif // MINI_DBMS_BUCKET_PAGE_H
