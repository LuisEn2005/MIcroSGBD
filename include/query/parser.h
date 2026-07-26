#ifndef MINI_DBMS_PARSER_H
#define MINI_DBMS_PARSER_H

#include "../query/tokenizer.h"
#include "../common/status.h"
#include <memory>

namespace minidbms {
  class ASTNode {
    public:
      virtual ~ASTNode() = default;
  };

  class Parser {
    public:
      explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
      Status Parse(std::unique_ptr<ASTNode>* ast);

    private:
      std::vector<Token> tokens_;
      size_t cursor_{0};
  };

}

#endif // MINI_DBMS_PARSER_H
