#include "catalog/schema.h"
#include "query/operators/abstract_operator.h"
#include "query/operators/filter_operator.h"
#include "query/operators/projection_operator.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/record_codec.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace minidbms;

#define RUN_TEST(test_function)                                      \
    do {                                                             \
        std::cout << "[RUNNING] " << #test_function << "... ";      \
        test_function();                                             \
        std::cout << "[PASSED]\n";                                  \
    } while (false)

namespace {

class MockScanOperator : public AbstractOperator {
public:
    explicit MockScanOperator(std::vector<Record> records)
        : records_(std::move(records)) {}

    Status Open() override {
        cursor_ = 0;
        return Status::OK();
    }

    bool Next(Record* record, RecordID* rid) override {
        if (record == nullptr || rid == nullptr || cursor_ >= records_.size()) {
            return false;
        }

        *record = records_[cursor_];
        *rid = records_[cursor_].GetRecordID();
        ++cursor_;
        return true;
    }

    Status Close() override {
        return Status::OK();
    }

private:
    std::vector<Record> records_;
    std::size_t cursor_{0};
};

Schema BuildSchema() {
    return Schema({
        {"id", TypeId::INTEGER, sizeof(int32_t)},
        {"name", TypeId::VARCHAR, 32},
        {"active", TypeId::BOOLEAN, 1}
    });
}

Record BuildRecord(
    const Schema& schema,
    int32_t id,
    const std::string& name,
    bool active,
    SlotId slot_id
) {
    Record record;
    Status status = RecordCodec::Serialize(
        schema,
        {id, name, active},
        &record
    );
    assert(status.ok());
    record.SetRecordID({1, slot_id});
    return record;
}

void TestTokenizerBasicSelect() {
    Tokenizer tokenizer(
        "SELECT id, name FROM students WHERE id >= 10;"
    );
    const auto tokens = tokenizer.Tokenize();

    assert(tokens[0].type == TokenType::KEYWORD_SELECT);
    assert(tokens[1].type == TokenType::IDENTIFIER);
    assert(tokens[2].type == TokenType::COMMA);
    assert(tokens[3].type == TokenType::IDENTIFIER);
    assert(tokens[4].type == TokenType::KEYWORD_FROM);
    assert(tokens[8].type == TokenType::GREATER_EQUAL);
    assert(tokens[9].type == TokenType::NUMBER);
}

void TestTokenizerStringLiteral() {
    Tokenizer tokenizer(
        "SELECT * FROM students WHERE name = 'Alice';"
    );
    const auto tokens = tokenizer.Tokenize();

    bool found_string = false;
    for (const Token& token : tokens) {
        if (token.type == TokenType::STRING_LITERAL) {
            assert(token.text == "Alice");
            found_string = true;
        }
    }
    assert(found_string);
}

void TestParserSelectQuery() {
    Tokenizer tokenizer(
        "SELECT id, name FROM students WHERE id >= 10;"
    );
    Parser parser(tokenizer.Tokenize());

    std::unique_ptr<SQLStatement> statement;
    Status status = parser.Parse(&statement);

    assert(status.ok());
    assert(statement != nullptr);
    assert(statement->GetType() == StatementType::SELECT);

    const auto* select_statement =
        dynamic_cast<SelectStatement*>(statement.get());

    assert(select_statement != nullptr);
    assert(select_statement->table_name == "students");
    assert(select_statement->fields.size() == 2);
    assert(select_statement->conditions.size() == 1);
    assert(select_statement->conditions[0].column == "id");
    assert(select_statement->conditions[0].op == ">=");
    assert(select_statement->conditions[0].value == "10");
}

void TestRecordCodec() {
    const Schema schema = BuildSchema();
    Record record = BuildRecord(schema, 25, "Alice", true, 0);

    std::vector<FieldValue> values;
    Status status = RecordCodec::Deserialize(schema, record, &values);

    assert(status.ok());
    assert(std::get<int32_t>(values[0]) == 25);
    assert(std::get<std::string>(values[1]) == "Alice");
    assert(std::get<bool>(values[2]));
}

void TestVolcanoFilterAndProjection() {
    const Schema schema = BuildSchema();

    std::vector<Record> records;
    records.push_back(BuildRecord(schema, 10, "Ana", true, 0));
    records.push_back(BuildRecord(schema, 20, "Luis", false, 1));
    records.push_back(BuildRecord(schema, 30, "Marta", true, 2));

    std::unique_ptr<AbstractOperator> plan =
        std::make_unique<MockScanOperator>(std::move(records));

    plan = std::make_unique<FilterOperator>(
        std::move(plan),
        schema,
        Condition{"id", ">", "15"}
    );

    plan = std::make_unique<ProjectionOperator>(
        std::move(plan),
        schema,
        std::vector<uint32_t>{1}
    );

    assert(plan->Open().ok());

    const Schema projected_schema({
        {"name", TypeId::VARCHAR, 32}
    });

    std::vector<std::string> names;
    Record record;
    RecordID rid;

    while (plan->Next(&record, &rid)) {
        std::vector<FieldValue> values;
        Status status = RecordCodec::Deserialize(
            projected_schema,
            record,
            &values
        );
        assert(status.ok());
        names.push_back(std::get<std::string>(values[0]));
    }

    assert(plan->Close().ok());
    assert((names == std::vector<std::string>{"Luis", "Marta"}));
}

} // namespace

int main() {
    RUN_TEST(TestTokenizerBasicSelect);
    RUN_TEST(TestTokenizerStringLiteral);
    RUN_TEST(TestParserSelectQuery);
    RUN_TEST(TestRecordCodec);
    RUN_TEST(TestVolcanoFilterAndProjection);

    std::cout << "All query tests passed.\n";
    return 0;
}
