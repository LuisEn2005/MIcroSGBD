#include "index/hash_index.h"

#include "common/config.h"
#include "index/bucket_page.h"
#include "index/hash_index_header_page.h"

#include <stdexcept>
#include <unordered_set>

namespace minidbms {

HashIndex::HashIndex(
    BufferPoolManager* bpm,
    PageId header_page_id
)
    : bpm_(bpm),
      header_page_id_(header_page_id) {
    if (bpm_ == nullptr) {
        throw std::invalid_argument(
            "BufferPoolManager cannot be null"
        );
    }

    if (header_page_id_ <= HEADER_PAGE_ID) {
        throw std::invalid_argument(
            "Hash index requires a valid header page id"
        );
    }
}

Status HashIndex::Create(
    BufferPoolManager* bpm,
    uint16_t bucket_count,
    std::unique_ptr<HashIndex>* index,
    PageId* header_page_id
) {
    if (bpm == nullptr || index == nullptr || header_page_id == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index creation arguments cannot be null"
        );
    }

    if (bucket_count == 0 ||
        bucket_count > HashIndexHeaderPage::MaxBucketCount()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid hash index bucket count"
        );
    }

    PageId new_header_page_id = INVALID_PAGE_ID;
    Page* header_page = bpm->NewPage(&new_header_page_id);

    if (header_page == nullptr) {
        return Status::OutOfMemory(
            "Could not allocate hash index header page"
        );
    }

    HashIndexHeaderPage header(header_page);
    Status status = header.Init(
        bucket_count,
        HashIndexBucketPage::MAX_KEY_LENGTH
    );

    const bool unpinned = bpm->UnpinPage(
        new_header_page_id,
        status.ok()
    );

    if (!unpinned) {
        return Status::IOError(
            "Could not unpin hash index header page"
        );
    }

    if (!status.ok()) {
        bpm->DeletePage(new_header_page_id);
        return status;
    }

    *header_page_id = new_header_page_id;
    *index = std::make_unique<HashIndex>(bpm, new_header_page_id);
    return Status::OK();
}

Status HashIndex::Open(
    BufferPoolManager* bpm,
    PageId header_page_id,
    std::unique_ptr<HashIndex>* index
) {
    if (bpm == nullptr || index == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index open arguments cannot be null"
        );
    }

    if (header_page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid hash index header page id"
        );
    }

    auto opened_index = std::make_unique<HashIndex>(
        bpm,
        header_page_id
    );

    Status status = opened_index->ValidateHeader();
    if (!status.ok()) {
        return status;
    }

    *index = std::move(opened_index);
    return Status::OK();
}

uint64_t HashIndex::StableHash(const std::string& key) {
    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET_BASIS;

    for (unsigned char byte : key) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= FNV_PRIME;
    }

    return hash;
}

Status HashIndex::ValidateHeader(uint16_t* bucket_count) const {
    Page* header_page = bpm_->FetchPage(header_page_id_);

    if (header_page == nullptr) {
        return Status::IOError(
            "Could not fetch hash index header page"
        );
    }

    HashIndexHeaderPage header(header_page);
    Status status = Status::OK();

    if (!header.IsInitialized() ||
        header.GetMaxKeyLength() != HashIndexBucketPage::MAX_KEY_LENGTH) {
        status = Status::IOError(
            "Invalid or incompatible hash index header page"
        );
    } else if (bucket_count != nullptr) {
        *bucket_count = header.GetBucketCount();
    }

    if (!bpm_->UnpinPage(header_page_id_, false)) {
        return Status::IOError(
            "Could not unpin hash index header page"
        );
    }

    return status;
}

Status HashIndex::GetBucketCount(uint16_t* bucket_count) const {
    if (bucket_count == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Bucket count output cannot be null"
        );
    }

    return ValidateHeader(bucket_count);
}

Status HashIndex::GetBucketPageId(
    const std::string& key,
    uint16_t* bucket_index,
    PageId* bucket_page_id
) const {
    if (bucket_index == nullptr || bucket_page_id == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Bucket outputs cannot be null"
        );
    }

    if (key.size() > HashIndexBucketPage::MAX_KEY_LENGTH) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index key exceeds maximum length"
        );
    }

    Page* header_page = bpm_->FetchPage(header_page_id_);
    if (header_page == nullptr) {
        return Status::IOError(
            "Could not fetch hash index header page"
        );
    }

    HashIndexHeaderPage header(header_page);
    Status status = Status::OK();

    if (!header.IsInitialized()) {
        status = Status::IOError(
            "Hash index header page is not initialized"
        );
    } else {
        const uint16_t count = header.GetBucketCount();
        *bucket_index = static_cast<uint16_t>(
            StableHash(key) % count
        );

        status = header.GetBucketPageId(
            *bucket_index,
            bucket_page_id
        );
    }

    if (!bpm_->UnpinPage(header_page_id_, false)) {
        return Status::IOError(
            "Could not unpin hash index header page"
        );
    }

    return status;
}

Status HashIndex::CreatePrimaryBucket(
    uint16_t bucket_index,
    const std::string& key,
    RecordID value
) {
    PageId new_bucket_page_id = INVALID_PAGE_ID;
    Page* new_bucket_page = bpm_->NewPage(&new_bucket_page_id);

    if (new_bucket_page == nullptr) {
        return Status::OutOfMemory(
            "Could not allocate primary hash bucket"
        );
    }

    HashIndexBucketPage new_bucket(new_bucket_page);
    Status status = new_bucket.Init();

    if (status.ok()) {
        status = new_bucket.Insert(key, value);
    }

    if (!bpm_->UnpinPage(new_bucket_page_id, status.ok())) {
        return Status::IOError(
            "Could not unpin new primary hash bucket"
        );
    }

    if (!status.ok()) {
        bpm_->DeletePage(new_bucket_page_id);
        return status;
    }

    Page* header_page = bpm_->FetchPage(header_page_id_);
    if (header_page == nullptr) {
        bpm_->DeletePage(new_bucket_page_id);
        return Status::IOError(
            "Could not refetch hash index header page"
        );
    }

    HashIndexHeaderPage header(header_page);
    PageId existing_bucket_page_id = INVALID_PAGE_ID;

    status = header.GetBucketPageId(
        bucket_index,
        &existing_bucket_page_id
    );

    if (status.ok() && existing_bucket_page_id != INVALID_PAGE_ID) {
        status = Status::IOError(
            "Hash bucket was initialized concurrently"
        );
    }

    if (status.ok()) {
        status = header.SetBucketPageId(
            bucket_index,
            new_bucket_page_id
        );
    }

    const bool header_unpinned = bpm_->UnpinPage(
        header_page_id_,
        status.ok()
    );

    if (!header_unpinned) {
        return Status::IOError(
            "Could not unpin modified hash index header"
        );
    }

    if (!status.ok()) {
        bpm_->DeletePage(new_bucket_page_id);
    }

    return status;
}

Status HashIndex::AppendOverflowBucket(
    PageId previous_bucket_page_id,
    const std::string& key,
    RecordID value
) {
    PageId new_bucket_page_id = INVALID_PAGE_ID;
    Page* new_bucket_page = bpm_->NewPage(&new_bucket_page_id);

    if (new_bucket_page == nullptr) {
        return Status::OutOfMemory(
            "Could not allocate overflow hash bucket"
        );
    }

    HashIndexBucketPage new_bucket(new_bucket_page);
    Status status = new_bucket.Init();

    if (status.ok()) {
        status = new_bucket.Insert(key, value);
    }

    if (!bpm_->UnpinPage(new_bucket_page_id, status.ok())) {
        return Status::IOError(
            "Could not unpin new overflow hash bucket"
        );
    }

    if (!status.ok()) {
        bpm_->DeletePage(new_bucket_page_id);
        return status;
    }

    Page* previous_page = bpm_->FetchPage(previous_bucket_page_id);
    if (previous_page == nullptr) {
        bpm_->DeletePage(new_bucket_page_id);
        return Status::IOError(
            "Could not refetch previous hash bucket"
        );
    }

    HashIndexBucketPage previous_bucket(previous_page);

    if (!previous_bucket.IsInitialized()) {
        status = Status::IOError(
            "Previous hash bucket is not initialized"
        );
    } else if (
        previous_bucket.GetOverflowPageId() != INVALID_PAGE_ID
    ) {
        status = Status::IOError(
            "Overflow bucket was linked concurrently"
        );
    } else {
        status = previous_bucket.SetOverflowPageId(
            new_bucket_page_id
        );
    }

    const bool previous_unpinned = bpm_->UnpinPage(
        previous_bucket_page_id,
        status.ok()
    );

    if (!previous_unpinned) {
        return Status::IOError(
            "Could not unpin previous hash bucket"
        );
    }

    if (!status.ok()) {
        bpm_->DeletePage(new_bucket_page_id);
    }

    return status;
}

Status HashIndex::Insert(
    const std::string& key,
    RecordID value
) {
    if (key.empty()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index key cannot be empty"
        );
    }

    if (value.page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index requires a valid RecordID"
        );
    }

    uint16_t bucket_index = 0;
    PageId bucket_page_id = INVALID_PAGE_ID;

    Status status = GetBucketPageId(
        key,
        &bucket_index,
        &bucket_page_id
    );

    if (!status.ok()) {
        return status;
    }

    if (bucket_page_id == INVALID_PAGE_ID) {
        return CreatePrimaryBucket(
            bucket_index,
            key,
            value
        );
    }

    std::unordered_set<PageId> visited_pages;
    PageId current_page_id = bucket_page_id;
    PageId first_page_with_space = INVALID_PAGE_ID;
    PageId last_page_id = INVALID_PAGE_ID;

    // Se recorre toda la cadena antes de insertar. Esto evita crear
    // duplicados si el mismo key/RID está en una página overflow y
    // una página anterior recuperó espacio después de una eliminación.
    while (current_page_id != INVALID_PAGE_ID) {
        if (!visited_pages.insert(current_page_id).second) {
            return Status::IOError(
                "Cycle detected in hash bucket chain"
            );
        }

        Page* page = bpm_->FetchPage(current_page_id);
        if (page == nullptr) {
            return Status::IOError(
                "Could not fetch hash bucket"
            );
        }

        HashIndexBucketPage bucket(page);

        if (!bucket.IsInitialized()) {
            bpm_->UnpinPage(current_page_id, false);
            return Status::IOError(
                "Hash bucket page is not initialized"
            );
        }

        std::vector<RecordID> values_for_key;
        const Status lookup_status = bucket.GetValue(
            key,
            &values_for_key
        );

        if (!lookup_status.ok() &&
            lookup_status.code() != StatusCode::NOT_FOUND) {
            bpm_->UnpinPage(current_page_id, false);
            return lookup_status;
        }

        for (const RecordID& existing : values_for_key) {
            if (existing == value) {
                if (!bpm_->UnpinPage(
                        current_page_id,
                        false
                    )) {
                    return Status::IOError(
                        "Could not unpin hash bucket"
                    );
                }

                return Status::OK();
            }
        }

        if (first_page_with_space == INVALID_PAGE_ID &&
            bucket.GetEntryCount() < bucket.GetCapacity()) {
            first_page_with_space = current_page_id;
        }

        const PageId overflow_page_id =
            bucket.GetOverflowPageId();

        if (!bpm_->UnpinPage(current_page_id, false)) {
            return Status::IOError(
                "Could not unpin hash bucket"
            );
        }

        last_page_id = current_page_id;
        current_page_id = overflow_page_id;
    }

    if (first_page_with_space != INVALID_PAGE_ID) {
        Page* page = bpm_->FetchPage(first_page_with_space);

        if (page == nullptr) {
            return Status::IOError(
                "Could not refetch hash bucket with free space"
            );
        }

        HashIndexBucketPage bucket(page);
        bool inserted = false;

        status = bucket.Insert(
            key,
            value,
            &inserted
        );

        if (!bpm_->UnpinPage(
                first_page_with_space,
                status.ok() && inserted
            )) {
            return Status::IOError(
                "Could not unpin modified hash bucket"
            );
        }

        return status;
    }

    if (last_page_id == INVALID_PAGE_ID) {
        return Status::IOError(
            "Hash bucket chain ended unexpectedly"
        );
    }

    return AppendOverflowBucket(
        last_page_id,
        key,
        value
    );
}

Status HashIndex::GetValue(
    const std::string& key,
    std::vector<RecordID>* result
) const {
    if (result == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Hash index result cannot be null"
        );
    }

    result->clear();

    uint16_t bucket_index = 0;
    PageId bucket_page_id = INVALID_PAGE_ID;

    Status status = GetBucketPageId(
        key,
        &bucket_index,
        &bucket_page_id
    );

    if (!status.ok()) {
        return status;
    }

    if (bucket_page_id == INVALID_PAGE_ID) {
        return Status::OK();
    }

    std::unordered_set<PageId> visited_pages;
    PageId current_page_id = bucket_page_id;

    while (current_page_id != INVALID_PAGE_ID) {
        if (!visited_pages.insert(current_page_id).second) {
            return Status::IOError(
                "Cycle detected in hash bucket chain"
            );
        }

        Page* page = bpm_->FetchPage(current_page_id);
        if (page == nullptr) {
            return Status::IOError("Could not fetch hash bucket");
        }

        HashIndexBucketPage bucket(page);
        if (!bucket.IsInitialized()) {
            bpm_->UnpinPage(current_page_id, false);
            return Status::IOError(
                "Hash bucket page is not initialized"
            );
        }

        const Status bucket_status = bucket.GetValue(key, result);
        const PageId overflow_page_id =
            bucket.GetOverflowPageId();

        if (!bpm_->UnpinPage(current_page_id, false)) {
            return Status::IOError(
                "Could not unpin hash bucket after search"
            );
        }

        if (!bucket_status.ok() &&
            bucket_status.code() != StatusCode::NOT_FOUND) {
            return bucket_status;
        }

        current_page_id = overflow_page_id;
    }

    return Status::OK();
}

Status HashIndex::Remove(const std::string& key) {
    return RemoveInternal(key, std::nullopt);
}

Status HashIndex::Remove(
    const std::string& key,
    RecordID value
) {
    return RemoveInternal(key, value);
}

Status HashIndex::RemoveInternal(
    const std::string& key,
    const std::optional<RecordID>& value
) {
    uint16_t bucket_index = 0;
    PageId bucket_page_id = INVALID_PAGE_ID;

    Status status = GetBucketPageId(
        key,
        &bucket_index,
        &bucket_page_id
    );

    if (!status.ok()) {
        return status;
    }

    if (bucket_page_id == INVALID_PAGE_ID) {
        return Status::NotFound("Hash key was not found");
    }

    std::unordered_set<PageId> visited_pages;
    PageId current_page_id = bucket_page_id;
    uint32_t total_removed = 0;

    while (current_page_id != INVALID_PAGE_ID) {
        if (!visited_pages.insert(current_page_id).second) {
            return Status::IOError(
                "Cycle detected in hash bucket chain"
            );
        }

        Page* page = bpm_->FetchPage(current_page_id);
        if (page == nullptr) {
            return Status::IOError("Could not fetch hash bucket");
        }

        HashIndexBucketPage bucket(page);
        if (!bucket.IsInitialized()) {
            bpm_->UnpinPage(current_page_id, false);
            return Status::IOError(
                "Hash bucket page is not initialized"
            );
        }

        uint32_t removed_here = 0;
        const Status bucket_status = bucket.Remove(
            key,
            value,
            &removed_here
        );

        const PageId overflow_page_id =
            bucket.GetOverflowPageId();

        if (!bpm_->UnpinPage(
                current_page_id,
                removed_here > 0
            )) {
            return Status::IOError(
                "Could not unpin hash bucket after removal"
            );
        }

        if (!bucket_status.ok() &&
            bucket_status.code() != StatusCode::NOT_FOUND) {
            return bucket_status;
        }

        total_removed += removed_here;

        if (value.has_value() && total_removed > 0) {
            break;
        }

        current_page_id = overflow_page_id;
    }

    if (total_removed == 0) {
        return Status::NotFound("Hash key/RID entry was not found");
    }

    return Status::OK();
}

} // namespace minidbms
