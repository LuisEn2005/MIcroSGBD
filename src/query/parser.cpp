#include "query/parser.h"

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

    if (Peek().type != TokenType::END_OF_FILE) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se encontraron tokens inesperados al final de la sentencia"
        );
    }

    return Status::OK();
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

    if (Peek().type != TokenType::NUMBER &&
        Peek().type != TokenType::IDENTIFIER &&
        Peek().type != TokenType::STRING_LITERAL) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba un valor en WHERE"
        );
    }

    condition->value = Advance().text;
    return Status::OK();
}

Status Parser::ParseSelect(
    std::unique_ptr<SQLStatement>* statement
) {
    auto select_statement = std::make_unique<SelectStatement>();

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
        Condition condition;
        Status status = ParseCondition(&condition);
        if (!status.ok()) {
            return status;
        }

        select_statement->conditions.push_back(
            std::move(condition)
        );
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
        if (Peek().type != TokenType::NUMBER &&
            Peek().type != TokenType::IDENTIFIER &&
            Peek().type != TokenType::STRING_LITERAL) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Se esperaba un valor en INSERT"
            );
        }

        insert_statement->values.push_back(Advance().text);

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

Status Parser::ParseCreateIndex(
    std::unique_ptr<SQLStatement>* statement
) {
    auto create_statement =
        std::make_unique<CreateIndexStatement>();

    if (!Match(TokenType::KEYWORD_INDEX)) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Se esperaba INDEX despues de CREATE"
        );
    }

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

    if (tokens_.empty() ||
        Peek().type == TokenType::END_OF_FILE) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "No hay tokens para procesar"
        );
    }

    if (Match(TokenType::KEYWORD_SELECT)) {
        return ParseSelect(statement);
    }

    if (Match(TokenType::KEYWORD_INSERT)) {
        return ParseInsert(statement);
    }

    if (Match(TokenType::KEYWORD_CREATE)) {
        return ParseCreateIndex(statement);
    }

    return Status(
        StatusCode::INVALID_ARGUMENT,
        "Comando SQL no soportado"
    );
}

} // namespace minidbms
