#ifndef MINI_DBMS_EXECUTOR_H
#define MINI_DBMS_EXECUTOR_H

#include "../query/parser.h"
#include "../query/operators/abstract_operator.h"
#include "../metrics/query_stats.h"
#include "../catalog/catalog_manager.h"
#include "../buffer/buffer_pool_manager.h"

namespace minidbms {
    class QueryExecutor {
        public:
            QueryExecutor(CatalogManager* catalog, BufferPoolManager* bpm)
                : catalog_(catalog), bpm_(bpm) {}

            Status Execute(const SQLStatement& stmt, QueryStats* stats);

        private:
            std::unique_ptr<AbstractOperator> BuildPlan(const SelectStatement& select_stmt);

            CatalogManager* catalog_;
            BufferPoolManager* bpm_;
    };

}

#endif // MINI_DBMS_EXECUTOR_H
