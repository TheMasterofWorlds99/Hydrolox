#pragma once

#include <elpc/elpc.hpp>

enum class TokenType {
  // KEYWORDS
  FUNC,
  RETURN,
  IF,
  ELSE,
  WHILE,
  FOR,

  // TYPES (I32, F32, etc)
  U8,
  I32,
  BOOL,
  STRING,

  // LITERALS (2, 3.141, 3.1i, etc)
  INT_LIT,
  BOOL_LIT,
  STRING_LIT,

  // SYMBOLS
  LEFT_PAREN,
  RIGHT_PAREN,
  COLON,
  SEMICOLON,
  COMMA,
  EQUAL,
  LEFT_BRACE,
  RIGHT_BRACE,
  LEFT_SQ_BRACE,
  RIGHT_SQ_BRACE,
  BANG,
  PERCENT,

  // OPERATORS
  PLUS,
  MINUS,
  STAR,
  SLASH,

  //-Crements
  PLUS_PLUS,
  MINUS_MINUS,

  // COMPARISONS
  LESS,
  GREATER,
  LESS_EQ,
  GREATER_EQ,
  EQ_EQ,
  NOT_EQ,

  // LOGICAL OPERATORS
  AND,
  OR,

  // IDENTIFIER
  IDENT
};

inline elpc::Lexer<TokenType> GenerateLexer(const std::string &src) {
  elpc::Lexer<TokenType> lexer(src);

  // Keywords
  lexer.addRule(TokenType::FUNC, "\\bfunc\\b");
  lexer.addRule(TokenType::RETURN, "\\breturn\\b");
  lexer.addRule(TokenType::IF, "\\bif\\b");
  lexer.addRule(TokenType::ELSE, "\\belse\\b");
  lexer.addRule(TokenType::WHILE, "\\bwhile\\b");
  lexer.addRule(TokenType::FOR, "\\bfor\\b");

  // Types
  lexer.addRule(TokenType::U8, "\\bu8\\b");
  lexer.addRule(TokenType::I32, "\\bi32\\b");
  lexer.addRule(TokenType::BOOL, "\\bbool\\b");
  lexer.addRule(TokenType::STRING, "\\bstring\\b");

  // Literals
  lexer.addRule(TokenType::INT_LIT, "\\b[0-9]+\\b");
  lexer.addRule(TokenType::BOOL_LIT, "\\b(true|false)\\b");
  lexer.addRule(TokenType::STRING_LIT, "\"([^\"]*)\"");

  // Operators
  lexer.addRule(TokenType::PLUS, "\\+");
  lexer.addRule(TokenType::MINUS, "\\-");
  lexer.addRule(TokenType::STAR, "\\*");
  lexer.addRule(TokenType::SLASH, "\\/");

  //-Crements
  lexer.addRule(TokenType::PLUS_PLUS, "\\+\\+");
  lexer.addRule(TokenType::MINUS_MINUS, "\\-\\-");

  // Comparisons
  lexer.addRule(TokenType::LESS_EQ, "\\<\\=");
  lexer.addRule(TokenType::GREATER_EQ, "\\>\\=");
  lexer.addRule(TokenType::EQ_EQ, "\\=\\=");
  lexer.addRule(TokenType::NOT_EQ, "\\!\\=");
  lexer.addRule(TokenType::LESS, "\\<");
  lexer.addRule(TokenType::GREATER, "\\>");

  // Symbols
  lexer.addRule(TokenType::LEFT_PAREN, "\\(");
  lexer.addRule(TokenType::RIGHT_PAREN, "\\)");
  lexer.addRule(TokenType::LEFT_BRACE, "\\{");
  lexer.addRule(TokenType::RIGHT_BRACE, "\\}");
  lexer.addRule(TokenType::LEFT_SQ_BRACE, "\\[");
  lexer.addRule(TokenType::RIGHT_SQ_BRACE, "\\]");
  lexer.addRule(TokenType::COLON, "\\:");
  lexer.addRule(TokenType::SEMICOLON, "\\;");
  lexer.addRule(TokenType::COMMA, "\\,");
  lexer.addRule(TokenType::EQUAL, "\\=");
  lexer.addRule(TokenType::BANG, "\\!");
  lexer.addRule(TokenType::PERCENT, "\\%");

  // Logical Operators
  lexer.addRule(TokenType::AND, "&&");
  lexer.addRule(TokenType::OR, "\\|\\|");

  // Identifier
  lexer.addRule(TokenType::IDENT, "\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");

  lexer.addSkip("\\s+");
  lexer.addSkip("//[^\n]*");

  return lexer;
}
