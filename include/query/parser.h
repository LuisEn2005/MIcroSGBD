#ifndef MINI_DBMS_PARSER_H
#define MINI_DBMS_PARSER_H

#include "common/status.h"
#include "query/literal.h"
#include "query/tokenizer.h"

#include <cstdint>
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
    CREATE_TABLE,
    CREATE_INDEX
};

struct Condition {
    std::string column;
    std::string op;
    SQLLiteral value;
};

struct ColumnDefinition {
    std::string name;
    std::string type_name;
    uint32_t length{0};
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
    bool is_explain{false};
};

class InsertStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::INSERT;
    }

    std::string table_name;
    std::vector<SQLLiteral> values;
};

class UpdateStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::UPDATE;
    }

    std::string table_name;
    std::string column_name;
    SQLLiteral new_value;
    std::vector<Condition> conditions;
};

class DeleteStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::DELETE;
    }

    std::string table_name;
    std::vector<Condition> conditions;
};

class CreateTableStatement : public SQLStatement {
public:
    StatementType GetType() const override {
        return StatementType::CREATE_TABLE;
    }

    std::string table_name;
    std::vector<ColumnDefinition> columns;
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

    Status ParseSelect(
        std::unique_ptr<SQLStatement>* statement,
        bool is_explain = false
    );
    Status ParseInsert(std::unique_ptr<SQLStatement>* statement);
    Status ParseUpdate(std::unique_ptr<SQLStatement>* statement);
    Status ParseDelete(std::unique_ptr<SQLStatement>* statement);
    Status ParseCreateTable(std::unique_ptr<SQLStatement>* statement);
    Status ParseCreateIndex(std::unique_ptr<SQLStatement>* statement);
    Status ParseExplain(std::unique_ptr<SQLStatement>* statement);

    Status ParseLiteral(SQLLiteral* literal);
    Status ParseCondition(Condition* condition);
    Status ParseConditions(std::vector<Condition>* conditions);
    Status FinishStatement();

    std::vector<Token> tokens_;
    std::size_t cursor_{0};
};

} // namespace minidbms

#endif // MINI_DBMS_PARSER_H
