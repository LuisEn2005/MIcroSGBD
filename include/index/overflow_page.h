#ifndef MINI_DBMS_OVERFLOW_PAGE_H
#define MINI_DBMS_OVERFLOW_PAGE_H

#include "../common/types.h"

namespace minidbms {
  class HashIndexOverflowPage {
    public:
      bool Insert(const char* key, RecordID value);
      bool Remove(const char* key);
      bool GetValue(const char* key, RecordID* result);
      PageId GetNextOverflowPageId() const;
      void SetNextOverflowPageId(PageId page_id);
  };

}

#endif // MINI_DBMS_OVERFLOW_PAGE_H
