#pragma once

#include "elpc/core/loc.hpp"
#include <cstdint>
#include <elpc/elpc.hpp>
#include <lexer.hpp>
#include <memory>
#include <optional>

namespace AST {

// === LITERAL ===
struct IntLiteral;
struct BoolLiteral;
struct StringLiteral;
struct ArrayLiteral;
struct Identifier;

// === EXPR ===
struct BinaryExpr;
struct CallExpr;
struct AssignExpr;
struct IndexExpr;
struct CastExpr;

// === STMT ===
struct ExprStmt;
struct ReturnStmt;
struct BlockStmt;
struct IfStmt;
struct WhileStmt;
struct ForStmt;

// === DECL ===
struct VarDecl;
struct FunctionDecl;

// === AST VISITOR ===
class ASTVisitor {
public:
  virtual ~ASTVisitor() = default;
  virtual void visit(const IntLiteral &node) = 0;
  virtual void visit(const BoolLiteral &node) = 0;
  virtual void visit(const StringLiteral &node) = 0;
  virtual void visit(const ArrayLiteral &node) = 0;
  virtual void visit(const Identifier &node) = 0;

  virtual void visit(const BinaryExpr &node) = 0;
  virtual void visit(const CallExpr &node) = 0;
  virtual void visit(const AssignExpr &node) = 0;
  virtual void visit(const IndexExpr &node) = 0;
  virtual void visit(const CastExpr &node) = 0;

  virtual void visit(const ExprStmt &node) = 0;
  virtual void visit(const ReturnStmt &node) = 0;
  virtual void visit(const BlockStmt &node) = 0;
  virtual void visit(const IfStmt &node) = 0;
  virtual void visit(const WhileStmt &node) = 0;
  virtual void visit(const ForStmt &node) = 0;

  virtual void visit(const VarDecl &node) = 0;
  virtual void visit(const FunctionDecl &node) = 0;
};

// === BASE NODE ===
struct Node {
  elpc::SourceLocation loc;
  virtual ~Node() = default;
  virtual void accept(ASTVisitor &visitor) const = 0;
};

// === EXPR ===
struct Expr : public Node {};

struct IntLiteral : public Expr {
  int32_t value;

  IntLiteral(int32_t val, elpc::SourceLocation loc) : value(val) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct BoolLiteral : public Expr {
  bool value;

  BoolLiteral(bool val, elpc::SourceLocation loc) : value(val) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct StringLiteral : public Expr {
  std::string value;

  StringLiteral(std::string val, elpc::SourceLocation loc)
      : value(std::move(val)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct ArrayLiteral : public Expr {
  std::vector<std::unique_ptr<Expr>> elements;

  ArrayLiteral(std::vector<std::unique_ptr<Expr>> elements,
               elpc::SourceLocation loc)
      : elements(std::move(elements)) {
    this->loc = loc;
  }
  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct Identifier : public Expr {
  std::string name;

  Identifier(std::string name, elpc::SourceLocation loc)
      : name(std::move(name)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct BinaryExpr : public Expr {
  TokenType op;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;

  BinaryExpr(TokenType op, std::unique_ptr<Expr> left,
             std::unique_ptr<Expr> right, elpc::SourceLocation loc)
      : op(op), left(std::move(left)), right(std::move(right)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct CallExpr : public Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> args;

  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> args,
           elpc::SourceLocation loc)
      : callee(std::move(callee)), args(std::move(args)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct AssignExpr : public Expr {
  std::string name;
  std::unique_ptr<Expr> value;

  AssignExpr(std::string name, std::unique_ptr<Expr> value,
             elpc::SourceLocation loc)
      : name(std::move(name)), value(std::move(value)) {
    this->loc = loc;
  }
  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct IndexExpr : public Expr {
  std::unique_ptr<Expr> array;
  std::unique_ptr<Expr> index;

  IndexExpr(std::unique_ptr<Expr> array, std::unique_ptr<Expr> index,
            elpc::SourceLocation loc)
      : array(std::move(array)), index(std::move(index)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct CastExpr : public Expr {
  TokenType targetType;
  std::unique_ptr<Expr> expr;

  CastExpr(TokenType targetType, std::unique_ptr<Expr> expr,
           elpc::SourceLocation loc)
      : targetType(targetType), expr(std::move(expr)) {
    this->loc = loc;
  }
  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

// === STMT ===
struct Stmt : public Node {};

struct ExprStmt : public Stmt {
  std::unique_ptr<Expr> expr;

  ExprStmt(std::unique_ptr<Expr> expr, elpc::SourceLocation loc)
      : expr(std::move(expr)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct ReturnStmt : public Stmt {
  std::unique_ptr<Expr> value;

  ReturnStmt(std::unique_ptr<Expr> val, elpc::SourceLocation loc)
      : value(std::move(val)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct BlockStmt : public Stmt {
  std::vector<std::unique_ptr<Stmt>> statements;

  BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts, elpc::SourceLocation loc)
      : statements(std::move(stmts)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct IfStmt : public Stmt {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<BlockStmt> thenBranch;
  std::unique_ptr<BlockStmt> elseBranch; // Can be nullptr!

  IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<BlockStmt> thenBranch,
         std::unique_ptr<BlockStmt> elseBranch, elpc::SourceLocation loc)
      : condition(std::move(condition)), thenBranch(std::move(thenBranch)),
        elseBranch(std::move(elseBranch)) {
    this->loc = loc;
  }
  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct WhileStmt : public Stmt {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<BlockStmt> whileBranch;

  WhileStmt(std::unique_ptr<Expr> condition,
            std::unique_ptr<BlockStmt> whileBranch, elpc::SourceLocation loc)
      : condition(std::move(condition)), whileBranch(std::move(whileBranch)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

// === DECLS ===
struct VarDecl : public Stmt {
  std::string name;
  TokenType type;
  std::optional<size_t> arraySize;
  std::unique_ptr<Expr> initializer;

  VarDecl(std::string name, TokenType type, std::optional<size_t> arraySize,
          std::unique_ptr<Expr> init, elpc::SourceLocation loc)
      : name(std::move(name)), type(type), arraySize(arraySize),
        initializer(std::move(init)) {
    this->loc = loc;
  }

  bool isArray() const { return arraySize.has_value(); }
  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

struct FunctionDecl : public Node {
  std::string name;
  std::vector<std::pair<std::string, TokenType>> params;
  TokenType returnType;
  std::unique_ptr<BlockStmt> body;

  FunctionDecl(std::string name,
               std::vector<std::pair<std::string, TokenType>> params,
               TokenType retType, std::unique_ptr<BlockStmt> body,
               elpc::SourceLocation loc)
      : name(std::move(name)), params(std::move(params)), returnType(retType),
        body(std::move(body)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

// === STMT ===
struct ForStmt : public Stmt {
  std::unique_ptr<VarDecl> init;
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> increment;
  std::unique_ptr<BlockStmt> body;

  ForStmt(std::unique_ptr<VarDecl> init, std::unique_ptr<Expr> condition,
          std::unique_ptr<Expr> increment, std::unique_ptr<BlockStmt> body,
          elpc::SourceLocation loc)
      : init(std::move(init)), condition(std::move(condition)),
        increment(std::move(increment)), body(std::move(body)) {
    this->loc = loc;
  }

  void accept(ASTVisitor &visitor) const override { visitor.visit(*this); }
};

} // namespace AST
