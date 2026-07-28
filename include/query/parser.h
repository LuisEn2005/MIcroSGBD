#ifndef MINI_DBMS_PARSER_H
#define MINI_DBMS_PARSER_H

#include "common/status.h"
#include "query/tokenizer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minidbms {

enum class StatementType {
    SELECT,
    INSERT,
    UPDATE,
    DELETE,
    EXPLAIN,
    CREATE_INDEX
};

struct Condition {
    std::string column;
    std::string op;
    std::string value;
};

class SQLStatement {
public:
    virtual ~SQLStatement() = default;
    virtual StatementType GetType() const = 0;
};

class SelectStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::SELECT;
    }

    std::string table_name;
    std::vector<std::string> fields;
    std::vector<Condition> conditions;
};

class InsertStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::INSERT;
    }

    std::string table_name;
    std::vector<std::string> values;
};

class CreateIndexStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::CREATE_INDEX;
    }

    std::string index_name;
    std::string table_name;
    std::string column_name;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : tokens_(std::move(tokens)) {}

    Status Parse(std::unique_ptr<SQLStatement>* statement);

private:
    const Token& Peek() const;
    Token Advance();
    bool Match(TokenType type);

    Status ParseSelect(std::unique_ptr<SQLStatement>* statement);
    Status ParseInsert(std::unique_ptr<SQLStatement>* statement);
    Status ParseCreateIndex(std::unique_ptr<SQLStatement>* statement);
    Status ParseCondition(Condition* condition);
    Status FinishStatement();

    std::vector<Token> tokens_;
    std::size_t cursor_{0};
};

} // namespace minidbms

#endif // MINI_DBMS_PARSER_H
