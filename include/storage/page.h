#ifndef MINI_DBMS_PAGE_H
#define MINI_DBMS_PAGE_H

#include "../common/types.h"
#include <cstring>

namespace minidbms {
  class Page {
    public:
      Page() { ResetMemory(); }
      ~Page() = default;

      char* GetData() { return data_; }
      PageId GetPageId() const { return page_id_; }
      int GetPinCount() const { return pin_count_; }
      bool IsDirty() const { return is_dirty_; }

      void SetPageId(PageId page_id) { page_id_ = page_id; }
      void SetDirty(bool is_dirty) { is_dirty_ = is_dirty; }

      void WLatch();
      void WUnlatch();
      void RLatch();
      void RUnlatch();

    private:
      friend class BufferPoolManager;

      void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_ = false;
      }

      char data_[PAGE_SIZE];
      PageId page_id_{INVALID_PAGE_ID};
      int pin_count_{0};
      bool is_dirty_{false};
  };

}

#endif // MINI_DBMS_PAGE_H
