#pragma once

#include <ast.hpp>
#include <elpc/elpc.hpp>
#include <elpc/ir/llvmBridge.hpp>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

struct ArrayVar {
  llvm::Value *ptr;
  llvm::Type *elemType;
  size_t size;
};

class ArrayTable {
  std::vector<std::unordered_map<std::string, ArrayVar>> scopes;

public:
  ArrayTable() { scopes.emplace_back(); }

  void pushScope() { scopes.emplace_back(); }
  void popScope() {
    if (scopes.size() > 1)
      scopes.pop_back();
  }

  void define(const std::string &name, ArrayVar var) {
    scopes.back()[name] = std::move(var);
  }

  ArrayVar *lookup(const std::string &name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end())
        return &found->second;
    }
    return nullptr;
  }
};

class LLVMGenerator : public AST::ASTVisitor {
  elpc::LLVMBridge &bridge;

  // Tghis holds the result of the last evaluated expr node
  llvm::Value *currentValue = nullptr;

  ArrayTable arrays;
  std::vector<llvm::Value *> lastArrayElements;

public:
  LLVMGenerator(elpc::LLVMBridge &bridge) : bridge(bridge) {}

  void visit(const AST::IntLiteral &node) override {
    // LLVM handles 32-bit ints perfectly natively
    currentValue = bridge.emitInt(node.value, 32);
  }

  void visit(const AST::BoolLiteral &node) override {
    currentValue = bridge.emitBool(node.value);
  }

  void visit(const AST::ArrayLiteral &node) override {
    lastArrayElements.clear();
    for (auto &elem : node.elements) {
      elem->accept(*this);
      lastArrayElements.push_back(currentValue);
    }
    currentValue = nullptr; // array literals don't produce a scalar
  }

  void visit(const AST::Identifier &node) override {
    auto varOpt = bridge.lookupVar(node.name, node.loc);
    if (!varOpt) {
      bridge.error("Variable not found in scope", node.loc);
      currentValue = nullptr;
      return;
    }

    auto var = *varOpt;
    currentValue = bridge.emitLoad(var.type, var.ptr, node.name + "_val");
  }

  void visit(const AST::BinaryExpr &node) override {
    node.left->accept(*this);
    llvm::Value *lhs = currentValue;

    node.right->accept(*this);
    llvm::Value *rhs = currentValue;

    if (!lhs || !rhs)
      return;

    switch (node.op) {
    case TokenType::PLUS:
      currentValue = bridge.emitAdd(lhs, rhs, "addtmp");
      break;
    case TokenType::MINUS:
      currentValue = bridge.emitSub(lhs, rhs, "subtmp");
      break;
    case TokenType::STAR:
      currentValue = bridge.emitMul(lhs, rhs, "multmp");
      break;
    case TokenType::SLASH:
      // We pass the source location so elpc can catch div-by-zero!
      currentValue = bridge.emitDiv(lhs, rhs, "divtmp", node.loc);
      break;
    case TokenType::LESS:
      currentValue = bridge.emitICmpLT(lhs, rhs, "cmptmp");
      break;

    case TokenType::GREATER:
      currentValue = bridge.emitICmpGT(lhs, rhs, "cmptmp");
      break;

    case TokenType::LESS_EQ:
      currentValue = bridge.emitICmpLTE(lhs, rhs, "cmptmp");
      break;

    case TokenType::GREATER_EQ:
      currentValue = bridge.emitICmpGTE(lhs, rhs, "cmptmp");
      break;

    case TokenType::EQ_EQ:
      currentValue = bridge.emitICmpEQ(lhs, rhs, "cmptmp");
      break;

    case TokenType::NOT_EQ:
      currentValue = bridge.emitICmpNE(lhs, rhs, "cmptmp");
      break;
    case TokenType::AND:
      currentValue = bridge.emitAnd(lhs, rhs, "andtmp");
      break;
    case TokenType::OR:
      currentValue = bridge.emitOr(lhs, rhs, "ortmp");
      break;
    case TokenType::PERCENT:
      currentValue = bridge.emitSRem(lhs, rhs, "modtmp");
      break;
    default:
      bridge.error("Unknown binary operator", node.loc);
      currentValue = nullptr;
      break;
    }
  }

  void visit(const AST::CallExpr &node) override {
    std::string calleeName = (node.callee == "start") ? "main" : node.callee;
    llvm::Function *fn = bridge.getModule().getFunction(calleeName);

    if (!fn)
      return;

    std::vector<llvm::Value *> args;
    for (auto &arg : node.args) {
      arg->accept(*this);
      args.push_back(currentValue);
    }
    currentValue = bridge.emitCall(fn, args, "calltmp");
  }

  void visit(const AST::AssignExpr &node) override {
    node.value->accept(*this);
    llvm::Value *val = currentValue;
    if (!val)
      return;

    auto varOpt = bridge.lookupVar(node.name, node.loc);
    if (!varOpt)
      return;

    auto var = *varOpt;
    bridge.emitStore(val, var.ptr);
  }

  void visit(const AST::IndexExpr &node) override {
    auto *ident = dynamic_cast<const AST::Identifier *>(node.array.get());
    if (!ident) {
      bridge.error("Can only index named arrays", node.loc);
      currentValue = nullptr;
      return;
    }

    ArrayVar *arr = arrays.lookup(ident->name);
    if (!arr) {
      bridge.error("Unknown array '" + ident->name + "'", node.loc);
      currentValue = nullptr;
      return;
    }

    node.index->accept(*this);
    llvm::Value *idx = currentValue;

    llvm::ArrayType *arrType = llvm::ArrayType::get(arr->elemType, arr->size);
    llvm::Value *zero = bridge.emitInt(0, 32);
    llvm::Value *gep = bridge.getBuilder().CreateGEP(
        arrType, arr->ptr, {zero, idx}, ident->name + "_idx");

    currentValue = bridge.emitLoad(arr->elemType, gep, ident->name + "_elem");
  }

  void visit(const AST::IfStmt &node) override {
    node.condition->accept(*this);
    llvm::Value *condVal = currentValue;
    if (!condVal)
      return;

    // If it's already i1 (result of a comparison), use it directly
    llvm::Value *condI1;
    if (condVal->getType()->isIntegerTy(1)) {
      condI1 = condVal;
    } else {
      llvm::Value *zero = bridge.emitInt(0, 32);
      condI1 = bridge.emitICmpNE(condVal, zero, "ifcond");
    }

    llvm::Function *fn = bridge.getCurrentFunction();
    llvm::BasicBlock *thenBB =
        llvm::BasicBlock::Create(bridge.getContext(), "then", fn);
    llvm::BasicBlock *elseBB =
        llvm::BasicBlock::Create(bridge.getContext(), "else");
    llvm::BasicBlock *mergeBB =
        llvm::BasicBlock::Create(bridge.getContext(), "ifcont");

    // Branch to either Then or Else
    bridge.emitCondBranch(condI1, thenBB, node.elseBranch ? elseBB : mergeBB);

    bridge.setInsertBlock(thenBB);
    node.thenBranch->accept(*this);
    // Note: If the block already hit a 'return', we don't emit the merge
    // branch
    if (!bridge.getBuilder().GetInsertBlock()->getTerminator()) {
      bridge.emitBranch(mergeBB);
    }

    if (node.elseBranch) {
      fn->insert(fn->end(), elseBB); // Add else block to function
      bridge.setInsertBlock(elseBB);
      node.elseBranch->accept(*this);

      if (!bridge.getBuilder().GetInsertBlock()->getTerminator()) {
        bridge.emitBranch(mergeBB);
      }
    }

    fn->insert(fn->end(), mergeBB);
    bridge.setInsertBlock(mergeBB);
  }

  void visit(const AST::WhileStmt &node) override {
    llvm::Function *fn = bridge.getCurrentFunction();

    llvm::BasicBlock *condBB =
        llvm::BasicBlock::Create(bridge.getContext(), "loop.cond", fn);
    llvm::BasicBlock *bodyBB =
        llvm::BasicBlock::Create(bridge.getContext(), "loop.body");
    llvm::BasicBlock *endBB =
        llvm::BasicBlock::Create(bridge.getContext(), "loop.end");

    // jump to condition
    bridge.emitBranch(condBB);

    // CONDITION
    bridge.setInsertBlock(condBB);
    node.condition->accept(*this);
    llvm::Value *condVal = currentValue;

    llvm::Value *condI1;
    if (condVal->getType()->isIntegerTy(1)) {
      condI1 = condVal;
    } else {
      llvm::Value *zero = bridge.emitInt(0, 32);
      condI1 = bridge.emitICmpNE(condVal, zero, "whilecond");
    }

    bridge.emitCondBranch(condI1, bodyBB, endBB);

    // BODY
    fn->insert(fn->end(), bodyBB);
    bridge.setInsertBlock(bodyBB);

    node.whileBranch->accept(*this);

    if (!bridge.getBuilder().GetInsertBlock()->getTerminator()) {
      bridge.emitBranch(condBB); // back-edge
    }

    // END
    fn->insert(fn->end(), endBB);
    bridge.setInsertBlock(endBB);
  }

  void visit(const AST::ForStmt &node) override {
    llvm::Function *fn = bridge.getCurrentFunction();

    // Init (runs once, before the loop)
    arrays.pushScope();
    node.init->accept(*this);

    llvm::BasicBlock *condBB =
        llvm::BasicBlock::Create(bridge.getContext(), "for.cond", fn);
    llvm::BasicBlock *bodyBB =
        llvm::BasicBlock::Create(bridge.getContext(), "for.body");
    llvm::BasicBlock *incrBB =
        llvm::BasicBlock::Create(bridge.getContext(), "for.incr");
    llvm::BasicBlock *endBB =
        llvm::BasicBlock::Create(bridge.getContext(), "for.end");

    bridge.emitBranch(condBB);

    // CONDITION
    bridge.setInsertBlock(condBB);
    node.condition->accept(*this);
    llvm::Value *condVal = currentValue;
    llvm::Value *condI1;
    if (condVal->getType()->isIntegerTy(1)) {
      condI1 = condVal;
    } else {
      condI1 = bridge.emitICmpNE(condVal, bridge.emitInt(0, 32), "forcond");
    }
    bridge.emitCondBranch(condI1, bodyBB, endBB);

    // BODY
    fn->insert(fn->end(), bodyBB);
    bridge.setInsertBlock(bodyBB);
    node.body->accept(*this);
    if (!bridge.getBuilder().GetInsertBlock()->getTerminator())
      bridge.emitBranch(incrBB);

    // INCREMENT
    fn->insert(fn->end(), incrBB);
    bridge.setInsertBlock(incrBB);
    node.increment->accept(*this);
    bridge.emitBranch(condBB); // back-edge to condition

    // END
    fn->insert(fn->end(), endBB);
    bridge.setInsertBlock(endBB);

    arrays.popScope();
  }

  void visit(const AST::ReturnStmt &node) override {
    node.value->accept(*this);
    if (currentValue) {
      bridge.emitReturn(currentValue);
    }
  }

  void visit(const AST::ExprStmt &node) override { node.expr->accept(*this); }

  void visit(const AST::BlockStmt &node) override {
    arrays.pushScope();
    for (const auto &stmt : node.statements)
      stmt->accept(*this);
    arrays.popScope();
  }

  void visit(const AST::VarDecl &node) override {
    if (node.isArray()) {
      llvm::Type *elemType = toLLVMType(node.type);
      size_t size = *node.arraySize;
      llvm::ArrayType *arrType = llvm::ArrayType::get(elemType, size);
      llvm::Value *alloca = bridge.emitAlloca(arrType, node.name);

      arrays.define(node.name, {alloca, elemType, size});

      // Initializer must be an ArrayLiteral — accept() fills lastArrayElements
      node.initializer->accept(*this);
      for (size_t i = 0; i < lastArrayElements.size() && i < size; i++) {
        llvm::Value *zero = bridge.emitInt(0, 32);
        llvm::Value *idx = bridge.emitInt(static_cast<int>(i), 32);
        llvm::Value *gep = bridge.getBuilder().CreateGEP(
            arrType, alloca, {zero, idx},
            node.name + "_init_" + std::to_string(i));
        bridge.emitStore(lastArrayElements[i], gep);
      }
    } else {
      node.initializer->accept(*this);
      llvm::Value *initVal = currentValue;
      if (!initVal)
        return;

      llvm::Type *varType = toLLVMType(node.type);
      llvm::Value *alloca = bridge.emitAlloca(varType, node.name);
      bridge.emitStore(initVal, alloca);
      bridge.defineVar(node.name, {alloca, varType}, node.loc);
    }
  }

  void visit(const AST::FunctionDecl &node) override {
    llvm::Type *retType = toLLVMType(node.returnType);

    std::vector<llvm::Type *> argTypes;
    for (auto &p : node.params) {
      argTypes.push_back(toLLVMType(p.second));
    }

    std::string fnName = (node.name == "start") ? "main" : node.name;
    llvm::Function *fn = bridge.beginFunction(fnName, retType, argTypes);

    unsigned idx = 0;
    for (auto &arg : fn->args()) {
      std::string argName = node.params[idx].first;
      arg.setName(argName);

      llvm::Type *paramType = toLLVMType(node.params[idx].second);
      llvm::Value *alloca = bridge.emitAlloca(paramType, argName);
      bridge.emitStore(&arg, alloca);
      bridge.defineVar(argName, {alloca, paramType}, node.loc);
      idx++;
    }

    node.body->accept(*this);

    bridge.endFunction();
  }

private:
  // Helper function
  llvm::Type *toLLVMType(TokenType type) {
    switch (type) {
    case TokenType::I32:
      return llvm::Type::getInt32Ty(bridge.getContext());
    case TokenType::BOOL:
      return llvm::Type::getInt1Ty(bridge.getContext());
    default:
      throw std::runtime_error("Unknown type");
    }
  }
};
