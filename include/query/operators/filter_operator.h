#ifndef MINI_DBMS_FILTER_OPERATOR_H
#define MINI_DBMS_FILTER_OPERATOR_H

#include "catalog/schema.h"
#include "query/operators/abstract_operator.h"
#include "query/parser.h"
#include "storage/record_codec.h"

#include <cstdint>
#include <memory>

namespace minidbms {

class FilterOperator : public AbstractOperator {
public:
    FilterOperator(
        std::unique_ptr<AbstractOperator> child,
        Schema schema,
        Condition condition
    );

    Status Open() override;
    bool Next(Record* record, RecordID* rid) override;
    Status Close() override;

private:
    Status ParseTargetValue();
    bool Evaluate(const Record& record);

    std::unique_ptr<AbstractOperator> child_;
    Schema schema_;
    Condition condition_;

    uint32_t column_index_{0};
    bool column_found_{false};
    FieldValue target_value_{};
    Status filter_status_{};
};

} // namespace minidbms

#endif // MINI_DBMS_FILTER_OPERATOR_H
