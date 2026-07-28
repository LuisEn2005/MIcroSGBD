#ifndef MINI_DBMS_DISK_MANAGER_H
#define MINI_DBMS_DISK_MANAGER_H

#include "../common/status.h"
#include "../common/types.h"

#include <cstddef>
#include <fstream>
#include <mutex>
#include <set>
#include <string>

namespace minidbms {
    class DiskManager {
        public:
            explicit DiskManager(const std::string& db_file);
            ~DiskManager();

            Status WritePage(PageId page_id, const char* page_data);
            Status ReadPage(PageId page_id, char* page_data);

            PageId AllocatePage();
            void DeallocatePage(PageId page_id);

            PageId GetNumPages() const;
            std::size_t GetFileSize() const;

        private:
            std::fstream db_io_;
            std::string db_filename_;
            PageId num_pages_{0};
            std::set<PageId> free_pages_;

            mutable std::mutex latch_;
    };

} // namespace minidbms

#endif // MINI_DBMS_DISK_MANAGER_H
