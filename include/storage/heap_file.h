#ifndef MINI_DBMS_HEAP_FILE_H
#define MINI_DBMS_HEAP_FILE_H

#include "record.h"
#include "../buffer/buffer_pool_manager.h"
#include "../common/status.h"

namespace minidbms {
  class HeapFile {
    public:
      HeapFile(BufferPoolManager* bpm, PageId first_page_id)
        : bpm_(bpm), first_page_id_(first_page_id) {}

      Status InsertRecord(const Record& record, RecordID* rid);
      Status GetRecord(RecordID rid, Record* record);
      Status UpdateRecord(const Record& record);
      Status DeleteRecord(RecordID rid);

      PageId GetFirstPageId() const { return first_page_id_; }

    private:
      BufferPoolManager* bpm_;
      PageId first_page_id_;
  };

}

#endif // MINI_DBMS_HEAP_FILE_H
