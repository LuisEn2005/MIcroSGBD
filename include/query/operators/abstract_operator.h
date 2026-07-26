#ifndef MINI_DBMS_ABSTRACT_OPERATOR_H
#define MINI_DBMS_ABSTRACT_OPERATOR_H

#include "../../storage/record.h"
#include "../../common/types.h"
#include "../../common/status.h"

namespace minidbms {
    class AbstractOperator {
        public:
            virtual ~AbstractOperator() = default;

            virtual Status Open() = 0;
            virtual bool Next(Record* record, RecordID* rid) = 0;
            virtual Status Close() = 0;
    };

}

#endif // MINI_DBMS_ABSTRACT_OPERATOR_H
