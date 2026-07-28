#include "query/executor.h"

#include "index/index_key.h"
#include "query/literal.h"
#include "query/operators/filter_operator.h"
#include "query/operators/index_scan_operator.h"
#include "query/operators/projection_operator.h"
#include "query/operators/seq_scan_operator.h"
#include "storage/heap_file.h"
#include "storage/record_codec.h"
#include "storage/slotted_page.h"
#include "storage/table_storage.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace minidbms {
namespace {

Status FindColumn(
    const Schema& schema,
    const std::string& column_name,
    uint32_t* column_index
) {
    if (column_index == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Column index output cannot be null"
        );
    }

    const auto& columns = schema.GetColumns();

    for (uint32_t index = 0;
         index < columns.size();
         ++index) {
        if (columns[index].name == column_name) {
            *column_index = index;
            return Status::OK();
        }
    }

    return Status::NotFound(
        "Column does not exist: " + column_name
    );
}

Status ClosePlan(
    AbstractOperator* plan,
    const Status& current_status
) {
    if (plan == nullptr) {
        return current_status;
    }

    const Status close_status = plan->Close();
    return current_status.ok() ? close_status : current_status;
}

Status DestroyIndexAfterFailure(
    std::unique_ptr<HashIndex>* index,
    const Status& primary_status
) {
    if (index == nullptr || !*index) {
        return primary_status;
    }

    const Status cleanup_status = (*index)->Destroy();
    index->reset();

    if (cleanup_status.ok()) {
        return primary_status;
    }

    return Status::IOError(
        "Index creation failed: " + primary_status.message() +
        "; cleanup failed: " + cleanup_status.message()
    );
}

} // namespace

Status QueryExecutor::Execute(
    const SQLStatement& statement,
    QueryStats* stats,
    QueryResult* result
) {
    QueryStats local_stats;
    QueryStats* active_stats =
        stats != nullptr ? stats : &local_stats;

    active_stats->Reset();
    if (result != nullptr) {
        result->Reset();
    }

    if (catalog_ == nullptr || bpm_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Executor dependencies cannot be null"
        );
    }

    const Status catalog_status =
        catalog_->GetInitializationStatus();

    if (!catalog_status.ok()) {
        return catalog_status;
    }

    const auto before_snapshot = bpm_->GetStatsSnapshot();
    const auto start_time = std::chrono::steady_clock::now();

    Status status = Status::OK();

    switch (statement.GetType()) {
        case StatementType::SELECT: {
            const auto* select_statement =
                dynamic_cast<const SelectStatement*>(&statement);

            if (select_statement == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid SELECT statement"
                );
                break;
            }

            std::unique_ptr<AbstractOperator> plan;
            Schema output_schema({});

            status = BuildPlan(
                *select_statement,
                active_stats,
                &plan,
                &output_schema
            );

            if (status.ok()) {
                status = plan->Open();
            }

            if (status.ok()) {
                if (result != nullptr) {
                    for (const Column& column :
                         output_schema.GetColumns()) {
                        result->column_names.push_back(column.name);
                    }
                }

                Record record;
                RecordID rid;

                while (plan->Next(&record, &rid)) {
                    ++active_stats->rows_returned;

                    if (result != nullptr) {
                        std::vector<FieldValue> row;
                        status = RecordCodec::Deserialize(
                            output_schema,
                            record,
                            &row
                        );

                        if (!status.ok()) {
                            break;
                        }

                        result->rows.push_back(std::move(row));
                    }
                }

                status = ClosePlan(plan.get(), status);
            }

            break;
        }

        case StatementType::CREATE_TABLE: {
            const auto* create_table =
                dynamic_cast<const CreateTableStatement*>(&statement);

            if (create_table == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid CREATE TABLE statement"
                );
                break;
            }

            status = ExecuteCreateTable(*create_table);
            break;
        }

        case StatementType::CREATE_INDEX: {
            const auto* create_index =
                dynamic_cast<const CreateIndexStatement*>(&statement);

            if (create_index == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid CREATE INDEX statement"
                );
                break;
            }

            status = ExecuteCreateIndex(*create_index);
            break;
        }

        case StatementType::UPDATE: {
            const auto* update_statement =
                dynamic_cast<const UpdateStatement*>(&statement);

            if (update_statement == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid UPDATE statement"
                );
                break;
            }

            status = ExecuteUpdate(
                *update_statement,
                active_stats
            );
            break;
        }

        case StatementType::DELETE: {
            const auto* delete_statement =
                dynamic_cast<const DeleteStatement*>(&statement);

            if (delete_statement == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid DELETE statement"
                );
                break;
            }

            status = ExecuteDelete(
                *delete_statement,
                active_stats
            );
            break;
        }

        case StatementType::INSERT: {
            const auto* insert_statement =
                dynamic_cast<const InsertStatement*>(&statement);

            if (insert_statement == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid INSERT statement"
                );
                break;
            }

            Schema schema({});
            status = catalog_->GetTableSchema(
                insert_statement->table_name,
                &schema
            );
            if (!status.ok()) {
                break;
            }

            const auto& columns = schema.GetColumns();
            if (insert_statement->values.size() != columns.size()) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "The number of values does not match the table schema"
                );
                break;
            }

            std::vector<FieldValue> values;
            values.reserve(columns.size());

            for (std::size_t index = 0;
                 index < columns.size();
                 ++index) {
                FieldValue value;
                status = ConvertLiteral(
                    columns[index],
                    insert_statement->values[index],
                    true,
                    &value
                );

                if (!status.ok()) {
                    break;
                }

                values.push_back(std::move(value));
            }

            if (!status.ok()) {
                break;
            }

            TableStorage storage(bpm_, catalog_);
            RecordID rid;
            status = storage.InsertRecord(
                insert_statement->table_name,
                values,
                &rid
            );

            if (status.ok()) {
                active_stats->rows_affected = 1;
            }

            break;
        }

        default:
            status = Status(
                StatusCode::INVALID_ARGUMENT,
                "Statement execution is not implemented"
            );
            break;
    }

    const auto end_time = std::chrono::steady_clock::now();
    const auto after_snapshot = bpm_->GetStatsSnapshot();

    BufferPoolManager::PopulateDeltaStats(
        before_snapshot,
        after_snapshot,
        active_stats
    );

    active_stats->execution_time_ms =
        std::chrono::duration<double, std::milli>(
            end_time - start_time
        ).count();

    return status;
}

Status QueryExecutor::ExecuteCreateTable(
    const CreateTableStatement& statement
) {
    if (statement.table_name.empty() ||
        statement.columns.empty()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "CREATE TABLE requires a name and at least one column"
        );
    }

    Schema existing_schema({});
    if (catalog_->GetTableSchema(
            statement.table_name,
            &existing_schema
        ).ok()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Table already exists"
        );
    }

    std::unordered_set<std::string> column_names;
    std::vector<Column> columns;
    columns.reserve(statement.columns.size());

    for (const auto& definition : statement.columns) {
        if (definition.name.empty() ||
            !column_names.insert(definition.name).second) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Column names must be non-empty and unique"
            );
        }

        std::string type_upper = definition.type_name;
        std::transform(
            type_upper.begin(),
            type_upper.end(),
            type_upper.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::toupper(character));
            }
        );

        TypeId type_id = TypeId::INTEGER;
        uint32_t length = definition.length;

        if (type_upper == "INT" || type_upper == "INTEGER") {
            type_id = TypeId::INTEGER;
            length = sizeof(int32_t);
        } else if (type_upper == "CHAR" ||
                   type_upper == "VARCHAR") {
            type_id = TypeId::VARCHAR;
            if (length == 0) {
                length = 30;
            }
        } else if (type_upper == "BOOLEAN" ||
                   type_upper == "BOOL") {
            type_id = TypeId::BOOLEAN;
            length = 1;
        } else {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Unsupported data type: " + definition.type_name
            );
        }

        columns.push_back({definition.name, type_id, length});
    }

    Schema schema(std::move(columns));

    PageId first_page_id = INVALID_PAGE_ID;
    Page* page = bpm_->NewPage(&first_page_id);
    if (page == nullptr) {
        return Status::OutOfMemory(
            "Could not allocate the first table page"
        );
    }

    SlottedPage slotted(page);
    Status status = slotted.Init();

    const bool unpinned = bpm_->UnpinPage(
        first_page_id,
        status.ok()
    );

    if (!unpinned) {
        return Status::IOError(
            "Could not unpin the first table page"
        );
    }

    if (!status.ok()) {
        bpm_->DeletePage(first_page_id);
        return status;
    }

    // La página de datos debe llegar a disco antes de publicar su PageId
    // en el catálogo persistente.
    if (!bpm_->FlushPage(first_page_id)) {
        bpm_->DeletePage(first_page_id);
        return Status::IOError(
            "Could not persist the first table page"
        );
    }

    status = catalog_->CreateTable(
        statement.table_name,
        schema,
        first_page_id
    );

    if (!status.ok()) {
        if (!bpm_->DeletePage(first_page_id)) {
            return Status::IOError(
                "CREATE TABLE failed: " + status.message() +
                "; allocated page cleanup failed"
            );
        }
    }

    return status;
}

Status QueryExecutor::ExecuteCreateIndex(
    const CreateIndexStatement& statement
) {
    if (catalog_->HasIndex(
            statement.table_name,
            statement.column_name
        )) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "An index already exists for this table column"
        );
    }

    if (catalog_->HasIndexName(statement.index_name)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Index name already exists"
        );
    }

    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;

    Status status = catalog_->GetTableSchema(
        statement.table_name,
        &schema
    );
    if (!status.ok()) {
        return status;
    }

    status = catalog_->GetTableFirstPageId(
        statement.table_name,
        &first_page_id
    );
    if (!status.ok()) {
        return status;
    }

    uint32_t column_index = 0;
    status = FindColumn(
        schema,
        statement.column_name,
        &column_index
    );
    if (!status.ok()) {
        return status;
    }

    std::unique_ptr<HashIndex> index;
    PageId header_page_id = INVALID_PAGE_ID;

    status = HashIndex::Create(
        bpm_,
        HashIndex::DEFAULT_BUCKET_COUNT,
        &index,
        &header_page_id
    );
    if (!status.ok()) {
        return status;
    }

    auto heap_file = std::make_unique<HeapFile>(
        bpm_,
        first_page_id
    );

    SeqScanOperator scan(std::move(heap_file));
    status = scan.Open();

    if (!status.ok()) {
        return DestroyIndexAfterFailure(&index, status);
    }

    Record record;
    RecordID rid;

    while (scan.Next(&record, &rid)) {
        FieldValue value;

        status = RecordCodec::GetValue(
            schema,
            record,
            column_index,
            &value
        );
        if (!status.ok()) {
            break;
        }

        if (std::holds_alternative<std::monostate>(value)) {
            continue;
        }

        std::string encoded_key;
        status = IndexKey::Encode(value, &encoded_key);
        if (!status.ok()) {
            break;
        }

        status = index->Insert(encoded_key, rid);
        if (!status.ok()) {
            break;
        }
    }

    status = ClosePlan(&scan, status);
    if (!status.ok()) {
        return DestroyIndexAfterFailure(&index, status);
    }

    // Se escriben header, buckets y overflow antes de publicar el índice
    // dentro del catálogo persistente.
    bpm_->FlushAllPages();

    return catalog_->CreateIndex(
        statement.index_name,
        statement.table_name,
        statement.column_name,
        std::move(index)
    );
}

Status QueryExecutor::ExecuteUpdate(
    const UpdateStatement& statement,
    QueryStats* stats
) {
    SelectStatement select_statement;
    select_statement.table_name = statement.table_name;
    select_statement.conditions = statement.conditions;

    std::unique_ptr<AbstractOperator> plan;
    Status status = BuildPlan(
        select_statement,
        stats,
        &plan
    );
    if (!status.ok()) {
        return status;
    }

    status = plan->Open();
    if (!status.ok()) {
        return status;
    }

    Schema schema({});
    status = catalog_->GetTableSchema(
        statement.table_name,
        &schema
    );
    if (!status.ok()) {
        return ClosePlan(plan.get(), status);
    }

    uint32_t column_index = 0;
    status = FindColumn(
        schema,
        statement.column_name,
        &column_index
    );
    if (!status.ok()) {
        return ClosePlan(plan.get(), status);
    }

    FieldValue new_value;
    status = ConvertLiteral(
        schema.GetColumns()[column_index],
        statement.new_value,
        true,
        &new_value
    );
    if (!status.ok()) {
        return ClosePlan(plan.get(), status);
    }

    struct PendingUpdate {
        RecordID rid;
        std::vector<FieldValue> values;
    };

    std::vector<PendingUpdate> pending;
    Record record;
    RecordID rid;

    while (plan->Next(&record, &rid)) {
        std::vector<FieldValue> values;
        status = RecordCodec::Deserialize(
            schema,
            record,
            &values
        );

        if (!status.ok()) {
            break;
        }

        values[column_index] = new_value;
        pending.push_back({rid, std::move(values)});
    }

    status = ClosePlan(plan.get(), status);
    if (!status.ok()) {
        return status;
    }

    TableStorage storage(bpm_, catalog_);

    for (PendingUpdate& update : pending) {
        status = storage.UpdateRecord(
            statement.table_name,
            update.rid,
            update.values
        );

        if (!status.ok()) {
            return status;
        }

        if (stats != nullptr) {
            ++stats->rows_affected;
        }
    }

    return Status::OK();
}

Status QueryExecutor::ExecuteDelete(
    const DeleteStatement& statement,
    QueryStats* stats
) {
    SelectStatement select_statement;
    select_statement.table_name = statement.table_name;
    select_statement.conditions = statement.conditions;

    std::unique_ptr<AbstractOperator> plan;
    Status status = BuildPlan(
        select_statement,
        stats,
        &plan
    );
    if (!status.ok()) {
        return status;
    }

    status = plan->Open();
    if (!status.ok()) {
        return status;
    }

    std::vector<RecordID> pending;
    Record record;
    RecordID rid;

    while (plan->Next(&record, &rid)) {
        pending.push_back(rid);
    }

    status = ClosePlan(plan.get(), status);
    if (!status.ok()) {
        return status;
    }

    TableStorage storage(bpm_, catalog_);

    for (const RecordID candidate : pending) {
        status = storage.DeleteRecord(
            statement.table_name,
            candidate
        );

        if (!status.ok()) {
            return status;
        }

        if (stats != nullptr) {
            ++stats->rows_affected;
        }
    }

    return Status::OK();
}

Status QueryExecutor::BuildPlan(
    const SelectStatement& select_statement,
    QueryStats* stats,
    std::unique_ptr<AbstractOperator>* plan,
    Schema* output_schema
) {
    if (plan == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Plan output cannot be null"
        );
    }

    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;

    Status status = catalog_->GetTableSchema(
        select_statement.table_name,
        &schema
    );
    if (!status.ok()) {
        return status;
    }

    status = catalog_->GetTableFirstPageId(
        select_statement.table_name,
        &first_page_id
    );
    if (!status.ok()) {
        return status;
    }

    auto heap_file = std::make_unique<HeapFile>(
        bpm_,
        first_page_id
    );

    std::unique_ptr<AbstractOperator> current_plan;
    int indexed_condition = -1;
    std::string encoded_key;

    for (std::size_t index = 0;
         index < select_statement.conditions.size();
         ++index) {
        const Condition& condition =
            select_statement.conditions[index];

        if (condition.op != "=" ||
            !catalog_->HasIndex(
                select_statement.table_name,
                condition.column
            )) {
            continue;
        }

        uint32_t column_index = 0;
        status = FindColumn(
            schema,
            condition.column,
            &column_index
        );
        if (!status.ok()) {
            return status;
        }

        FieldValue typed_value;
        status = ConvertLiteral(
            schema.GetColumns()[column_index],
            condition.value,
            false,
            &typed_value
        );
        if (!status.ok()) {
            return status;
        }

        status = IndexKey::Encode(
            typed_value,
            &encoded_key
        );
        if (!status.ok()) {
            return status;
        }

        indexed_condition = static_cast<int>(index);
        break;
    }

    if (indexed_condition >= 0) {
        const Condition& condition =
            select_statement.conditions[
                static_cast<std::size_t>(indexed_condition)
            ];

        HashIndex* index = catalog_->GetIndex(
            select_statement.table_name,
            condition.column
        );

        if (index == nullptr) {
            return Status::IOError(
                "Catalog index metadata has no open index"
            );
        }

        if (stats != nullptr) {
            stats->scan_type = ScanType::HASH_INDEX;
        }

        current_plan = std::make_unique<IndexScanOperator>(
            index,
            std::move(heap_file),
            encoded_key,
            stats
        );
    } else {
        if (stats != nullptr) {
            stats->scan_type = ScanType::SEQUENTIAL;
        }

        current_plan = std::make_unique<SeqScanOperator>(
            std::move(heap_file),
            stats
        );
    }

    for (const Condition& condition :
         select_statement.conditions) {
        current_plan = std::make_unique<FilterOperator>(
            std::move(current_plan),
            schema,
            condition
        );
    }

    Schema result_schema = schema;

    if (!select_statement.fields.empty() &&
        select_statement.fields.front() != "*") {
        std::vector<uint32_t> column_indices;
        std::vector<Column> result_columns;

        for (const std::string& field_name :
             select_statement.fields) {
            uint32_t column_index = 0;
            status = FindColumn(
                schema,
                field_name,
                &column_index
            );

            if (!status.ok()) {
                return status;
            }

            column_indices.push_back(column_index);
            result_columns.push_back(
                schema.GetColumns()[column_index]
            );
        }

        current_plan = std::make_unique<ProjectionOperator>(
            std::move(current_plan),
            schema,
            std::move(column_indices)
        );

        result_schema = Schema(std::move(result_columns));
    }

    if (output_schema != nullptr) {
        *output_schema = result_schema;
    }

    *plan = std::move(current_plan);
    return Status::OK();
}

} // namespace minidbms
