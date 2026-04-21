#pragma once

#include <elpc/elpc.hpp>
#include <types.hpp>

inline elpc::Lexer<TokenType> GenerateLexer(const std::string &src) {
  elpc::Lexer<TokenType> lexer(src);

  // Keywords
  lexer.addRule(TokenType::FUNC, "\\bfunc\\b");
  lexer.addRule(TokenType::EXTERN, "\\bextern\\b");
  lexer.addRule(TokenType::RETURN, "\\breturn\\b");
  lexer.addRule(TokenType::IF, "\\bif\\b");
  lexer.addRule(TokenType::ELSE, "\\belse\\b");
  lexer.addRule(TokenType::WHILE, "\\bwhile\\b");
  lexer.addRule(TokenType::FOR, "\\bfor\\b");
  lexer.addRule(TokenType::STRUCT, "\\bstruct\\b");

  // Types
  lexer.addRule(TokenType::U8, "\\bu8\\b");
  lexer.addRule(TokenType::U16, "\\bu16\\b");
  lexer.addRule(TokenType::I32, "\\bi32\\b");
  lexer.addRule(TokenType::I64, "\\bi64\\b");
  lexer.addRule(TokenType::F32, "\\bf32\\b");
  lexer.addRule(TokenType::F64, "\\bf64\\b");
  lexer.addRule(TokenType::BOOL, "\\bbool\\b");
  lexer.addRule(TokenType::STRING, "\\bstring\\b");
  lexer.addRule(TokenType::STR, "\\bstr\\b");

  // VECTORS
  lexer.addRule(TokenType::VEC, "\\bvec\\b");
  lexer.addRule(TokenType::VEC2, "\\bvec2\\b");
  lexer.addRule(TokenType::VEC3, "\\bvec3\\b");
  lexer.addRule(TokenType::VEC4, "\\bvec4\\b");
  lexer.addRule(TokenType::DVEC2, "\\bdvec2\\b");
  lexer.addRule(TokenType::DVEC3, "\\bdvec3\\b");
  lexer.addRule(TokenType::DVEC4, "\\bdvec4\\b");
  lexer.addRule(TokenType::IVEC2, "\\bivec2\\b");
  lexer.addRule(TokenType::IVEC3, "\\bivec3\\b");
  lexer.addRule(TokenType::IVEC4, "\\bivec4\\b");

  // Literals
  lexer.addRule(TokenType::INT_LIT, "\\b[0-9]+\\b");
  lexer.addRule(TokenType::FLOAT_LIT, "\\b[0-9]+\\.[0-9]+\\b");
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
  lexer.addRule(TokenType::TRIPLEDOT, "\\.\\.\\.");
  lexer.addRule(TokenType::DOT, "\\.");

  // Logical Operators
  lexer.addRule(TokenType::AND, "&&");
  lexer.addRule(TokenType::OR, "\\|\\|");

  // Identifier
  lexer.addRule(TokenType::IDENT, "\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");

  lexer.addSkip("\\s+");
  lexer.addSkip("//[^\n]*");

  return lexer;
}
