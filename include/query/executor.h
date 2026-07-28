#ifndef MINI_DBMS_EXECUTOR_H
#define MINI_DBMS_EXECUTOR_H

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog_manager.h"
#include "common/status.h"
#include "metrics/query_stats.h"
#include "query/operators/abstract_operator.h"
#include "query/parser.h"

#include <memory>

namespace minidbms {

class QueryExecutor {
public:
    QueryExecutor(
        CatalogManager* catalog,
        BufferPoolManager* bpm
    )
        : catalog_(catalog),
          bpm_(bpm) {}

    Status Execute(
        const SQLStatement& statement,
        QueryStats* stats
    );

private:
    Status BuildPlan(
        const SelectStatement& select_statement,
        QueryStats* stats,
        std::unique_ptr<AbstractOperator>* plan
    );

    Status ExecuteCreateIndex(
        const CreateIndexStatement& statement
    );

    Status ExecuteCreateTable(
        const CreateTableStatement& statement
    );

    Status ExecuteUpdate(
        const UpdateStatement& statement,
        QueryStats* stats
    );

    Status ExecuteDelete(
        const DeleteStatement& statement,
        QueryStats* stats
    );

    CatalogManager* catalog_;
    BufferPoolManager* bpm_;
};

} // namespace minidbms

#endif // MINI_DBMS_EXECUTOR_H
