#ifndef MINI_DBMS_HASH_INDEX_HEADER_PAGE_H
#define MINI_DBMS_HASH_INDEX_HEADER_PAGE_H

#include "common/status.h"
#include "common/types.h"
#include "storage/page.h"

#include <cstdint>

namespace minidbms {

class HashIndexHeaderPage {
public:
    explicit HashIndexHeaderPage(Page* page) : page_(page) {}

    Status Init(uint16_t bucket_count, uint16_t max_key_length);
    bool IsInitialized() const;

    uint16_t GetBucketCount() const;
    uint16_t GetMaxKeyLength() const;

    Status GetBucketPageId(
        uint16_t bucket_index,
        PageId* bucket_page_id
    ) const;

    Status SetBucketPageId(
        uint16_t bucket_index,
        PageId bucket_page_id
    );

    static constexpr uint16_t MaxBucketCount() {
        return static_cast<uint16_t>(
            (PAGE_SIZE - HEADER_SIZE) / sizeof(PageId)
        );
    }

private:
    static constexpr uint32_t MAGIC = 0x31495848U; // "HIX1"
    static constexpr uint16_t VERSION = 1;

    static constexpr std::size_t MAGIC_OFFSET = 0;
    static constexpr std::size_t VERSION_OFFSET = 4;
    static constexpr std::size_t BUCKET_COUNT_OFFSET = 6;
    static constexpr std::size_t MAX_KEY_LENGTH_OFFSET = 8;
    static constexpr std::size_t RESERVED_OFFSET = 10;
    static constexpr std::size_t DIRECTORY_OFFSET = 16;
    static constexpr std::size_t HEADER_SIZE = DIRECTORY_OFFSET;

    Page* page_;
};

} // namespace minidbms

#endif // MINI_DBMS_HASH_INDEX_HEADER_PAGE_H
