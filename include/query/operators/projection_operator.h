#ifndef MINI_DBMS_PROJECTION_OPERATOR_H
#define MINI_DBMS_PROJECTION_OPERATOR_H

#include "../../query/operators/abstract_operator.h"
#include <vector>
#include <memory>

namespace minidbms {
    class ProjectionOperator : public AbstractOperator {
        public:
            ProjectionOperator(std::unique_ptr<AbstractOperator> child, std::vector<uint32_t> col_indices)
                : child_(std::move(child)), col_indices_(std::move(col_indices)) {}

            Status Open() override { return child_->Open(); }
            bool Next(Record* record, RecordID* rid) override;
            Status Close() override { return child_->Close(); }

        private:
            std::unique_ptr<AbstractOperator> child_;
            std::vector<uint32_t> col_indices_;
    };

}

#endif // MINI_DBMS_PROJECTION_OPERATOR_H
