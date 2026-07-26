#ifndef MINI_DBMS_DISK_MANAGER_H
#define MINI_DBMS_DISK_MANAGER_H

#include "../common/types.h"
#include "../common/status.h"
#include <string>
#include <fstream>

namespace minidbms {
  class DiskManager {
    public:
      explicit DiskManager(const std::string& db_file);
      ~DiskManager();

      Status WritePage(PageId page_id, const char* page_data);
      Status ReadPage(PageId page_id, char* page_data);
      PageId AllocatePage();
      void DeallocatePage(PageId page_id);

    private:
      std::fstream db_io_;
      std::string db_filename_;
      PageId num_pages_{0};
  };

}

#endif // MINI_DBMS_DISK_MANAGER_H
