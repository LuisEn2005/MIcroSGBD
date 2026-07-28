#ifndef MINI_DBMS_HASH_INDEX_H
#define MINI_DBMS_HASH_INDEX_H

#include "buffer/buffer_pool_manager.h"
#include "common/status.h"
#include "common/types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace minidbms {

class HashIndex {
public:
    static constexpr uint16_t DEFAULT_BUCKET_COUNT = 64;

    static Status Create(
        BufferPoolManager* bpm,
        uint16_t bucket_count,
        std::unique_ptr<HashIndex>* index,
        PageId* header_page_id
    );

    static Status Open(
        BufferPoolManager* bpm,
        PageId header_page_id,
        std::unique_ptr<HashIndex>* index
    );

    HashIndex(BufferPoolManager* bpm, PageId header_page_id);

    Status Insert(const std::string& key, RecordID value);

    // Elimina todas las entradas asociadas a la clave.
    Status Remove(const std::string& key);

    // Elimina únicamente el par exacto clave/RID.
    Status Remove(const std::string& key, RecordID value);

    Status GetValue(
        const std::string& key,
        std::vector<RecordID>* result
    ) const;

    PageId GetHeaderPageId() const {
        return header_page_id_;
    }

    Status GetBucketCount(uint16_t* bucket_count) const;

private:
    static uint64_t StableHash(const std::string& key);

    Status ValidateHeader(uint16_t* bucket_count = nullptr) const;

    Status GetBucketPageId(
        const std::string& key,
        uint16_t* bucket_index,
        PageId* bucket_page_id
    ) const;

    Status CreatePrimaryBucket(
        uint16_t bucket_index,
        const std::string& key,
        RecordID value
    );

    Status AppendOverflowBucket(
        PageId previous_bucket_page_id,
        const std::string& key,
        RecordID value
    );

    Status RemoveInternal(
        const std::string& key,
        const std::optional<RecordID>& value
    );

    BufferPoolManager* bpm_;
    PageId header_page_id_;
};

} // namespace minidbms

#endif // MINI_DBMS_HASH_INDEX_H
