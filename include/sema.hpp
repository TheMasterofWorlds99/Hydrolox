#pragma once

#include <ast.hpp>
#include <elpc/elpc.hpp>
#include <optional>
#include <string>

class SemanticAnalyzer : public elpc::Sema, public AST::ASTVisitor {
  // Symbol table natively tracks Primitives, Arrays, and Structs
  elpc::SymbolTable<std::string, TypeInfo> symbols;

  std::optional<TypeInfo> currentExprType;
  std::optional<TypeInfo> currentExpectedReturnType;

  struct FuncSignature {
    TypeInfo retType;
    std::vector<TypeInfo> paramTypes;
    bool isVariadic = false;
  };
  std::unordered_map<std::string, FuncSignature> functions;

  struct StructInfo {
    std::vector<std::pair<std::string, TypeInfo>> fields;

    std::optional<std::pair<size_t, TypeInfo>>
    findField(const std::string &name) const {
      for (size_t i = 0; i < fields.size(); i++)
        if (fields[i].first == name)
          return {{i, fields[i].second}};
      return std::nullopt;
    }
  };
  std::unordered_map<std::string, StructInfo> structs;

public:
  SemanticAnalyzer(elpc::DiagnosticEngine &diag) : elpc::Sema(diag) {}

  void visit(const AST::IntLiteral &node) override {
    currentExprType = TypeInfo{PrimitiveType{TokenType::I32}};
  }
  void visit(const AST::FloatLiteral &node) override {
    currentExprType = TypeInfo{PrimitiveType{TokenType::F64}};
  }
  void visit(const AST::BoolLiteral &node) override {
    currentExprType = TypeInfo{PrimitiveType{TokenType::BOOL}};
  }
  void visit(const AST::StringLiteral &node) override {
    currentExprType = TypeInfo{PrimitiveType{TokenType::STR}};
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

    currentExprType = TypeInfo{ArrayType{std::make_shared<TypeInfo>(*elemType),
                                         (int)node.elements.size()}};
  }

  void visit(const AST::StructLiteral &node) override {
    auto it = structs.find(node.typeName);
    if (it == structs.end()) {
      error("Unknown struct type '" + node.typeName + "'", node.loc);
      currentExprType = std::nullopt;
      return;
    }
    const auto &info = it->second;
    for (auto &[name, expr] : node.fields) {
      auto fieldOpt = info.findField(name);
      if (!fieldOpt) {
        error("Struct '" + node.typeName + "' has no field '" + name + "'",
              node.loc);
        continue;
      }
      expr->accept(*this);
      if (currentExprType && *currentExprType != fieldOpt->second) {
        if (!isImplicitWidening(*currentExprType, fieldOpt->second))
          error("Type mismatch for field '" + name + "'", node.loc);
      }
    }
    currentExprType = TypeInfo{StructType{node.typeName}};
  }

  void visit(const AST::Identifier &node) override {
    auto typeOpt = symbols.lookup(node.name);
    if (typeOpt) {
      currentExprType = *typeOpt;
      if (currentExprType->isPrimitive() &&
          std::get<PrimitiveType>(currentExprType->data).tag ==
              TokenType::STRING) {
        currentExprType = TypeInfo{PrimitiveType{TokenType::STR}};
      }
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
    case TokenType::EQ_EQ:
    case TokenType::NOT_EQ:
      currentExprType = TypeInfo{PrimitiveType{TokenType::BOOL}};
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
    for (size_t i = 0; i < node.args.size(); ++i) {
      node.args[i]->accept(*this);
      if (i < sig.paramTypes.size() && currentExprType != sig.paramTypes[i] &&
          !isImplicitWidening(*currentExprType, sig.paramTypes[i])) {
        error("Argument type mismatch in function call", node.loc);
      }
    }
    currentExprType = sig.retType;
  }

  void visit(const AST::AssignExpr &node) override {
    auto varTypeOpt = symbols.lookup(node.name);
    node.value->accept(*this);
    if (varTypeOpt && currentExprType && *varTypeOpt != *currentExprType) {
      if (!isImplicitWidening(*currentExprType, *varTypeOpt))
        error("Type mismatch in assignment.", node.loc);
    }
  }

  void visit(const AST::IndexExpr &node) override {
    node.array->accept(*this);
    auto arrayType = currentExprType;

    if (!arrayType || !arrayType->isArray() && !arrayType->isVector()) {
      error("Cannot index non-array type", node.loc);
      return;
    }

    node.index->accept(*this);
    if (!currentExprType || !currentExprType->isPrimitive() ||
        std::get<PrimitiveType>(currentExprType->data).tag != TokenType::I32)
      error("Array index must be an integer", node.loc);

    if (arrayType->isVector()) {
      currentExprType = *std::get<VectorType>(arrayType->data).base;
    } else {
      currentExprType = *std::get<ArrayType>(arrayType->data).base;
    }
  }

  void visit(const AST::CastExpr &node) override {
    node.expr->accept(*this);
    currentExprType = node.targetType;
  }

  void visit(const AST::MemberAccessExpr &node) override {
    node.object->accept(*this);
    if (!currentExprType || !currentExprType->isStruct()) {
      error("Left of '.' is not a struct", node.loc);
      return;
    }

    std::string structName = std::get<StructType>(currentExprType->data).name;
    auto fieldOpt = structs[structName].findField(node.field);
    if (!fieldOpt) {
      error("Struct '" + structName + "' has no field '" + node.field + "'",
            node.loc);
      return;
    }
    currentExprType = fieldOpt->second;
  }

  void visit(const AST::MemberAssignExpr &node) override {
    auto varTypeOpt = symbols.lookup(node.objectName);
    if (!varTypeOpt || !varTypeOpt->isStruct()) {
      error("'" + node.objectName + "' is not a struct variable", node.loc);
      return;
    }

    std::string structName = std::get<StructType>(varTypeOpt->data).name;
    auto fieldOpt = structs[structName].findField(node.field);
    if (!fieldOpt) {
      error("Struct '" + structName + "' has no field '" + node.field + "'",
            node.loc);
      return;
    }

    node.value->accept(*this);
    if (currentExprType && *currentExprType != fieldOpt->second &&
        !isImplicitWidening(*currentExprType, fieldOpt->second)) {
      error("Type mismatch in member assignment", node.loc);
    }
  }

  void visit(const AST::ExprStmt &node) override { node.expr->accept(*this); }

  void visit(const AST::ReturnStmt &node) override {
    node.value->accept(*this);
    if (currentExprType && currentExpectedReturnType) {
      if (!isImplicitWidening(*currentExprType, *currentExpectedReturnType) &&
          *currentExprType != *currentExpectedReturnType) {
        error("Return type mismatch.", node.loc);
      }
    }
  }

  void visit(const AST::BlockStmt &node) override {
    symbols.pushScope();
    for (const auto &stmt : node.statements)
      stmt->accept(*this);
    symbols.popScope();
  }

  void visit(const AST::IfStmt &node) override {
    node.condition->accept(*this);
    node.thenBranch->accept(*this);
    if (node.elseBranch)
      node.elseBranch->accept(*this);
  }

  void visit(const AST::WhileStmt &node) override {
    node.condition->accept(*this);
    node.whileBranch->accept(*this);
  }

  void visit(const AST::ForStmt &node) override {
    symbols.pushScope();
    node.init->accept(*this);
    node.condition->accept(*this);
    node.increment->accept(*this);
    node.body->accept(*this);
    symbols.popScope();
  }

  void visit(const AST::VarDecl &node) override {
    node.initializer->accept(*this);
    TypeInfo declaredType = node.type;

    if (currentExprType && *currentExprType != declaredType &&
        !isImplicitWidening(*currentExprType, declaredType)) {
      error("Type mismatch in declaration.", node.loc);
    }

    if (!symbols.define(node.name, declaredType))
      error("Variable '" + node.name + "' already declared in this scope",
            node.loc);
  }

  void visit(const AST::FunctionDecl &node) override {
    std::vector<TypeInfo> pTypes;
    for (auto &p : node.params)
      pTypes.push_back(p.second);
    functions[node.name] = {node.returnType, pTypes};

    currentExpectedReturnType = node.returnType;
    symbols.pushScope();
    for (auto &p : node.params)
      symbols.define(p.first, p.second);
    node.body->accept(*this);
    symbols.popScope();
    currentExpectedReturnType = std::nullopt;
  }

  void visit(const AST::ExternDecl &node) override {
    functions[node.name] = {node.returnType, node.paramTypes, node.isVariadic};
  }

  void visit(const AST::StructDecl &node) override {
    structs[node.name] = {node.fields};
  }

private:
  std::optional<TypeInfo> commonNumericType(const TypeInfo &a,
                                            const TypeInfo &b) {
    if (a.isVector() && b.isVector()) {
      auto &va = std::get<VectorType>(a.data);
      auto &vb = std::get<VectorType>(b.data);
      if (va.size == vb.size && *va.base == *vb.base)
        return a;
      return std::nullopt;
    }

    if (!a.isPrimitive() || !b.isPrimitive())
      return std::nullopt;
    TokenType ta = std::get<PrimitiveType>(a.data).tag;
    TokenType tb = std::get<PrimitiveType>(b.data).tag;

    auto rank = [](TokenType t) -> int {
      if (t == TokenType::U8)
        return 0;
      if (t == TokenType::U16)
        return 1;
      if (t == TokenType::I32)
        return 2;
      if (t == TokenType::I64)
        return 3;
      if (t == TokenType::F32)
        return 4;
      if (t == TokenType::F64)
        return 5;
      return -1;
    };

    int ra = rank(ta), rb = rank(tb);
    if (ra < 0 || rb < 0)
      return std::nullopt;
    return (ra >= rb) ? a : b;
  }

  bool isImplicitWidening(const TypeInfo &from, const TypeInfo &to) {
    if (from.isArray() && to.isVector()) {
      auto &arr = std::get<ArrayType>(from.data);
      auto &vec = std::get<VectorType>(to.data);
      if (arr.size == vec.size && *arr.base == *vec.base ||
          isImplicitWidening(*arr.base, *vec.base))
        return true;
    }

    if (!from.isPrimitive() || !to.isPrimitive())
      return false;
    TokenType f = std::get<PrimitiveType>(from.data).tag;
    TokenType t = std::get<PrimitiveType>(to.data).tag;

    if (t == TokenType::F64 &&
        (f == TokenType::F32 || f == TokenType::I64 || f == TokenType::I32 ||
         f == TokenType::U16 || f == TokenType::U8))
      return true;
    if (t == TokenType::F32 && (f == TokenType::I64 || f == TokenType::I32 ||
                                f == TokenType::U16 || f == TokenType::U8))
      return true;
    if (t == TokenType::I64 &&
        (f == TokenType::U8 || f == TokenType::U16 || f == TokenType::I32))
      return true;
    if (t == TokenType::I32 && (f == TokenType::U8 || f == TokenType::U16))
      return true;
    if (t == TokenType::U16 && f == TokenType::U8)
      return true;
    return false;
  };
};
