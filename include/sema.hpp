#pragma once

#include <ast.hpp>
#include <elpc/elpc.hpp>
#include <optional>
#include <string>

class SemanticAnalyzer : public elpc::Sema, public AST::ASTVisitor {
  // Symbol table tracks the *types* of variables in scope
  elpc::SymbolTable<std::string, TokenType> symbols;

  // Tracks the inferred type of the currently evaluated expression
  std::optional<TokenType> currentExprType;

  // Tracks the return type of the function we are currently analyzing
  std::optional<TokenType> currentExpectedReturnType;

  struct FuncSignature {
    TokenType retType;
    std::vector<TokenType> paramTypes;
    bool isVariadic = false;
  };
  std::unordered_map<std::string, FuncSignature> functions;

  struct ArrayInfo {
    TokenType elemType;
    size_t size;
  };
  elpc::SymbolTable<std::string, ArrayInfo> arrays;

public:
  SemanticAnalyzer(elpc::DiagnosticEngine &diag) : elpc::Sema(diag) {}

  void visit(const AST::IntLiteral &node) override {
    currentExprType = TokenType::I32; // It's an integer!
  }

  void visit(const AST::BoolLiteral &node) override {
    currentExprType = TokenType::BOOL;
  }

  void visit(const AST::StringLiteral &node) override {
    currentExprType = TokenType::STRING;
  }

  void visit(const AST::ArrayLiteral &node) override {
    if (node.elements.empty()) {
      error("Array literal cannot be empty", node.loc);
      currentExprType = std::nullopt;
      return;
    }

    node.elements[0]->accept(*this);
    auto elemType = currentExprType;

    for (size_t i = 1; i < node.elements.size(); i++) {
      node.elements[i]->accept(*this);
      if (currentExprType != elemType)
        error("Array literal elements must all have the same type", node.loc);
    }
    currentExprType = elemType;
  }

  void visit(const AST::Identifier &node) override {
    // When we add variables later, this will look them up!
    auto typeOpt = symbols.lookup(node.name);
    if (typeOpt) {
      currentExprType = *typeOpt;
      return;
    }

    auto arrOpt = arrays.lookup(node.name);
    if (arrOpt) {
      currentExprType = arrOpt->elemType;
      return;
    }
    error("Undeclared variable '" + node.name + "'", node.loc);
    currentExprType = std::nullopt;
  }

  void visit(const AST::BinaryExpr &node) override {
    node.left->accept(*this);
    auto leftType = currentExprType;
    node.right->accept(*this);
    auto rightType = currentExprType;

    if (!leftType || !rightType) {
      currentExprType = std::nullopt;
      return;
    }

    auto common = commonNumericType(*leftType, *rightType);
    if (common) {
      leftType = common;
      rightType = common;
    }

    switch (node.op) {
    case TokenType::LESS:
    case TokenType::GREATER:
    case TokenType::LESS_EQ:
    case TokenType::GREATER_EQ:
      if (!common) {
        error("Comparison operators require matching numeric operands",
              node.loc);
      }
      currentExprType = TokenType::BOOL;
      break;
    case TokenType::EQ_EQ:
    case TokenType::NOT_EQ:
      if (leftType != rightType) {
        error("Operands must have the same type for equality comparison",
              node.loc);
      }
      currentExprType = TokenType::BOOL;
      break;
    case TokenType::AND:
    case TokenType::OR:
      if (leftType != TokenType::BOOL || rightType != TokenType::BOOL)
        error("'&&' and '||' require bool operands", node.loc);
      currentExprType = TokenType::BOOL;
      break;
    case TokenType::PERCENT:
      if (!common) {
        error("'%' requires matching numeric operands", node.loc);
      }
      currentExprType = leftType;
      break;
    default:
      if (!common)
        error("Type mismatch in binary expression", node.loc);
      currentExprType = leftType;
      break;
    }
  }

  void visit(const AST::CallExpr &node) override {
    if (!functions.count(node.callee)) {
      error("Call to undefined function '" + node.callee + "'", node.loc);
      return;
    }

    auto &sig = functions[node.callee];
    if (sig.isVariadic) {
      if (node.args.size() < sig.paramTypes.size()) {
        error("Not enough arguments for variadic function '" + node.callee +
                  "'",
              node.loc);
      }
    } else {
      if (sig.paramTypes.size() != node.args.size()) {
        error("Function '" + node.callee + "' expects " +
                  std::to_string(sig.paramTypes.size()) + " args",
              node.loc);
      }
    }

    for (size_t i = 0; i < node.args.size(); ++i) {
      node.args[i]->accept(*this);

      if (i < sig.paramTypes.size()) {
        if (isImplicitWidening(*currentExprType, sig.paramTypes[i])) {
          // Allowed
        } else if (currentExprType != sig.paramTypes[i]) {
          error("Argument type mismatch in function call", node.loc);
        }
      }
    }
    currentExprType = sig.retType;
  }

  void visit(const AST::AssignExpr &node) override {
    auto varTypeOpt = symbols.lookup(node.name);
    if (!varTypeOpt) {
      error("Cannot assign to undeclared variable '" + node.name + "'",
            node.loc);
      return;
    }

    node.value->accept(*this);
    if (currentExprType && *varTypeOpt != currentExprType) {
      if (isImplicitWidening(*currentExprType, *varTypeOpt)) {
        // Implicit widening (u8 -> i32) is ALLOWED
      } else {
        error("Type mismatch in assignment. Explicit cast required.", node.loc);
      }
    }
  }

  void visit(const AST::IndexExpr &node) override {
    auto *ident = dynamic_cast<const AST::Identifier *>(node.array.get());
    if (!ident) {
      error("Can only index named arrays", node.loc);
      currentExprType = std::nullopt;
      return;
    }

    auto arrOpt = arrays.lookup(ident->name);
    if (!arrOpt) {
      error("'" + ident->name + "' is not an array", node.loc);
      currentExprType = std::nullopt;
      return;
    }

    node.index->accept(*this);
    if (currentExprType != TokenType::I32)
      error("Array index must be i32", node.loc);

    currentExprType = arrOpt->elemType;
  }

  void visit(const AST::CastExpr &node) override {
    node.expr->accept(*this);

    // Safety check: Don't allow casting a string to an integer!
    if (currentExprType == TokenType::STRING &&
        node.targetType != TokenType::STRING) {
      error("Cannot cast string to numeric type", node.loc);
    }

    currentExprType = node.targetType; // The type is now explicitly changed
  }

  void visit(const AST::ExprStmt &node) override { node.expr->accept(*this); }

  void visit(const AST::ReturnStmt &node) override {
    node.value->accept(*this);

    if (currentExprType && currentExpectedReturnType) {
      if (isImplicitWidening(*currentExprType, *currentExpectedReturnType)) {
        // Allowed!
      } else if (currentExprType != currentExpectedReturnType) {
        error("Return type mismatch.", node.loc);
      }
    }
  }

  void visit(const AST::BlockStmt &node) override {
    symbols.pushScope();
    arrays.pushScope();

    for (const auto &stmt : node.statements) {
      stmt->accept(*this);
    }

    arrays.popScope();
    symbols.popScope();
  }

  void visit(const AST::IfStmt &node) override {
    node.condition->accept(*this);
    if (!currentExprType) {
      error("Invalid condition in if statement", node.loc);
    }

    node.thenBranch->accept(*this);
    if (node.elseBranch) {
      node.elseBranch->accept(*this);
    }
  }

  void visit(const AST::WhileStmt &node) override {
    node.condition->accept(*this);
    if (!currentExprType) {
      error("Invalid condition in while statement", node.loc);
    }

    node.whileBranch->accept(*this);
  }

  void visit(const AST::ForStmt &node) override {
    symbols.pushScope();
    arrays.pushScope();

    node.init->accept(*this);

    node.condition->accept(*this);
    if (!currentExprType)
      error("Invalid for loop condition", node.loc);

    node.increment->accept(*this);
    node.body->accept(*this);

    arrays.popScope();
    symbols.popScope();
  }

  void visit(const AST::VarDecl &node) override {
    node.initializer->accept(*this);

    if (currentExprType == TokenType::I32 && node.type == TokenType::U8) {
      currentExprType = TokenType::U8;
    }

    if (node.isArray()) {
      // Register in array table
      if (!arrays.define(node.name, {node.type, *node.arraySize}))
        error("Variable '" + node.name + "' already declared in this scope",
              node.loc);

      // Check initializer is an array literal with matching element type
      if (currentExprType && currentExprType != node.type)
        error("Array element type mismatch in declaration", node.loc);
    } else {
      if (currentExprType && currentExprType != node.type) {
        if (isImplicitWidening(*currentExprType, node.type)) {
          // Implicit widening is ALLOWED
        } else {
          error("Type mismatch in declaration. Explicit cast required.",
                node.loc);
        }
      }

      if (!symbols.define(node.name, node.type))
        error("Variable '" + node.name + "' already declared in this scope",
              node.loc);
    }
  }

  void visit(const AST::FunctionDecl &node) override {
    std::vector<TokenType> pTypes;
    for (auto &p : node.params)
      pTypes.push_back(p.second);
    functions[node.name] = {node.returnType, pTypes};

    currentExpectedReturnType = node.returnType;
    symbols.pushScope();
    arrays.pushScope();

    for (auto &p : node.params)
      symbols.define(p.first, p.second);

    node.body->accept(*this);

    arrays.popScope();
    symbols.popScope();
    currentExpectedReturnType = std::nullopt;
  }

  void visit(const AST::ExternDecl &node) override {
    functions[node.name] = {node.returnType, node.paramTypes, node.isVariadic};
  }

private:
  // This helper function returns the larger of the two numeric types, or
  // nullopt if incompatible with each other (u8 = a, i32 = b -> b is wider)
  std::optional<TokenType> commonNumericType(TokenType a, TokenType b) {

    // Helper lambda to get the rank of the type as compared to the other types
    // as an integer
    auto rank = [](TokenType t) -> int {
      if (t == TokenType::U8)
        return 0;
      if (t == TokenType::U16)
        return 1;
      if (t == TokenType::I32)
        return 2;
      return -1;
    };

    int ra = rank(a), rb = rank(b);
    if (ra < 0 || rb < 0)
      return std::nullopt; // The types aren't any of the types we listed
                           // (non-numeric)
    return (ra >= rb) ? a : b;
  }

  bool isImplicitWidening(TokenType from, TokenType to) {
    if (to == TokenType::I32 &&
        (from == TokenType::U8 || from == TokenType::U16))
      return true;
    if (to == TokenType::U16 && from == TokenType::U8)
      return true;
    // A little cheaty, but for now it works
    if (from == TokenType::I32 && (to == TokenType::U8 || to == TokenType::U16))
      return true;
    return false;
  };
};
