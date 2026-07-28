#ifndef MINI_DBMS_BUFFER_POOL_MANAGER_H
#define MINI_DBMS_BUFFER_POOL_MANAGER_H

#include "../common/config.h"
#include "../storage/disk_manager.h"
#include "../storage/page.h"
#include "../buffer/replacer.h"
#include "../metrics/query_stats.h"
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

      uint64_t GetBufferHits() const { return buffer_hits_; }
      uint64_t GetBufferMisses() const { return buffer_misses_; }
      uint64_t GetDiskReads() const { return disk_reads_; }
      uint64_t GetDiskWrites() const { return disk_writes_; }

      struct BufferPoolStatsSnapshot {
          uint64_t buffer_hits{0};
          uint64_t buffer_misses{0};
          uint64_t disk_reads{0};
          uint64_t disk_writes{0};
      };

      BufferPoolStatsSnapshot GetStatsSnapshot() const {
          std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(latch_));
          return {buffer_hits_, buffer_misses_, disk_reads_, disk_writes_};
      }

      void PopulateStats(QueryStats* stats) const {
          if (stats == nullptr) return;
          stats->buffer_hits = buffer_hits_;
          stats->buffer_misses = buffer_misses_;
          stats->disk_reads = disk_reads_;
          stats->disk_writes = disk_writes_;
      }

      static void PopulateDeltaStats(
          const BufferPoolStatsSnapshot& before,
          const BufferPoolStatsSnapshot& after,
          QueryStats* stats
      ) {
          if (stats == nullptr) return;
          stats->buffer_hits = after.buffer_hits >= before.buffer_hits ? after.buffer_hits - before.buffer_hits : 0;
          stats->buffer_misses = after.buffer_misses >= before.buffer_misses ? after.buffer_misses - before.buffer_misses : 0;
          stats->disk_reads = after.disk_reads >= before.disk_reads ? after.disk_reads - before.disk_reads : 0;
          stats->disk_writes = after.disk_writes >= before.disk_writes ? after.disk_writes - before.disk_writes : 0;
      }

      void ResetStats() {
          std::lock_guard<std::mutex> lock(latch_);
          buffer_hits_ = 0;
          buffer_misses_ = 0;
          disk_reads_ = 0;
          disk_writes_ = 0;
      }

    private:
      std::size_t pool_size_;
      DiskManager* disk_manager_;
      Replacer* replacer_;
      Page* pages_;
      std::unordered_map<PageId, FrameId> page_table_;
      std::list<FrameId> free_list_;
      std::mutex latch_;

      uint64_t buffer_hits_{0};
      uint64_t buffer_misses_{0};
      uint64_t disk_reads_{0};
      uint64_t disk_writes_{0};
  };

}

#endif // MINI_DBMS_BUFFER_POOL_MANAGER_H
