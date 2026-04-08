#pragma once

#include <ast.hpp>
#include <elpc/parser/prattParser.hpp>

class HydroloxParser
    : public elpc::PrattParser<TokenType, std::unique_ptr<AST::Expr>> {
  using ExprNode = std::unique_ptr<AST::Expr>;

public:
  HydroloxParser(const std::vector<elpc::Token<TokenType>> &tokens)
      : elpc::PrattParser<TokenType, ExprNode>(tokens) {

    // --- PRATT PARSER EXPRESSION RULES ---

    // Integer Literals
    registerPrefix(TokenType::INT_LIT, [this](const auto &tok) -> ExprNode {
      return std::make_unique<AST::IntLiteral>(tok.toInt().value_or(0),
                                               tok.location);
    });

    // Boolean Literals
    registerPrefix(TokenType::BOOL_LIT, [this](const auto &tok) -> ExprNode {
      bool val = (tok.lexeme == "true");
      return std::make_unique<AST::BoolLiteral>(val, tok.location);
    });

    // String Literals
    registerPrefix(TokenType::STRING_LIT, [this](const auto &tok) -> ExprNode {
      std::string val = tok.lexeme.substr(1, tok.lexeme.size() - 2);
      return std::make_unique<AST::StringLiteral>(val, tok.location);
    });

    // Identifiers (Variables)
    registerPrefix(TokenType::IDENT, [this](const auto &tok) -> ExprNode {
      return std::make_unique<AST::Identifier>(tok.lexeme, tok.location);
    });

    // Grouping Parenthesis && Type casting
    registerPrefix(TokenType::LEFT_PAREN, [this](const auto &tok) -> ExprNode {
      if ((peek().isOneOf(TokenType::U8, TokenType::I32, TokenType::BOOL,
                          TokenType::STRING)) &&
          peek(1).is(TokenType::RIGHT_PAREN)) {

        auto typeTok = consume(); // Consume the type
        consume();                // Consume ')'

        // UNARY precedence ensures `(u8)x + y` casts `x`, not `x + y`
        auto expr = parseExpression(elpc::Precedence::UNARY);
        return std::make_unique<AST::CastExpr>(typeTok.type, std::move(expr),
                                               tok.location);
      }

      auto expr = parseExpression(elpc::Precedence::NONE);
      expect(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
      return expr;
    });

    registerPrefix(
        TokenType::LEFT_SQ_BRACE, [this](const auto &tok) -> ExprNode {
          std::vector<ExprNode> elements;

          if (!check(TokenType::RIGHT_SQ_BRACE)) {
            do {
              elements.push_back(parseExpression(elpc::Precedence::NONE));
            } while (match(TokenType::COMMA));
          }

          expect(TokenType::RIGHT_SQ_BRACE,
                 "Expected ']' after array literal.");

          return std::make_unique<AST::ArrayLiteral>(std::move(elements),
                                                     tok.location);
        });

    //-Crement
    auto postfixRule = [this](const auto &tok, ExprNode lhs) -> ExprNode {
      auto *ident = dynamic_cast<AST::Identifier *>(lhs.get());
      if (!ident)
        throw std::runtime_error(
            "'++'/,'--' can only be applied to a variable.");

      // x++ => x = x + 1, x-- => x = x - 1
      TokenType op = (tok.type == TokenType::PLUS_PLUS) ? TokenType::PLUS
                                                        : TokenType::MINUS;

      auto varRef =
          std::make_unique<AST::Identifier>(ident->name, tok.location);
      auto one = std::make_unique<AST::IntLiteral>(1, tok.location);
      auto binop = std::make_unique<AST::BinaryExpr>(
          op, std::move(varRef), std::move(one), tok.location);

      return std::make_unique<AST::AssignExpr>(ident->name, std::move(binop),
                                               tok.location);
    };
    registerInfix(TokenType::PLUS_PLUS, elpc::Precedence::UNARY, postfixRule);
    registerInfix(TokenType::MINUS_MINUS, elpc::Precedence::UNARY, postfixRule);

    // Unary prefix rule
    registerPrefix(TokenType::MINUS, [this](const auto &tok) -> ExprNode {
      auto operand = parseExpression(elpc::Precedence::UNARY);
      // Wrap as a negation: 0 - operand
      auto zero = std::make_unique<AST::IntLiteral>(0, tok.location);
      return std::make_unique<AST::BinaryExpr>(
          TokenType::MINUS, std::move(zero), std::move(operand), tok.location);
    });

    registerInfix(TokenType::LEFT_SQ_BRACE, elpc::Precedence::PRIMARY,
                  [this](const auto &tok, ExprNode lhs) -> ExprNode {
                    auto index = parseExpression(elpc::Precedence::NONE);
                    expect(TokenType::RIGHT_SQ_BRACE,
                           "Expected ']' after index.");

                    return std::make_unique<AST::IndexExpr>(
                        std::move(lhs), std::move(index), tok.location);
                  });

    // Logical operators
    auto logicalRule = [this](const auto &tok, ExprNode lhs) -> ExprNode {
      auto rhs = parseExpression(tok.type == TokenType::AND
                                     ? elpc::Precedence::LOGICAL_AND
                                     : elpc::Precedence::LOGICAL_OR);
      return std::make_unique<AST::BinaryExpr>(tok.type, std::move(lhs),
                                               std::move(rhs), tok.location);
    };
    registerInfix(TokenType::OR, elpc::Precedence::LOGICAL_OR, logicalRule);
    registerInfix(TokenType::AND, elpc::Precedence::LOGICAL_AND, logicalRule);

    // Modulo
    registerInfix(TokenType::PERCENT, elpc::Precedence::FACTOR,
                  [this](const auto &tok, ExprNode lhs) -> ExprNode {
                    auto rhs = parseExpression(elpc::Precedence::FACTOR);
                    return std::make_unique<AST::BinaryExpr>(
                        tok.type, std::move(lhs), std::move(rhs), tok.location);
                  });

    // Binary Operators (+, -) -> Precedence::TERM
    auto termRule = [this](const auto &tok, ExprNode lhs) -> ExprNode {
      auto rhs = parseExpression(elpc::Precedence::TERM);
      return std::make_unique<AST::BinaryExpr>(tok.type, std::move(lhs),
                                               std::move(rhs), tok.location);
    };
    registerInfix(TokenType::PLUS, elpc::Precedence::TERM, termRule);
    registerInfix(TokenType::MINUS, elpc::Precedence::TERM, termRule);

    // Binary Operators (*, /) -> Precedence::FACTOR
    auto factorRule = [this](const auto &tok, ExprNode lhs) -> ExprNode {
      auto rhs = parseExpression(elpc::Precedence::FACTOR);
      return std::make_unique<AST::BinaryExpr>(tok.type, std::move(lhs),
                                               std::move(rhs), tok.location);
    };
    registerInfix(TokenType::STAR, elpc::Precedence::FACTOR, factorRule);
    registerInfix(TokenType::SLASH, elpc::Precedence::FACTOR, factorRule);

    // Precedence Operators
    auto cmpRule = [this](const auto &tok, ExprNode lhs) -> ExprNode {
      auto rhs = parseExpression(elpc::Precedence::COMPARISON);
      return std::make_unique<AST::BinaryExpr>(tok.type, std::move(lhs),
                                               std::move(rhs), tok.location);
    };

    registerInfix(TokenType::LESS, elpc::Precedence::COMPARISON, cmpRule);
    registerInfix(TokenType::GREATER, elpc::Precedence::COMPARISON, cmpRule);
    registerInfix(TokenType::LESS_EQ, elpc::Precedence::COMPARISON, cmpRule);
    registerInfix(TokenType::GREATER_EQ, elpc::Precedence::COMPARISON, cmpRule);

    registerInfix(TokenType::EQ_EQ, elpc::Precedence::EQUALITY,
                  [this](const auto &tok, ExprNode lhs) -> ExprNode {
                    auto rhs = parseExpression(elpc::Precedence::EQUALITY);
                    return std::make_unique<AST::BinaryExpr>(
                        tok.type, std::move(lhs), std::move(rhs), tok.location);
                  });

    registerInfix(TokenType::NOT_EQ, elpc::Precedence::EQUALITY,
                  [this](const auto &tok, ExprNode lhs) -> ExprNode {
                    auto rhs = parseExpression(elpc::Precedence::EQUALITY);
                    return std::make_unique<AST::BinaryExpr>(
                        tok.type, std::move(lhs), std::move(rhs), tok.location);
                  });

    registerInfix(TokenType::EQUAL, elpc::Precedence::ASSIGNMENT,
                  [this](const auto &tok, ExprNode lhs) -> ExprNode {
                    auto *ident = dynamic_cast<AST::Identifier *>(lhs.get());
                    if (!ident)
                      throw std::runtime_error("Invalid assignment target.");

                    // Evaluate the right side of the equals sign
                    auto rhs = parseExpression(elpc::Precedence::NONE);
                    return std::make_unique<AST::AssignExpr>(
                        ident->name, std::move(rhs), tok.location);
                  });

    registerInfix(
        TokenType::LEFT_PAREN, elpc::Precedence::PRIMARY,
        [this](const auto &tok, ExprNode lhs) -> ExprNode {
          auto *ident = dynamic_cast<AST::Identifier *>(lhs.get());
          if (!ident)
            throw std::runtime_error("Can only call functions by name.");

          std::string callee = ident->name;
          std::vector<ExprNode> args;

          if (!check(TokenType::RIGHT_PAREN)) {
            do {
              args.push_back(parseExpression(elpc::Precedence::NONE));
            } while (match(TokenType::COMMA));
          }

          expect(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");

          return std::make_unique<AST::CallExpr>(callee, std::move(args),
                                                 tok.location);
        });
  }

  // === RECURSIVE DESCENT RULES ===

  std::vector<std::unique_ptr<AST::Node>> parse() {
    std::vector<std::unique_ptr<AST::Node>> program;
    while (!isAtEnd()) {
      if (check(TokenType::FUNC)) {
        program.push_back(parseFunctionDecl());
      } else {
        auto t = peek();
        throw std::runtime_error("[hydrolox] Only function declarations "
                                 "allowed at top level. Found: " +
                                 t.lexeme);
      }
    }
    return program;
  }

private:
  std::unique_ptr<AST::FunctionDecl> parseFunctionDecl() {
    auto funcTok = consume(); // Consume 'func'

    auto typeTok =
        expectOneOf("Expected return type (e.g. 'i32').", TokenType::U8,
                    TokenType::I32, TokenType::BOOL, TokenType::STRING);
    auto nameTok = expect(TokenType::IDENT, "Expected function name.");

    expect(TokenType::LEFT_PAREN, "Expected '(' after function name.");

    std::vector<std::pair<std::string, TokenType>> params;
    if (!check(TokenType::RIGHT_PAREN)) {
      do {
        auto paramName = expect(TokenType::IDENT, "Expected parameter name.");
        expect(TokenType::COLON, "Expected ':'.");
        auto paramType =
            expectOneOf("Expected parameter type.", TokenType::U8,
                        TokenType::I32, TokenType::BOOL, TokenType::STRING);
        params.push_back({paramName.lexeme, paramType.type});
      } while (match(TokenType::COMMA));
    }

    expect(TokenType::RIGHT_PAREN, "Expected ')' after parameters.");

    auto body = parseBlock();

    return std::make_unique<AST::FunctionDecl>(
        nameTok.lexeme, std::move(params), typeTok.type, std::move(body),
        funcTok.location);
  }

  std::unique_ptr<AST::BlockStmt> parseBlock() {
    auto braceTok =
        expect(TokenType::LEFT_BRACE, "Expected '{' before block body.");

    std::vector<std::unique_ptr<AST::Stmt>> stmts;
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
      stmts.push_back(parseStatement());
    }

    expect(TokenType::RIGHT_BRACE, "Expected '}' after block body.");
    return std::make_unique<AST::BlockStmt>(std::move(stmts),
                                            braceTok.location);
  }

  std::unique_ptr<AST::Stmt> parseStatement() {
    if (check(TokenType::IF))
      return parseIfStmt();
    if (check(TokenType::WHILE))
      return parseWhileStmt();
    if (check(TokenType::FOR))
      return parseForStmt();
    if (check(TokenType::RETURN))
      return parseReturnStmt();
    if (check(TokenType::IDENT) && peek(1).is(TokenType::COLON))
      return parseVarDecl();

    return parseExprStmt();
  }

  std::unique_ptr<AST::IfStmt> parseIfStmt() {
    auto ifTok = consume(); // Consume 'if'

    // Parenthesis for the condition
    expect(TokenType::LEFT_PAREN, "Expected '(' after if statement");
    auto condition = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::RIGHT_PAREN, "Expected ')' during if statement");
    auto thenBranch = parseBlock();

    std::unique_ptr<AST::BlockStmt> elseBranch = nullptr;
    if (match(TokenType::ELSE)) {
      elseBranch = parseBlock();
    }

    return std::make_unique<AST::IfStmt>(std::move(condition),
                                         std::move(thenBranch),
                                         std::move(elseBranch), ifTok.location);
  }

  std::unique_ptr<AST::WhileStmt> parseWhileStmt() {
    auto whileTok = consume(); // Consume 'while'

    expect(TokenType::LEFT_PAREN, "Expected '(' after while statement");
    auto condition = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::RIGHT_PAREN, "Expected ')' during while statement");

    auto whileBranch = parseBlock();

    return std::make_unique<AST::WhileStmt>(
        std::move(condition), std::move(whileBranch), whileTok.location);
  }

  std::unique_ptr<AST::ForStmt> parseForStmt() {
    auto forTok = consume(); // consume 'for'
    expect(TokenType::LEFT_PAREN, "Expected '(' after 'for'.");

    // Init: must be a var decl — e.g. i: i32 = 0
    auto init = parseVarDecl(); // consumes the semicolon itself

    // Condition
    auto condition = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::SEMICOLON, "Expected ';' after for condition.");

    // Increment — e.g. i++ or i = i + 1, no semicolon
    auto increment = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::RIGHT_PAREN, "Expected ')' after for increment.");

    auto body = parseBlock();

    return std::make_unique<AST::ForStmt>(std::move(init), std::move(condition),
                                          std::move(increment), std::move(body),
                                          forTok.location);
  }

  std::unique_ptr<AST::ReturnStmt> parseReturnStmt() {
    auto retTok = consume(); // Consume 'return'
    auto expr = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::SEMICOLON, "Expected ';' after return value.");

    return std::make_unique<AST::ReturnStmt>(std::move(expr), retTok.location);
  }

  std::unique_ptr<AST::VarDecl> parseVarDecl() {
    auto nameTok = consume(); // We know it's an IDENT
    expect(TokenType::COLON, "Expected ':' after variable name.");
    auto typeTok =
        expectOneOf("Expected variable type.", TokenType::U8, TokenType::I32,
                    TokenType::BOOL, TokenType::STRING);

    std::optional<size_t> arraySize;
    if (match(TokenType::LEFT_SQ_BRACE)) {
      auto sizeTok = expect(TokenType::INT_LIT, "Expected array size.");
      arraySize = static_cast<size_t>(sizeTok.toInt().value_or(0));
      expect(TokenType::RIGHT_SQ_BRACE, "Expected ']' after array size.");
    }

    expect(TokenType::EQUAL, "Expected '=' after type.");

    auto init = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::SEMICOLON, "Expected ';' after variable declaration.");

    return std::make_unique<AST::VarDecl>(nameTok.lexeme, typeTok.type,
                                          arraySize, std::move(init),
                                          nameTok.location);
  }

  std::unique_ptr<AST::ExprStmt> parseExprStmt() {
    auto expr = parseExpression(elpc::Precedence::NONE);
    expect(TokenType::SEMICOLON, "Expected ';' after expression.");
    return std::make_unique<AST::ExprStmt>(std::move(expr), expr->loc);
  }
};
