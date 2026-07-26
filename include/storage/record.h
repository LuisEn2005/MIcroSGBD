#ifndef MINI_DBMS_RECORD_H
#define MINI_DBMS_RECORD_H

#include "../common/types.h"
#include <vector>
#include <cstdint>

namespace minidbms {
  class Record {
    public:
      Record() = default;
      Record(RecordID rid, uint32_t size, char* data)
        : rid_(rid), size_(size), data_(data, data + size) {}

      RecordID GetRecordID() const { return rid_; }
      void SetRecordID(RecordID rid) { rid_ = rid; }

      uint32_t GetSize() const { return size_; }
      const char* GetData() const { return data_.data(); }

      void SetData(const char* data, uint32_t size) {
        size_ = size;
        data_.assign(data, data + size);
      }

    private:
      RecordID rid_{INVALID_PAGE_ID, 0};
      uint32_t size_{0};
      std::vector<char> data_;
  };

}

#endif // MINI_DBMS_RECORD_H
