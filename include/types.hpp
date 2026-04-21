#pragma once

#include <memory>
#include <string>
#include <variant>

enum class TokenType {
  // KEYWORDS
  FUNC,
  EXTERN,
  RETURN,
  IF,
  ELSE,
  WHILE,
  FOR,
  STRUCT,

  // TYPES (I32, F32, etc)
  U8,
  U16,
  I32,
  I64,
  F32,
  F64,
  BOOL,
  STRING,
  STR,
  STRUCT_TYPE,

  // VECTORS
  VEC,
  VEC2,
  VEC3,
  VEC4,
  DVEC2,
  DVEC3,
  DVEC4,
  IVEC2,
  IVEC3,
  IVEC4,

  // LITERALS (2, 3.141, 3.1i, etc)
  INT_LIT,
  FLOAT_LIT,
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
  TRIPLEDOT,
  DOT,

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

struct TypeInfo;

struct PrimitiveType {
  TokenType tag; // I32, F32, etc
};

struct VectorType {
  std::shared_ptr<TypeInfo> base;
  int size;
};

struct ArrayType {
  std::shared_ptr<TypeInfo> base;
  int size;
};

struct StructType {
  std::string name;
};

using TypeVariant =
    std::variant<PrimitiveType, VectorType, ArrayType, StructType>;

// The helpers for std::visit
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct TypeInfo {
  TypeVariant data;

  // Helper shortcuts
  bool isPrimitive() const {
    return std::holds_alternative<PrimitiveType>(data);
  }
  bool isVector() const { return std::holds_alternative<VectorType>(data); }
  bool isArray() const { return std::holds_alternative<ArrayType>(data); }
  bool isStruct() const { return std::holds_alternative<StructType>(data); }

  // Required for Symbol Table lookups and Type Checking
  bool operator==(const TypeInfo &other) const;
  bool operator!=(const TypeInfo &other) const { return !(*this == other); }
};
