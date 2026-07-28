#ifndef MINI_DBMS_PROJECTION_OPERATOR_H
#define MINI_DBMS_PROJECTION_OPERATOR_H

#include "catalog/schema.h"
#include "query/operators/abstract_operator.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace minidbms {

class ProjectionOperator : public AbstractOperator {
public:
    ProjectionOperator(
        std::unique_ptr<AbstractOperator> child,
        Schema input_schema,
        std::vector<uint32_t> selected_fields
    );

    Status Open() override;
    bool Next(Record* record, RecordID* rid) override;
    Status Close() override;

private:
    std::unique_ptr<AbstractOperator> child_;
    Schema input_schema_;
    std::vector<uint32_t> selected_fields_;
    Status projection_status_{};
};

} // namespace minidbms

#endif // MINI_DBMS_PROJECTION_OPERATOR_H
