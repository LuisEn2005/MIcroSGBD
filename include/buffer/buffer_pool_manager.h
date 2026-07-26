#ifndef MINI_DBMS_BUFFER_POOL_MANAGER_H
#define MINI_DBMS_BUFFER_POOL_MANAGER_H

#include "../storage/disk_manager.h"
#include "../storage/page.h"
#include "replacer.h"
#include <unordered_map>
#include <list>
#include <mutex>

namespace minidbms {
  class BufferPoolManager {
    public:
      BufferPoolManager(std::size_t pool_size, DiskManager* disk_manager, Replacer* replacer);
      ~BufferPoolManager();

      Page* FetchPage(PageId page_id);
      bool UnpinPage(PageId page_id, bool is_dirty);
      bool FlushPage(PageId page_id);
      Page* NewPage(PageId* page_id);
      bool DeletePage(PageId page_id);
      void FlushAllPages();

      std::size_t GetPoolSize() const { return pool_size_; }

    private:
      std::size_t pool_size_;
      DiskManager* disk_manager_;
      Replacer* replacer_;
      Page* pages_;
      std::unordered_map<PageId, FrameId> page_table_;
      std::list<FrameId> free_list_;
      std::mutex latch_;
  };

}

#endif // MINI_DBMS_BUFFER_POOL_MANAGER_H
