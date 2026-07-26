#ifndef MINI_DBMS_EXECUTOR_H
#define MINI_DBMS_EXECUTOR_H

#include "../query/parser.h"
#include "../storage/record.h"
#include "../metrics/query_stats.h"
#include "../common/status.h"

namespace minidbms {
  class Executor {
    public:
      Executor() = default;
      virtual ~Executor() = default;

      virtual Status Init() = 0;
      virtual Status Next(Record* record, RecordID* rid) = 0;
      virtual const QueryStats& GetStats() const = 0;
  };

}

#endif // MINI_DBMS_EXECUTOR_H
