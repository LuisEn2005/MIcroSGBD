#ifndef MINI_DBMS_TOKENIZER_H
#define MINI_DBMS_TOKENIZER_H

#include <string>
#include <vector>

namespace minidbms {
  enum class TokenType { KEYWORD, IDENTIFIER, NUMBER, STRING, OPERATOR, END_OF_FILE };

  struct Token {
    TokenType type;
    std::string value;
  };

  class Tokenizer {
    public:
      explicit Tokenizer(std::string sql) : sql_(std::move(sql)) {}
      std::vector<Token> Tokenize();

    private:
      std::string sql_;
      size_t pos_{0};
  };

}

#endif // MINI_DBMS_TOKENIZER_H
