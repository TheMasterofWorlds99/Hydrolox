#pragma once

#include <ast.hpp>
#include <elpc/elpc.hpp>
#include <elpc/ir/llvmBridge.hpp>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
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
  llvm::Value *currentValue = nullptr;
  ArrayTable arrays;
  std::vector<llvm::Value *> lastArrayElements;

  auto &b() { return bridge.getBuilder(); }

  llvm::LLVMContext &ctx() { return bridge.getContext(); }

  llvm::Value *constI32(int v) {
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx()), v, true);
  }

  llvm::Value *condToI1(llvm::Value *val) {
    if (val->getType()->isIntegerTy(1))
      return val;
    return b().CreateICmpNE(val, constI32(0), "cond");
  }

  llvm::Type *toLLVMType(TokenType type) {
    switch (type) {
    case TokenType::U8:
      return llvm::Type::getInt8Ty(ctx());
    case TokenType::U16:
      return llvm::Type::getInt16Ty(ctx());
    case TokenType::I32:
      return llvm::Type::getInt32Ty(ctx());
    case TokenType::I64:
      return llvm::Type::getInt64Ty(ctx());
    case TokenType::BOOL:
      return llvm::Type::getInt1Ty(ctx());
    case TokenType::STR:
      // Fat Pointer/Slice
      return llvm::StructType::get(ctx(), {llvm::PointerType::getUnqual(ctx()),
                                           llvm::Type::getInt64Ty(ctx())});
    case TokenType::STRING:
      // Dynamic Buffer: { ptr, i64, i64 }
      return llvm::StructType::get(ctx(), {llvm::PointerType::getUnqual(ctx()),
                                           llvm::Type::getInt64Ty(ctx()),
                                           llvm::Type::getInt64Ty(ctx())});
    default:
      throw std::runtime_error("Unknown type");
    }
  }

  llvm::Value *castIfNeeded(llvm::Value *val, llvm::Type *destType) {
    if (!val)
      return nullptr;
    llvm::Type *srcType = val->getType();
    if (srcType == destType)
      return val;

    // Unsigned widening
    if (srcType->isIntegerTy(8) && destType->isIntegerTy(16))
      return b().CreateZExt(val, destType, "widen_u8_to_u16");
    if (srcType->isIntegerTy(8) && destType->isIntegerTy(32))
      return b().CreateZExt(val, destType, "widen_u8_to_i32");
    if (srcType->isIntegerTy(16) && destType->isIntegerTy(32))
      return b().CreateZExt(val, destType, "widen_u16_to_i32");
    if (srcType->isIntegerTy(8) && destType->isIntegerTy(64))
      return b().CreateZExt(val, destType);
    if (srcType->isIntegerTy(16) && destType->isIntegerTy(64))
      return b().CreateZExt(val, destType);
    if (srcType->isIntegerTy(32) && destType->isIntegerTy(64))
      return b().CreateZExt(val, destType);

    // Narrowing (explicit cast)
    if (srcType->isIntegerTy(32) && destType->isIntegerTy(16))
      return b().CreateTrunc(val, destType, "narrow_i32_to_u16");
    if (srcType->isIntegerTy(32) && destType->isIntegerTy(8))
      return b().CreateTrunc(val, destType, "narrow_i32_to_u8");
    if (srcType->isIntegerTy(16) && destType->isIntegerTy(8))
      return b().CreateTrunc(val, destType, "narrow_u16_to_u8");

    return val;
  }

public:
  LLVMGenerator(elpc::LLVMBridge &bridge) : bridge(bridge) {}

  void visit(const AST::IntLiteral &node) override {
    currentValue =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx()), node.value, true);
  }

  void visit(const AST::BoolLiteral &node) override {
    currentValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx()),
                                          node.value ? 1 : 0);
  }

  void visit(const AST::StringLiteral &node) override {
    // Create the raw C-style string in memory
    llvm::Value *rawStr = b().CreateGlobalString(node.value, "str_raw");

    // Get the length of the string as an i64
    llvm::Value *strLen = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx()),
                                                 node.value.length(), false);

    // Create the fat pointer struct
    llvm::Type *strTy = toLLVMType(TokenType::STR);
    llvm::Value *fatPtr = llvm::UndefValue::get(strTy);

    // Insrert the pointer and length into said struct
    fatPtr = b().CreateInsertValue(fatPtr, rawStr, 0); // Index 0 is the pointer
    fatPtr = b().CreateInsertValue(fatPtr, strLen, 1); // Index 1 is the length

    currentValue = fatPtr;
  }

  void visit(const AST::ArrayLiteral &node) override {
    lastArrayElements.clear();
    for (auto &elem : node.elements) {
      elem->accept(*this);
      lastArrayElements.push_back(currentValue);
    }
    currentValue = nullptr;
  }

  void visit(const AST::Identifier &node) override {
    auto varOpt = bridge.lookupVar(node.name);
    if (!varOpt) {
      bridge.error("Variable not found in scope", node.loc);
      currentValue = nullptr;
      return;
    }
    currentValue =
        b().CreateLoad(varOpt->type, varOpt->ptr, node.name + "_val");
  }

  void visit(const AST::BinaryExpr &node) override {
    node.left->accept(*this);
    llvm::Value *lhs = currentValue;
    node.right->accept(*this);
    llvm::Value *rhs = currentValue;
    if (!lhs || !rhs)
      return;

    // IMPLICIT WIDENING BEFORE MATH
    auto widenToCommon = [&](llvm::Value *&l, llvm::Value *&r) {
      llvm::Type *lt = l->getType(), *rt = r->getType();
      if (lt == rt)
        return;
      // Always widen the narrower side to the wider type
      if (lt->isIntegerTy() && rt->isIntegerTy()) {
        if (lt->getIntegerBitWidth() < rt->getIntegerBitWidth())
          l = b().CreateZExt(l, rt, "widen");
        else
          r = b().CreateZExt(r, lt, "widen");
      }
    };
    widenToCommon(lhs, rhs);
    bool isUnsigned =
        lhs->getType()->isIntegerTy(8) || lhs->getType()->isIntegerTy(16);

    switch (node.op) {
    case TokenType::PLUS:
      currentValue = b().CreateAdd(lhs, rhs, "add");
      break;
    case TokenType::MINUS:
      currentValue = b().CreateSub(lhs, rhs, "sub");
      break;
    case TokenType::STAR:
      currentValue = b().CreateMul(lhs, rhs, "mul");
      break;
    case TokenType::PERCENT:
      currentValue = isUnsigned ? b().CreateURem(lhs, rhs, "mod")
                                : b().CreateSRem(lhs, rhs, "mod");
      break;
    case TokenType::SLASH:
      if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(rhs); c && c->isZero()) {
        bridge.error("Division by zero", node.loc);
        currentValue = constI32(0);
      } else {
        currentValue = isUnsigned ? b().CreateUDiv(lhs, rhs, "div")
                                  : b().CreateSDiv(lhs, rhs, "div");
      }
      break;
    case TokenType::LESS:
      currentValue = isUnsigned ? b().CreateICmpULT(lhs, rhs, "cmp")
                                : b().CreateICmpSLT(lhs, rhs, "cmp");
      break;
    case TokenType::GREATER:
      currentValue = isUnsigned ? b().CreateICmpUGT(lhs, rhs, "cmp")
                                : b().CreateICmpSGT(lhs, rhs, "cmp");
      break;
    case TokenType::LESS_EQ:
      currentValue = isUnsigned ? b().CreateICmpULE(lhs, rhs, "cmp")
                                : b().CreateICmpSLE(lhs, rhs, "cmp");
      break;
    case TokenType::GREATER_EQ:
      currentValue = isUnsigned ? b().CreateICmpUGE(lhs, rhs, "cmp")
                                : b().CreateICmpSGE(lhs, rhs, "cmp");
      break;
    case TokenType::EQ_EQ:
      currentValue = b().CreateICmpEQ(lhs, rhs, "cmp");
      break;
    case TokenType::NOT_EQ:
      currentValue = b().CreateICmpNE(lhs, rhs, "cmp");
      break;
    case TokenType::AND:
      currentValue = b().CreateAnd(lhs, rhs, "and");
      break;
    case TokenType::OR:
      currentValue = b().CreateOr(lhs, rhs, "or");
      break;
    default:
      bridge.error("Unknown binary operator", node.loc);
      currentValue = nullptr;
      break;
    }
  }

  void visit(const AST::CallExpr &node) override {
    std::string name = (node.callee == "start") ? "main" : node.callee;
    llvm::Function *fn = bridge.getModule().getFunction(name);
    if (!fn)
      return;

    std::vector<llvm::Value *> args;
    llvm::FunctionType *fnType = fn->getFunctionType();

    for (size_t i = 0; i < node.args.size(); ++i) {
      node.args[i]->accept(*this);
      if (!currentValue)
        continue;

      llvm::Value *argVal = currentValue;

      // If the argument corresponds to a defined parameter, cast it
      if (i < fnType->getNumParams()) {
        llvm::Type *expectedType = fnType->getParamType(i);
        argVal = castIfNeeded(argVal, expectedType);
      } else {
        // If this is an extern function (has no body in Hydrolox) and we are
        // passing a struct (str/string)
        if (fn->isDeclaration() && argVal->getType()->isStructTy()) {
          // Extract the raw pointer (Index 0) to pass to C
          argVal = b().CreateExtractValue(argVal, 0, "raw_c_str");
        } else if (argVal->getType()->isIntegerTy(8) ||
                   argVal->getType()->isIntegerTy(16)) {
          // C vararg promotion (u8/u16 -> i32)
          argVal = b().CreateZExt(argVal, llvm::Type::getInt32Ty(ctx()),
                                  "vararg_prom");
        }
      }

      args.push_back(argVal);
    }
    currentValue = b().CreateCall(fn, args, "call");
  }

  void visit(const AST::AssignExpr &node) override {
    node.value->accept(*this);
    auto varOpt = bridge.lookupVar(node.name);
    if (!varOpt || !currentValue)
      return;

    // Automagically fix the type!
    currentValue = castIfNeeded(currentValue, varOpt->type);
    b().CreateStore(currentValue, varOpt->ptr);
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
    llvm::ArrayType *arrType = llvm::ArrayType::get(arr->elemType, arr->size);
    llvm::Value *gep = b().CreateGEP(
        arrType, arr->ptr, {constI32(0), currentValue}, ident->name + "_idx");
    currentValue = b().CreateLoad(arr->elemType, gep, ident->name + "_elem");
  }

  void visit(const AST::CastExpr &node) override {
    node.expr->accept(*this);
    if (!currentValue)
      return;

    llvm::Type *destType = toLLVMType(node.targetType);

    // castIfNeeded automatically issues the correct CreateTrunc or CreateZExt
    // instruction!
    currentValue = castIfNeeded(currentValue, destType);
  }

  void visit(const AST::IfStmt &node) override {
    node.condition->accept(*this);
    if (!currentValue)
      return;

    llvm::Value *condI1 = condToI1(currentValue);
    llvm::Function *fn = bridge.getCurrentFunction();
    llvm::BasicBlock *thenBB = llvm::BasicBlock::Create(ctx(), "then", fn);
    llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(ctx(), "else");
    llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx(), "ifcont");

    b().CreateCondBr(condI1, thenBB, node.elseBranch ? elseBB : mergeBB);

    b().SetInsertPoint(thenBB);
    node.thenBranch->accept(*this);
    if (!b().GetInsertBlock()->getTerminator())
      b().CreateBr(mergeBB);

    if (node.elseBranch) {
      fn->insert(fn->end(), elseBB);
      b().SetInsertPoint(elseBB);
      node.elseBranch->accept(*this);
      if (!b().GetInsertBlock()->getTerminator())
        b().CreateBr(mergeBB);
    }

    fn->insert(fn->end(), mergeBB);
    b().SetInsertPoint(mergeBB);
  }

  void visit(const AST::WhileStmt &node) override {
    llvm::Function *fn = bridge.getCurrentFunction();
    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(ctx(), "loop.cond", fn);
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(ctx(), "loop.body");
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx(), "loop.end");

    b().CreateBr(condBB);

    b().SetInsertPoint(condBB);
    node.condition->accept(*this);
    b().CreateCondBr(condToI1(currentValue), bodyBB, endBB);

    fn->insert(fn->end(), bodyBB);
    b().SetInsertPoint(bodyBB);
    node.whileBranch->accept(*this);
    if (!b().GetInsertBlock()->getTerminator())
      b().CreateBr(condBB);

    fn->insert(fn->end(), endBB);
    b().SetInsertPoint(endBB);
  }

  void visit(const AST::ForStmt &node) override {
    llvm::Function *fn = bridge.getCurrentFunction();

    arrays.pushScope();
    node.init->accept(*this);

    llvm::BasicBlock *condBB = llvm::BasicBlock::Create(ctx(), "for.cond", fn);
    llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(ctx(), "for.body");
    llvm::BasicBlock *incrBB = llvm::BasicBlock::Create(ctx(), "for.incr");
    llvm::BasicBlock *endBB = llvm::BasicBlock::Create(ctx(), "for.end");

    b().CreateBr(condBB);

    b().SetInsertPoint(condBB);
    node.condition->accept(*this);
    b().CreateCondBr(condToI1(currentValue), bodyBB, endBB);

    fn->insert(fn->end(), bodyBB);
    b().SetInsertPoint(bodyBB);
    node.body->accept(*this);
    if (!b().GetInsertBlock()->getTerminator())
      b().CreateBr(incrBB);

    fn->insert(fn->end(), incrBB);
    b().SetInsertPoint(incrBB);
    node.increment->accept(*this);
    b().CreateBr(condBB);

    fn->insert(fn->end(), endBB);
    b().SetInsertPoint(endBB);
    arrays.popScope();
  }

  void visit(const AST::ReturnStmt &node) override {
    node.value->accept(*this);
    if (currentValue) {
      llvm::Type *retType = bridge.getCurrentFunction()->getReturnType();
      currentValue = castIfNeeded(currentValue, retType);
      b().CreateRet(currentValue);
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
      node.initializer->accept(*this);

      for (size_t i = 0; i < lastArrayElements.size() && i < size; i++) {
        llvm::Value *gep = b().CreateGEP(
            arrType, alloca, {constI32(0), constI32(static_cast<int>(i))},
            node.name + "_init_" + std::to_string(i));
        b().CreateStore(lastArrayElements[i], gep);
      }
    } else {
      node.initializer->accept(*this);
      if (!currentValue)
        return;

      llvm::Type *varType = toLLVMType(node.type);

      // Automagically fix the type!
      currentValue = castIfNeeded(currentValue, varType);

      llvm::Value *alloca = bridge.emitAlloca(varType, node.name);
      b().CreateStore(currentValue, alloca);
      bridge.defineVar(node.name, {alloca, varType}, node.loc);
    }
  }

  void visit(const AST::FunctionDecl &node) override {
    llvm::Type *retType = toLLVMType(node.returnType);
    std::vector<llvm::Type *> argTypes;
    for (auto &p : node.params)
      argTypes.push_back(toLLVMType(p.second));

    std::string fnName = (node.name == "start") ? "main" : node.name;
    llvm::Function *fn = bridge.beginFunction(fnName, retType, argTypes);

    unsigned idx = 0;
    for (auto &arg : fn->args()) {
      std::string argName = node.params[idx].first;
      llvm::Type *argType = toLLVMType(node.params[idx].second);
      arg.setName(argName);
      llvm::Value *alloca = bridge.emitAlloca(argType, argName);
      b().CreateStore(&arg, alloca);
      bridge.defineVar(argName, {alloca, argType}, node.loc);
      idx++;
    }

    node.body->accept(*this);
    bridge.endFunction();
  }

  void visit(const AST::ExternDecl &node) override {
    llvm::Type *retType = toLLVMType(node.returnType);
    std::vector<llvm::Type *> argTypes;
    for (auto t : node.paramTypes) {
      argTypes.push_back(toLLVMType(t));
    }

    llvm::FunctionType *fnType =
        llvm::FunctionType::get(retType, argTypes, node.isVariadic);

    // Create the function with ExternalLinkage, meaning "The Linker will find
    // this later!"
    llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, node.name,
                           bridge.getModule());
  }
};
