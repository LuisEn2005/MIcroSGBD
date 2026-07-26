#ifndef MINI_DBMS_CATALOG_MANAGER_H
#define MINI_DBMS_CATALOG_MANAGER_H

#include "../catalog/schema.h"
#include "../common/types.h"
#include "../common/status.h"
#include <unordered_map>

namespace minidbms {
  class CatalogManager {
    public:
      CatalogManager() = default;

      Status CreateTable(const std::string& table_name, const Schema& schema, PageId first_page_id);
      Status GetTableSchema(const std::string& table_name, Schema* schema) const;
      Status GetTableFirstPageId(const std::string& table_name, PageId* first_page_id) const;

    private:
      struct TableMetaData {
        std::string name;
        Schema schema;
        PageId first_page_id;
      };

      std::unordered_map<std::string, TableMetaData> tables_;
  };

}

#endif // MINI_DBMS_CATALOG_MANAGER_H
