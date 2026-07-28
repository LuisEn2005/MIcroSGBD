#include "query/executor.h"

#include "index/index_key.h"
#include "query/operators/filter_operator.h"
#include "query/operators/index_scan_operator.h"
#include "query/operators/projection_operator.h"
#include "query/operators/seq_scan_operator.h"
#include "storage/heap_file.h"
#include "storage/record_codec.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <string>
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

Status ParseLiteral(
    const Column& column,
    const std::string& text,
    FieldValue* value
) {
    if (value == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Literal output cannot be null"
        );
    }

    try {
        switch (column.type) {
            case TypeId::INTEGER: {
                std::size_t consumed = 0;
                const long long parsed =
                    std::stoll(text, &consumed);

                if (consumed != text.size() ||
                    parsed <
                        std::numeric_limits<int32_t>::min() ||
                    parsed >
                        std::numeric_limits<int32_t>::max()) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "Invalid INTEGER literal"
                    );
                }

                *value = static_cast<int32_t>(parsed);
                return Status::OK();
            }

            case TypeId::VARCHAR:
                if (column.length > 0 &&
                    text.size() > column.length) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "VARCHAR literal exceeds declared length"
                    );
                }

                *value = text;
                return Status::OK();

            case TypeId::BOOLEAN: {
                std::string normalized = text;
                std::transform(
                    normalized.begin(),
                    normalized.end(),
                    normalized.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(
                            std::tolower(character)
                        );
                    }
                );

                if (normalized == "true" ||
                    normalized == "1") {
                    *value = true;
                    return Status::OK();
                }

                if (normalized == "false" ||
                    normalized == "0") {
                    *value = false;
                    return Status::OK();
                }

                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid BOOLEAN literal"
                );
            }
        }
    } catch (const std::exception&) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid typed literal"
        );
    }

    return Status(
        StatusCode::INVALID_ARGUMENT,
        "Unsupported literal type"
    );
}

} // namespace

Status QueryExecutor::Execute(
    const SQLStatement& statement,
    QueryStats* stats
) {
    QueryStats local_stats;
    QueryStats* active_stats =
        stats != nullptr ? stats : &local_stats;

    active_stats->Reset();

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
    const auto start_time =
        std::chrono::steady_clock::now();

    Status status = Status::OK();

    switch (statement.GetType()) {
        case StatementType::SELECT: {
            const auto* select_statement =
                dynamic_cast<const SelectStatement*>(
                    &statement
                );

            if (select_statement == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid SELECT statement"
                );
                break;
            }

            std::unique_ptr<AbstractOperator> plan;
            status = BuildPlan(
                *select_statement,
                active_stats,
                &plan
            );

            if (status.ok()) {
                status = plan->Open();
            }

            if (status.ok()) {
                Record record;
                RecordID rid;

                while (plan->Next(&record, &rid)) {
                    ++active_stats->rows_returned;
                }

                const Status close_status =
                    plan->Close();

                if (!close_status.ok()) {
                    status = close_status;
                }
            }

            break;
        }

        case StatementType::CREATE_INDEX: {
            const auto* create_statement =
                dynamic_cast<const CreateIndexStatement*>(
                    &statement
                );

            if (create_statement == nullptr) {
                status = Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid CREATE INDEX statement"
                );
                break;
            }

            status = ExecuteCreateIndex(
                *create_statement
            );
            break;
        }

        case StatementType::INSERT:
            status = Status(
                StatusCode::INVALID_ARGUMENT,
                "INSERT execution belongs to Sprint 4"
            );
            break;

        default:
            status = Status(
                StatusCode::INVALID_ARGUMENT,
                "Statement execution is not implemented"
            );
            break;
    }

    const auto end_time =
        std::chrono::steady_clock::now();
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
        return status;
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
            scan.Close();
            return status;
        }

        if (std::holds_alternative<std::monostate>(
                value
            )) {
            continue;
        }

        std::string encoded_key;
        status = IndexKey::Encode(
            value,
            &encoded_key
        );
        if (!status.ok()) {
            scan.Close();
            return status;
        }

        status = index->Insert(
            encoded_key,
            rid
        );
        if (!status.ok()) {
            scan.Close();
            return status;
        }
    }

    status = scan.Close();
    if (!status.ok()) {
        return status;
    }

    return catalog_->CreateIndex(
        statement.index_name,
        statement.table_name,
        statement.column_name,
        std::move(index)
    );
}

Status QueryExecutor::BuildPlan(
    const SelectStatement& select_statement,
    QueryStats* stats,
    std::unique_ptr<AbstractOperator>* plan
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
        status = ParseLiteral(
            schema.GetColumns()[column_index],
            condition.value,
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

        indexed_condition =
            static_cast<int>(index);
        break;
    }

    if (indexed_condition >= 0) {
        const Condition& condition =
            select_statement.conditions[
                static_cast<std::size_t>(
                    indexed_condition
                )
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

        current_plan =
            std::make_unique<IndexScanOperator>(
                index,
                std::move(heap_file),
                encoded_key,
                stats
            );
    } else {
        if (stats != nullptr) {
            stats->scan_type = ScanType::SEQUENTIAL;
        }

        current_plan =
            std::make_unique<SeqScanOperator>(
                std::move(heap_file),
                stats
            );
    }

    // Se conservan todos los filtros, incluso el usado por el índice.
    // Esto protege contra entradas obsoletas después de futuras
    // actualizaciones o eliminaciones.
    for (const Condition& condition :
         select_statement.conditions) {
        current_plan =
            std::make_unique<FilterOperator>(
                std::move(current_plan),
                schema,
                condition
            );
    }

    if (!select_statement.fields.empty() &&
        select_statement.fields.front() != "*") {
        std::vector<uint32_t> column_indices;

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

            column_indices.push_back(
                column_index
            );
        }

        current_plan =
            std::make_unique<ProjectionOperator>(
                std::move(current_plan),
                schema,
                std::move(column_indices)
            );
    }

    *plan = std::move(current_plan);
    return Status::OK();
}

} // namespace minidbms
