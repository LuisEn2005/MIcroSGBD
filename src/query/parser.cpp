#include "query/parser.h"

#include <limits>

namespace minidbms {

const Token& Parser::Peek() const {
    static const Token eof_token{
        TokenType::END_OF_FILE,
        ""
    };

    if (cursor_ >= tokens_.size()) {
        return eof_token;
    }

    return tokens_[cursor_];
}

Token Parser::Advance() {
    if (cursor_ >= tokens_.size()) {
        return {TokenType::END_OF_FILE, ""};
    }

    return tokens_[cursor_++];
}

bool Parser::Match(TokenType type) {
    if (Peek().type != type) {
        return false;
    }

    Advance();
    return true;
}

Status Parser::FinishStatement() {
    Match(TokenType::SEMICOLON);

    if (Peek().type == TokenType::INVALID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            Peek().text
        );
    }

    if (Peek().type != TokenType::END_OF_FILE) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se encontraron tokens inesperados al final de la sentencia"
        );
    }

    return Status::OK();
}

Status Parser::ParseLiteral(SQLLiteral* literal) {
    if (literal == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "La salida del literal no puede ser nula"
        );
    }

    const Token token = Advance();

    switch (token.type) {
        case TokenType::NUMBER:
            literal->kind = LiteralKind::NUMBER;
            literal->text = token.text;
            return Status::OK();

        case TokenType::STRING_LITERAL:
            literal->kind = LiteralKind::STRING;
            literal->text = token.text;
            return Status::OK();

        case TokenType::IDENTIFIER:
            literal->kind = LiteralKind::IDENTIFIER;
            literal->text = token.text;
            return Status::OK();

        case TokenType::KEYWORD_NULL:
            literal->kind = LiteralKind::NULL_VALUE;
            literal->text.clear();
            return Status::OK();

        case TokenType::INVALID:
            return Status(
                StatusCode::INVALID_ARGUMENT,
                token.text
            );

        default:
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Se esperaba un literal"
            );
    }
}

Status Parser::ParseCondition(Condition* condition) {
    if (condition == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "La condicion de salida no puede ser nula"
        );
    }

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba un nombre de columna en WHERE"
        );
    }

    condition->column = Advance().text;

    if (Match(TokenType::EQUAL)) {
        condition->op = "=";
    } else if (Match(TokenType::GREATER_EQUAL)) {
        condition->op = ">=";
    } else if (Match(TokenType::LESS_EQUAL)) {
        condition->op = "<=";
    } else if (Match(TokenType::GREATER)) {
        condition->op = ">";
    } else if (Match(TokenType::LESS)) {
        condition->op = "<";
    } else {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Operador no soportado en WHERE"
        );
    }

    return ParseLiteral(&condition->value);
}

Status Parser::ParseConditions(
    std::vector<Condition>* conditions
) {
    if (conditions == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "La salida de condiciones no puede ser nula"
        );
    }

    while (true) {
        Condition condition;
        Status status = ParseCondition(&condition);
        if (!status.ok()) {
            return status;
        }

        conditions->push_back(std::move(condition));

        if (!Match(TokenType::KEYWORD_AND)) {
            break;
        }
    }

    return Status::OK();
}

Status Parser::ParseSelect(
    std::unique_ptr<SQLStatement>* statement,
    bool is_explain
) {
    auto select_statement = std::make_unique<SelectStatement>();
    select_statement->is_explain = is_explain;

    if (Match(TokenType::ASTERISK)) {
        select_statement->fields.push_back("*");
    } else {
        while (true) {
            if (Peek().type != TokenType::IDENTIFIER) {
                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Se esperaba un nombre de columna en SELECT"
                );
            }

            select_statement->fields.push_back(Advance().text);

            if (!Match(TokenType::COMMA)) {
                break;
            }
        }
    }

    if (!Match(TokenType::KEYWORD_FROM)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba la clausula FROM"
        );
    }

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba un nombre de tabla despues de FROM"
        );
    }

    select_statement->table_name = Advance().text;

    if (Match(TokenType::KEYWORD_WHERE)) {
        Status status = ParseConditions(
            &select_statement->conditions
        );
        if (!status.ok()) {
            return status;
        }
    }

    Status status = FinishStatement();
    if (!status.ok()) {
        return status;
    }

    *statement = std::move(select_statement);
    return Status::OK();
}

Status Parser::ParseInsert(
    std::unique_ptr<SQLStatement>* statement
) {
    auto insert_statement = std::make_unique<InsertStatement>();

    if (!Match(TokenType::KEYWORD_INTO)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba INTO despues de INSERT"
        );
    }

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba el nombre de la tabla"
        );
    }

    insert_statement->table_name = Advance().text;

    if (!Match(TokenType::KEYWORD_VALUES) ||
        !Match(TokenType::LPAREN)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba VALUES (...)"
        );
    }

    while (true) {
        SQLLiteral literal;
        Status status = ParseLiteral(&literal);
        if (!status.ok()) {
            return status;
        }

        insert_statement->values.push_back(std::move(literal));

        if (!Match(TokenType::COMMA)) {
            break;
        }
    }

    if (!Match(TokenType::RPAREN)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba ')' al final de VALUES"
        );
    }

    Status status = FinishStatement();
    if (!status.ok()) {
        return status;
    }

    *statement = std::move(insert_statement);
    return Status::OK();
}

Status Parser::ParseUpdate(
    std::unique_ptr<SQLStatement>* statement
) {
    auto update_statement = std::make_unique<UpdateStatement>();

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba el nombre de la tabla en UPDATE"
        );
    }

    update_statement->table_name = Advance().text;

    if (!Match(TokenType::KEYWORD_SET)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba la clausula SET"
        );
    }

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba la columna a actualizar en SET"
        );
    }

    update_statement->column_name = Advance().text;

    if (!Match(TokenType::EQUAL)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba '=' en SET"
        );
    }

    Status status = ParseLiteral(&update_statement->new_value);
    if (!status.ok()) {
        return status;
    }

    if (Match(TokenType::KEYWORD_WHERE)) {
        status = ParseConditions(&update_statement->conditions);
        if (!status.ok()) {
            return status;
        }
    }

    status = FinishStatement();
    if (!status.ok()) {
        return status;
    }

    *statement = std::move(update_statement);
    return Status::OK();
}

Status Parser::ParseDelete(
    std::unique_ptr<SQLStatement>* statement
) {
    auto delete_statement = std::make_unique<DeleteStatement>();

    if (!Match(TokenType::KEYWORD_FROM)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba FROM despues de DELETE"
        );
    }

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba el nombre de la tabla en DELETE"
        );
    }

    delete_statement->table_name = Advance().text;

    if (Match(TokenType::KEYWORD_WHERE)) {
        Status status = ParseConditions(
            &delete_statement->conditions
        );
        if (!status.ok()) {
            return status;
        }
    }

    Status status = FinishStatement();
    if (!status.ok()) {
        return status;
    }

    *statement = std::move(delete_statement);
    return Status::OK();
}

Status Parser::ParseCreateTable(
    std::unique_ptr<SQLStatement>* statement
) {
    auto create_table = std::make_unique<CreateTableStatement>();

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba el nombre de la tabla en CREATE TABLE"
        );
    }

    create_table->table_name = Advance().text;

    if (!Match(TokenType::LPAREN)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba '(' despues del nombre de la tabla"
        );
    }

    while (true) {
        if (Peek().type != TokenType::IDENTIFIER) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Se esperaba nombre de columna"
            );
        }

        ColumnDefinition column;
        column.name = Advance().text;

        if (Peek().type != TokenType::IDENTIFIER) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Se esperaba tipo de datos de columna"
            );
        }

        column.type_name = Advance().text;

        if (Match(TokenType::LPAREN)) {
            if (Peek().type != TokenType::NUMBER) {
                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Se esperaba la longitud del tipo entre parentesis"
                );
            }

            try {
                const unsigned long parsed = std::stoul(
                    Advance().text
                );

                if (parsed == 0 ||
                    parsed > std::numeric_limits<uint32_t>::max()) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "Longitud de columna fuera de rango"
                    );
                }

                column.length = static_cast<uint32_t>(parsed);
            } catch (const std::exception&) {
                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Longitud de columna invalida"
                );
            }

            if (!Match(TokenType::RPAREN)) {
                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Se esperaba ')' despues de la longitud del tipo"
                );
            }
        }

        create_table->columns.push_back(std::move(column));

        if (!Match(TokenType::COMMA)) {
            break;
        }
    }

    if (!Match(TokenType::RPAREN)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba ')' al final de las columnas"
        );
    }

    Status status = FinishStatement();
    if (!status.ok()) {
        return status;
    }

    *statement = std::move(create_table);
    return Status::OK();
}

Status Parser::ParseCreateIndex(
    std::unique_ptr<SQLStatement>* statement
) {
    auto create_statement =
        std::make_unique<CreateIndexStatement>();

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba el nombre del indice"
        );
    }

    create_statement->index_name = Advance().text;

    if (!Match(TokenType::KEYWORD_ON)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba ON en CREATE INDEX"
        );
    }

    if (Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba el nombre de la tabla"
        );
    }

    create_statement->table_name = Advance().text;

    if (!Match(TokenType::LPAREN) ||
        Peek().type != TokenType::IDENTIFIER) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba una columna entre parentesis"
        );
    }

    create_statement->column_name = Advance().text;

    if (!Match(TokenType::RPAREN)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba ')' en CREATE INDEX"
        );
    }

    Status status = FinishStatement();
    if (!status.ok()) {
        return status;
    }

    *statement = std::move(create_statement);
    return Status::OK();
}

Status Parser::ParseExplain(
    std::unique_ptr<SQLStatement>* statement
) {
    if (!Match(TokenType::KEYWORD_ANALYZE)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba ANALYZE despues de EXPLAIN"
        );
    }

    if (!Match(TokenType::KEYWORD_SELECT)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba SELECT despues de EXPLAIN ANALYZE"
        );
    }

    return ParseSelect(statement, true);
}

Status Parser::Parse(
    std::unique_ptr<SQLStatement>* statement
) {
    if (statement == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "La salida de la sentencia no puede ser nula"
        );
    }

    statement->reset();

    for (const Token& token : tokens_) {
        if (token.type == TokenType::INVALID) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                token.text
            );
        }
    }

    if (tokens_.empty() ||
        Peek().type == TokenType::END_OF_FILE) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "No hay tokens para procesar"
        );
    }

    if (Match(TokenType::KEYWORD_EXPLAIN)) {
        return ParseExplain(statement);
    }

    if (Match(TokenType::KEYWORD_SELECT)) {
        return ParseSelect(statement, false);
    }

    if (Match(TokenType::KEYWORD_INSERT)) {
        return ParseInsert(statement);
    }

    if (Match(TokenType::KEYWORD_UPDATE)) {
        return ParseUpdate(statement);
    }

    if (Match(TokenType::KEYWORD_DELETE)) {
        return ParseDelete(statement);
    }

    if (Match(TokenType::KEYWORD_CREATE)) {
        if (Match(TokenType::KEYWORD_TABLE)) {
            return ParseCreateTable(statement);
        }
        if (Match(TokenType::KEYWORD_INDEX)) {
            return ParseCreateIndex(statement);
        }
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba TABLE o INDEX despues de CREATE"
        );
    }

    return Status(
        StatusCode::INVALID_ARGUMENT,
        "Comando SQL no soportado"
    );
}

} // namespace minidbms
