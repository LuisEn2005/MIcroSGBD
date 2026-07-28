#ifndef MINI_DBMS_PROJECTION_OPERATOR_H
#define MINI_DBMS_PROJECTION_OPERATOR_H

#include "../../query/operators/abstract_operator.h"
#include <vector>
#include <memory>
#include <utility>

namespace minidbms {
    class ProjectionOperator : public AbstractOperator {
        public:
            ProjectionOperator(std::unique_ptr<AbstractOperator> child, std::vector<uint32_t> select_fields)
                : child_(std::move(child)), select_fields_(std::move(select_fields)) {}

            Status Open() override {
                if (!child_) {
                    return Status(StatusCode::INVALID_ARGUMENT, "Operador hijo invalido");
                }
                return child_->Open();
            }

            bool Next(Record* record, RecordID* rid) override {
                if (!child_) {
                    return false;
                }
                return child_->Next(record, rid);
            }

            Status Close() override {
                if (!child_) {
                    return Status::OK();
                }
                return child_->Close();
            }

        private:
            std::unique_ptr<AbstractOperator> child_;
            std::vector<uint32_t> select_fields_;
    };

} // namespace minidbms

#endif // MINI_DBMS_PROJECTION_OPERATOR_H

