#ifndef MINI_DBMS_HASH_INDEX_H
#define MINI_DBMS_HASH_INDEX_H

#include "../buffer/buffer_pool_manager.h"
#include "../common/status.h"
#include <vector>

namespace minidbms {
  class HashIndex {
    public:
      HashIndex(BufferPoolManager* bpm, PageId header_page_id);

      Status Insert(const std::string& key, RecordID value);
      Status Remove(const std::string& key);
      Status GetValue(const std::string& key, std::vector<RecordID>* result);

    private:
      BufferPoolManager* bpm_;
      PageId header_page_id_;
  };

}

#endif // MINI_DBMS_HASH_INDEX_H
