#ifndef MINI_DBMS_BUCKET_PAGE_H
#define MINI_DBMS_BUCKET_PAGE_H

#include "../common/types.h"

namespace minidbms {
  class HashIndexBucketPage {
    public:
      bool Insert(const char* key, RecordID value);
      bool Remove(const char* key);
      bool GetValue(const char* key, RecordID* result);
      PageId GetOverflowPageId() const;
      void SetOverflowPageId(PageId page_id);
  };

}

#endif // MINI_DBMS_BUCKET_PAGE_H
