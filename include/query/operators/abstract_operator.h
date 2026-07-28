#ifndef MINI_DBMS_ABSTRACT_OPERATOR_H
#define MINI_DBMS_ABSTRACT_OPERATOR_H

#include "common/status.h"
#include "common/types.h"
#include "storage/record.h"

namespace minidbms {

class AbstractOperator {
public:
    virtual ~AbstractOperator() = default;

    virtual Status Open() = 0;
    virtual bool Next(Record* record, RecordID* rid) = 0;
    virtual Status Close() = 0;
};

} // namespace minidbms

#endif // MINI_DBMS_ABSTRACT_OPERATOR_H
