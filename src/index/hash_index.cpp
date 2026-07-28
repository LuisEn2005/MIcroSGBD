#include "../../include/index/hash_index.h"
#include <functional>

namespace minidbms {
    HashIndex::HashIndex(BufferPoolManager* bpm, PageId header_page_id)
        : bpm_(bpm), header_page_id_(header_page_id) {}

    Status HashIndex::Insert(const std::string& key, RecordID value) {
        (void)key;
        (void)value;
        return Status::OK();
    }

    Status HashIndex::Remove(const std::string& key) {
        (void)key;
        return Status::OK();
    }

    Status HashIndex::GetValue(const std::string& key, std::vector<RecordID>* result) {
        (void)key;
        (void)result;
        return Status::OK();
    }

} // namespace minidbms
