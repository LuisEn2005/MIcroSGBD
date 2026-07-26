#ifndef MINI_DBMS_FILTER_OPERATOR_H
#define MINI_DBMS_FILTER_OPERATOR_H

#include "abstract_operator.h"
#include "../parser.h"
#include <memory>
#include <cstring>

namespace minidbms {

    class FilterOperator : public AbstractOperator {
        public:
            FilterOperator(std::unique_ptr<AbstractOperator> child, Condition condition)
                : child_(std::move(child)), condition_(std::move(condition)) {}

            Status Open() override { 
                return child_->Open(); 
            }

            bool Next(Record* record, RecordID* rid) override {
                while (child_->Next(record, rid)) {
                    if (Evaluate(*record)) {
                        return true;
                    }
                }
                return false;
            }

            Status Close() override { 
                return child_->Close(); 
            }

        private:
            bool Evaluate(const Record& record) const {
                if (record.GetData() == nullptr || record.GetSize() < sizeof(int32_t)) {
                    return true;
                }

                int32_t val = *reinterpret_cast<const int32_t*>(record.GetData());
                int32_t target = std::stoi(condition_.value);

                if (condition_.op == "=")  return val == target;
                if (condition_.op == ">")  return val > target;
                if (condition_.op == "<")  return val < target;
                if (condition_.op == ">=") return val >= target;
                if (condition_.op == "<=") return val <= target;

                return true;
            }

            std::unique_ptr<AbstractOperator> child_;
            Condition condition_;
    };

}

#endif // MINI_DBMS_FILTER_OPERATOR_H
