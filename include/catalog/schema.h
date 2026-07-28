#ifndef MINI_DBMS_SCHEMA_H
#define MINI_DBMS_SCHEMA_H

#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace minidbms {
  enum class TypeId { INTEGER, VARCHAR, BOOLEAN };

  struct Column {
    std::string name;
    TypeId type;
    uint32_t length;
  };

  class Schema {
    public:
      explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

      const std::vector<Column>& GetColumns() const { return columns_; }
      uint32_t GetColumnCount() const { return static_cast<uint32_t>(columns_.size()); }

    private:
      std::vector<Column> columns_;
  };

}

#endif // MINI_DBMS_SCHEMA_H
