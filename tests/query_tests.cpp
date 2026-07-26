#include <iostream>
#include <cassert>
#include <memory>
#include <vector>
#include <string>

// Encabezados del Query Engine
#include "../include/query/tokenizer.h"
#include "../include/query/parser.h"
#include "../include/query/executor.h"
#include "../include/query/operators/abstract_operator.h"
#include "../include/query/operators/seq_scan_operator.h"
#include "../include/query/operators/filter_operator.h"
#include "../include/query/operators/projection_operator.h"

using namespace minidbms;

// --- Helper para asserts sencillos en consola ---
#define RUN_TEST(test_func) \
    std::cout << "[RUNNING] " << #test_func << "... "; \
    test_func(); \
    std::cout << "\033[32m[PASSED]\033[0m" << std::endl;

// --- Mock Operator para probar Volcano sin depender de disco ---
class MockScanOperator : public AbstractOperator {
public:
    MockScanOperator(std::vector<Record> records) 
        : records_(std::move(records)), cursor_(0) {}

    Status Open() override {
        cursor_ = 0;
        return Status::OK();
    }

    bool Next(Record* record, RecordID* rid) override {
        if (cursor_ >= records_.size()) {
            return false;
        }
        *record = records_[cursor_];
        *rid = RecordID{1, static_cast<SlotId>(cursor_)};
        cursor_++;
        return true;
    }

    Status Close() override {
        return Status::OK();
    }

private:
    std::vector<Record> records_;
    size_t cursor_{0};
};

// ==========================================
// 1. Pruebas Unitarias del Tokenizer
// ==========================================
void TestTokenizerBasicSelect() {
    std::string sql = "SELECT id, nombre FROM alumnos WHERE id = 1;";
    Tokenizer tokenizer(sql);
    auto tokens = tokenizer.Tokenize();

    assert(!tokens.empty());
    assert(tokens[0].type == TokenType::KEYWORD_SELECT);
    assert(tokens[1].type == TokenType::IDENTIFIER && tokens[1].text == "id");
    assert(tokens[2].type == TokenType::COMMA);
    assert(tokens[3].type == TokenType::IDENTIFIER && tokens[3].text == "nombre");
    assert(tokens[4].type == TokenType::KEYWORD_FROM);
    assert(tokens[5].type == TokenType::IDENTIFIER && tokens[5].text == "alumnos");
    assert(tokens[6].type == TokenType::KEYWORD_WHERE);
    assert(tokens[7].type == TokenType::IDENTIFIER && tokens[7].text == "id");
    assert(tokens[8].type == TokenType::EQUAL);
    assert(tokens[9].type == TokenType::NUMBER && tokens[9].text == "1");
}

void TestTokenizerExplainAnalyze() {
    std::string sql = "EXPLAIN ANALYZE SELECT * FROM tabla;";
    Tokenizer tokenizer(sql);
    auto tokens = tokenizer.Tokenize();

    assert(tokens[0].type == TokenType::KEYWORD_EXPLAIN);
    assert(tokens[1].type == TokenType::KEYWORD_ANALYZE);
    assert(tokens[2].type == TokenType::KEYWORD_SELECT);
    assert(tokens[3].type == TokenType::ASTERISK);
}

// ==========================================
// 2. Pruebas Unitarias del Parser
// ==========================================
void TestParserSelectQuery() {
    std::string sql = "SELECT * FROM alumnos WHERE id = 10";
    Tokenizer tokenizer(sql);
    auto tokens = tokenizer.Tokenize();

    Parser parser(tokens);
    std::unique_ptr<SQLStatement> stmt;
    Status status = parser.Parse(&stmt);

    assert(status.ok());
    assert(stmt != nullptr);
    assert(stmt->GetType() == StatementType::SELECT);

    auto* select_stmt = dynamic_cast<SelectStatement*>(stmt.get());
    assert(select_stmt != nullptr);
    assert(select_stmt->table_name == "alumnos");
    assert(select_stmt->fields.size() == 1 && select_stmt->fields[0] == "*");
    assert(select_stmt->conditions.size() == 1);
    assert(select_stmt->conditions[0].column == "id");
    assert(select_stmt->conditions[0].op == "=");
    assert(select_stmt->conditions[0].value == "10");
}

// ==========================================
// 3. Pruebas de Operadores Volcano
// ==========================================
void TestVolcanoPipeline() {
    // Creamos 3 registros simulados
    std::vector<Record> mock_records;
    
    int32_t val1 = 10;
    mock_records.emplace_back(RecordID{1, 0}, sizeof(val1), reinterpret_cast<char*>(&val1));
    
    int32_t val2 = 20;
    mock_records.emplace_back(RecordID{1, 1}, sizeof(val2), reinterpret_cast<char*>(&val2));

    int32_t val3 = 30;
    mock_records.emplace_back(RecordID{1, 2}, sizeof(val3), reinterpret_cast<char*>(&val3));

    // Armamos el pipeline: MockScan -> Filter (donde valor > 15)
    auto scan = std::make_unique<MockScanOperator>(mock_records);
    Condition cond{"val", ">", "15"};
    auto filter = std::make_unique<FilterOperator>(std::move(scan), cond);

    // Bucle de ejecucion Volcano: Open -> Next -> Close
    assert(filter->Open().ok());

    Record rec;
    RecordID rid;
    int fetched_count = 0;

    while (filter->Next(&rec, &rid)) {
        fetched_count++;
    }

    assert(filter->Close().ok());
    
    // Deberian pasar solo val2 (20) y val3 (30)
    assert(fetched_count == 2);
}

// ==========================================
// Main Runner
// ==========================================
int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "       EJECUTANDO PRUEBAS: QUERY ENGINE   " << std::endl;
    std::cout << "==========================================" << std::endl;

    RUN_TEST(TestTokenizerBasicSelect);
    RUN_TEST(TestTokenizerExplainAnalyze);
    RUN_TEST(TestParserSelectQuery);
    RUN_TEST(TestVolcanoPipeline);

    std::cout << "\n¡Todas las pruebas del Integrante 3 pasaron con éxito!\n" << std::endl;
    return 0;
}

