#pragma once

#include <ast.hpp>
#include <elpc/elpc.hpp>
#include <elpc/ir/llvmBridge.hpp>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
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

  std::unordered_map<std::string,
                     std::pair<llvm::StructType *, std::vector<std::string>>>
      structTypes;
  std::unordered_map<std::string, std::string> varToStructType;
  std::unordered_map<std::string, llvm::Value *> structVarAllocas;

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
    case TokenType::F32:
      return llvm::Type::getFloatTy(ctx());
    case TokenType::F64:
      return llvm::Type::getDoubleTy(ctx());
    case TokenType::BOOL:
      return llvm::Type::getInt1Ty(ctx());
    case TokenType::STR:
    case TokenType::STRING:
      // Dynamic Buffer: { ptr, i64, i64 }
      return llvm::StructType::get(ctx(), {llvm::PointerType::getUnqual(ctx()),
                                           llvm::Type::getInt64Ty(ctx()),
                                           llvm::Type::getInt64Ty(ctx())});
    default:
      throw std::runtime_error("Unknown type");
    }
  }

  llvm::Type *toLLVMType(const TypeInfo &type) {
    return std::visit(
        overloaded{
            [&](const PrimitiveType &p) -> llvm::Type * {
              return toLLVMType(p.tag);
            },
            [&](const ArrayType &a) -> llvm::Type * {
              return llvm::ArrayType::get(toLLVMType(*a.base), a.size);
            },
            [&](const VectorType &v) -> llvm::Type * {
              return llvm::FixedVectorType::get(toLLVMType(*v.base), v.size);
            },
            [&](const StructType &s) -> llvm::Type * {
              auto it = structTypes.find(s.name);
              if (it == structTypes.end())
                throw std::runtime_error("Unknown struct type: " + s.name);
              return it->second.first;
            }},
        type.data);
  }

  llvm::Value *castIfNeeded(llvm::Value *val, llvm::Type *destType) {
    if (!val)
      return nullptr;
    llvm::Type *srcType = val->getType();
    if (srcType == destType)
      return val;

    llvm::Type *srcScalar = srcType->getScalarType();
    llvm::Type *destScalar = destType->getScalarType();

    bool srcIsFloat = srcScalar->isFloatingPointTy();
    bool destIsFloat = destScalar->isFloatingPointTy();

    // Float <-> Float
    if (srcIsFloat && destIsFloat) {
      if (srcType->isFloatTy() && destType->isDoubleTy())
        return b().CreateFPExt(val, destType, "fpext");
      if (srcType->isDoubleTy() && destType->isFloatTy())
        return b().CreateFPTrunc(val, destType, "fptrunc");
    }
    // Int -> Float
    else if (!srcIsFloat && destIsFloat) {
      bool srcIsUnsigned = srcType->isIntegerTy(8) || srcType->isIntegerTy(16);
      return srcIsUnsigned ? b().CreateUIToFP(val, destType, "uitofp")
                           : b().CreateSIToFP(val, destType, "sitofp");
    }
    // Float -> Int
    else if (srcIsFloat && !destIsFloat) {
      bool destIsUnsigned =
          destType->isIntegerTy(8) || destType->isIntegerTy(16);
      return destIsUnsigned ? b().CreateFPToUI(val, destType, "fptoui")
                            : b().CreateFPToSI(val, destType, "fptosi");
    }

    // Unsigned widening
    if (srcType->isIntegerTy(8) && destType->isIntegerTy(16))
      return b().CreateZExt(val, destType, "widen_u8_to_u16");

    if (srcType->isIntegerTy(8) && destType->isIntegerTy(32))
      return b().CreateZExt(val, destType, "widen_u8_to_i32");
    if (srcType->isIntegerTy(16) && destType->isIntegerTy(32))
      return b().CreateZExt(val, destType, "widen_u16_to_i32");

    if (srcType->isIntegerTy(8) && destType->isIntegerTy(64))
      return b().CreateZExt(val, destType, "widen_u8_to_i64");
    if (srcType->isIntegerTy(16) && destType->isIntegerTy(64))
      return b().CreateZExt(val, destType, "widen_u16_to_i64");
    if (srcType->isIntegerTy(32) && destType->isIntegerTy(64))
      return b().CreateSExt(val, destType, "widen_i32_to_i64");

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

  void visit(const AST::FloatLiteral &node) override {
    // Generate as f32 by default
    currentValue =
        llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx()), node.value);
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

  void visit(const AST::StructLiteral &node) override {
    auto it = structTypes.find(node.typeName);
    if (it == structTypes.end()) {
      bridge.error("Unknown struct type '" + node.typeName + "'", node.loc);
      currentValue = nullptr;
      return;
    }
    auto *st = it->second.first;
    const auto &fieldNames = it->second.second;

    // Build a map of field name → value so order in literal doesn't matter
    std::unordered_map<std::string, llvm::Value *> fieldVals;
    for (auto &[name, expr] : node.fields) {
      expr->accept(*this);
      fieldVals[name] = currentValue;
    }

    llvm::Value *agg = llvm::UndefValue::get(st);
    for (unsigned i = 0; i < fieldNames.size(); i++) {
      auto valIt = fieldVals.find(fieldNames[i]);
      if (valIt == fieldVals.end() || !valIt->second)
        continue;
      llvm::Type *expectedType = st->getElementType(i);
      llvm::Value *val = castIfNeeded(valIt->second, expectedType);
      agg = b().CreateInsertValue(agg, val, i);
    }
    currentValue = agg;
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
    auto typeIsUnsigned = [](llvm::Type *t) -> bool {
      return t->isIntegerTy(8) || t->isIntegerTy(16);
    };

    auto widenToCommon = [&](llvm::Value *&l, llvm::Value *&r) {
      llvm::Type *lt = l->getType(), *rt = r->getType();
      if (lt == rt)
        return;

      bool lFloat = lt->isFloatingPointTy();
      bool rFloat = rt->isFloatingPointTy();

      if (lFloat && !rFloat)
        r = castIfNeeded(r, lt);
      else if (!lFloat && rFloat)
        l = castIfNeeded(l, rt);
      else if (lFloat && rFloat) {
        if (lt->isFloatTy())
          l = b().CreateFPExt(l, rt, "widen");
        else
          r = b().CreateFPExt(r, lt, "widen");
      }

      if (lt->isIntegerTy() && rt->isIntegerTy()) {
        if (lt->getIntegerBitWidth() < rt->getIntegerBitWidth())
          l = typeIsUnsigned(lt) ? b().CreateZExt(l, rt, "widen")
                                 : b().CreateSExt(l, rt, "widen");
        else
          r = typeIsUnsigned(rt) ? b().CreateZExt(r, lt, "widen")
                                 : b().CreateSExt(r, lt, "widen");
      }
    };
    widenToCommon(lhs, rhs);
    bool isFloat = lhs->getType()->getScalarType()->isFloatingPointTy();
    bool isUnsigned = typeIsUnsigned(lhs->getType()->getScalarType());

    switch (node.op) {
    case TokenType::PLUS:
      currentValue = isFloat ? b().CreateFAdd(lhs, rhs, "fadd")
                             : b().CreateAdd(lhs, rhs, "add");
      break;
    case TokenType::MINUS:
      currentValue = isFloat ? b().CreateFSub(lhs, rhs, "fsub")
                             : b().CreateSub(lhs, rhs, "sub");
      break;
    case TokenType::STAR:
      currentValue = isFloat ? b().CreateFMul(lhs, rhs, "fmul")
                             : b().CreateMul(lhs, rhs, "mul");
      break;
    case TokenType::PERCENT:
      currentValue = isFloat ? b().CreateFRem(lhs, rhs, "fmod")
                             : (isUnsigned ? b().CreateURem(lhs, rhs, "mod")
                                           : b().CreateSRem(lhs, rhs, "mod"));
      break;
    case TokenType::SLASH:
      if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(rhs); c && c->isZero()) {
        bridge.error("Division by zero", node.loc);
        currentValue = constI32(0);
      } else {
        if (isFloat) {
          currentValue = b().CreateFDiv(lhs, rhs, "fdiv");
        } else {
          currentValue = isUnsigned ? b().CreateUDiv(lhs, rhs, "div")
                                    : b().CreateSDiv(lhs, rhs, "div");
        }
      }
      break;
    case TokenType::LESS:
      currentValue = isFloat
                         ? b().CreateFCmpOLT(lhs, rhs, "cmp")
                         : (isUnsigned ? b().CreateICmpULT(lhs, rhs, "cmp")
                                       : b().CreateICmpSLT(lhs, rhs, "cmp"));
      break;
    case TokenType::GREATER:
      currentValue = isFloat
                         ? b().CreateFCmpOGT(lhs, rhs, "cmp")
                         : (isUnsigned ? b().CreateICmpUGT(lhs, rhs, "cmp")
                                       : b().CreateICmpSGT(lhs, rhs, "cmp"));
      break;
    case TokenType::LESS_EQ:
      currentValue = isFloat
                         ? b().CreateFCmpOLE(lhs, rhs, "cmp")
                         : (isUnsigned ? b().CreateICmpULE(lhs, rhs, "cmp")
                                       : b().CreateICmpSLE(lhs, rhs, "cmp"));
      break;
    case TokenType::GREATER_EQ:
      currentValue = isFloat
                         ? b().CreateFCmpOGE(lhs, rhs, "cmp")
                         : (isUnsigned ? b().CreateICmpUGE(lhs, rhs, "cmp")
                                       : b().CreateICmpSGE(lhs, rhs, "cmp"));
      break;
    case TokenType::EQ_EQ:
      currentValue = isFloat ? b().CreateFCmpOEQ(lhs, rhs, "cmp")
                             : b().CreateICmpEQ(lhs, rhs, "cmp");
      break;
    case TokenType::NOT_EQ:
      currentValue = isFloat ? b().CreateFCmpONE(lhs, rhs, "cmp")
                             : b().CreateICmpNE(lhs, rhs, "cmp");
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

      // Extract raw C pointers for extern functions BEFORE checking parameters
      if (fn->isDeclaration() && argVal->getType()->isStructTy()) {
        argVal = b().CreateExtractValue(argVal, 0, "raw_c_str");
      }

      // If the argument corresponds to a defined parameter, cast it
      if (i < fnType->getNumParams()) {
        llvm::Type *expectedType = fnType->getParamType(i);
        argVal = castIfNeeded(argVal, expectedType);
      } else {
        if (argVal->getType()->isIntegerTy(8) ||
            argVal->getType()->isIntegerTy(16)) {
          // C vararg promotion (u8/u16 -> i32)
          argVal = b().CreateZExt(argVal, llvm::Type::getInt32Ty(ctx()),
                                  "vararg_prom");
        } else if (argVal->getType()->isFloatTy()) {
          argVal = b().CreateFPExt(argVal, llvm::Type::getDoubleTy(ctx()),
                                   "vararg_fp_prom");
        }
      }

      args.push_back(argVal);
    }
    currentValue = b().CreateCall(fn, args, "call");
  }

  void visit(const AST::AssignExpr &node) override {
    node.value->accept(*this);
    auto varOpt = bridge.lookupVar(node.name);
    if (!varOpt)
      return;

    llvm::Value *storeVal = currentValue;

    if (!storeVal && varOpt->type->isVectorTy()) {
      storeVal = llvm::UndefValue::get(varOpt->type);
      for (size_t i = 0; i < lastArrayElements.size(); i++) {
        storeVal = b().CreateInsertElement(storeVal, lastArrayElements[i],
                                           constI32(i), "vec_assign");
      }
    }

    if (storeVal) {
      storeVal = castIfNeeded(storeVal, varOpt->type);
      b().CreateStore(storeVal, varOpt->ptr);
    }
  }

  void visit(const AST::IndexExpr &node) override {
    auto *ident = dynamic_cast<const AST::Identifier *>(node.array.get());
    if (ident) {
      auto varOpt = bridge.lookupVar(ident->name);
      if (varOpt && varOpt->type->isVectorTy()) {
        node.index->accept(*this);
        llvm::Value *idx = currentValue;
        llvm::Value *vec =
            b().CreateLoad(varOpt->type, varOpt->ptr, ident->name + "_val");
        currentValue =
            b().CreateExtractElement(vec, idx, ident->name + "_elem");
        return;
      }
    } else {
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
    // instruction
    currentValue = castIfNeeded(currentValue, destType);
  }

  void visit(const AST::MemberAccessExpr &node) override {
    auto *ident = dynamic_cast<const AST::Identifier *>(node.object.get());
    if (!ident) {
      bridge.error("Expected variable on left of '.'", node.loc);
      currentValue = nullptr;
      return;
    }
    auto structIt = varToStructType.find(ident->name);
    if (structIt == varToStructType.end()) {
      bridge.error("'" + ident->name + "' is not a struct", node.loc);
      currentValue = nullptr;
      return;
    }
    auto typeIt = structTypes.find(structIt->second);
    if (typeIt == structTypes.end())
      return;

    auto *st = typeIt->second.first;
    const auto &fieldNames = typeIt->second.second;
    auto fieldIt = std::find(fieldNames.begin(), fieldNames.end(), node.field);
    if (fieldIt == fieldNames.end()) {
      bridge.error("No field '" + node.field + "'", node.loc);
      currentValue = nullptr;
      return;
    }
    unsigned idx = static_cast<unsigned>(fieldIt - fieldNames.begin());

    auto allocaIt = structVarAllocas.find(ident->name);
    if (allocaIt == structVarAllocas.end()) {
      bridge.error("Struct variable '" + ident->name + "' has no alloca",
                   node.loc);
      currentValue = nullptr;
      return;
    }

    llvm::Value *gep = b().CreateStructGEP(st, allocaIt->second, idx,
                                           ident->name + "." + node.field);
    currentValue =
        b().CreateLoad(st->getElementType(idx), gep, node.field + "_val");
  }

  void visit(const AST::MemberAssignExpr &node) override {
    auto structIt = varToStructType.find(node.objectName);
    if (structIt == varToStructType.end()) {
      bridge.error("'" + node.objectName + "' is not a struct", node.loc);
      return;
    }
    auto typeIt = structTypes.find(structIt->second);
    if (typeIt == structTypes.end())
      return;

    auto *st = typeIt->second.first;
    const auto &fieldNames = typeIt->second.second;
    auto fieldIt = std::find(fieldNames.begin(), fieldNames.end(), node.field);
    if (fieldIt == fieldNames.end()) {
      bridge.error("No field '" + node.field + "'", node.loc);
      return;
    }
    unsigned idx = static_cast<unsigned>(fieldIt - fieldNames.begin());

    node.value->accept(*this);
    if (!currentValue)
      return;

    auto allocaIt = structVarAllocas.find(node.objectName);
    if (allocaIt == structVarAllocas.end()) {
      bridge.error("Struct variable '" + node.objectName + "' has no alloca",
                   node.loc);
      return;
    }

    llvm::Value *gep = b().CreateStructGEP(st, allocaIt->second, idx,
                                           node.objectName + "." + node.field);
    llvm::Type *fieldType = st->getElementType(idx);
    currentValue = castIfNeeded(currentValue, fieldType);
    b().CreateStore(currentValue, gep);
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
    if (node.type.isStruct()) {
      const std::string &structName = std::get<StructType>(node.type.data).name;
      auto typeIt = structTypes.find(structName);
      if (typeIt == structTypes.end())
        return;

      auto *st = typeIt->second.first;
      node.initializer->accept(*this);
      if (!currentValue)
        return;

      llvm::Value *alloca = bridge.emitAlloca(st, node.name);
      b().CreateStore(currentValue, alloca);
      bridge.defineVar(node.name, {alloca, st}, node.loc);
      varToStructType[node.name] = structName;
      structVarAllocas[node.name] = alloca;
      return;
    }

    if (node.type.isVector()) {
      llvm::Type *vecLLVMType = toLLVMType(node.type);
      llvm::Value *vecVal = llvm::UndefValue::get(vecLLVMType);

      node.initializer->accept(*this);

      // If it was initialized by another vector (v2 = v1)
      if (currentValue && currentValue->getType()->isVectorTy()) {
        vecVal = currentValue;
      } else {
        // It was initialized by an array literal[1.0, 2.0, 3.0]
        for (size_t i = 0; i < lastArrayElements.size(); i++) {
          vecVal = b().CreateInsertElement(vecVal, lastArrayElements[i],
                                           constI32(i), "vec_init");
        }
      }

      llvm::Value *alloca = bridge.emitAlloca(vecLLVMType, node.name);
      b().CreateStore(vecVal, alloca);
      bridge.defineVar(node.name, {alloca, vecLLVMType}, node.loc);
      return;
    }

    if (node.type.isArray()) {
      const auto &arrType = std::get<ArrayType>(node.type.data);
      llvm::Type *elemType = toLLVMType(*arrType.base);
      size_t size = arrType.size;
      llvm::ArrayType *llvmArrType = llvm::ArrayType::get(elemType, size);
      llvm::Value *alloca = bridge.emitAlloca(llvmArrType, node.name);

      arrays.define(node.name, {alloca, elemType, size});
      node.initializer->accept(*this);

      for (size_t i = 0; i < lastArrayElements.size() && i < size; i++) {
        llvm::Value *gep = b().CreateGEP(
            llvmArrType, alloca, {constI32(0), constI32(static_cast<int>(i))},
            node.name + "_init_" + std::to_string(i));
        b().CreateStore(lastArrayElements[i], gep);
      }
      return;
    }

    node.initializer->accept(*this);
    if (!currentValue)
      return;

    llvm::Type *varType = toLLVMType(node.type);
    currentValue = castIfNeeded(currentValue, varType);
    llvm::Value *alloca = bridge.emitAlloca(varType, node.name);
    b().CreateStore(currentValue, alloca);
    bridge.defineVar(node.name, {alloca, varType}, node.loc);
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
    for (const auto &t : node.paramTypes) {
      if (t.isPrimitive()) {
        TokenType tag = std::get<PrimitiveType>(t.data).tag;
        if (tag == TokenType::STR || tag == TokenType::STRING) {
          argTypes.push_back(llvm::PointerType::getUnqual(ctx()));
          continue;
        }
      }
      argTypes.push_back(toLLVMType(t));
    }

    llvm::FunctionType *fnType =
        llvm::FunctionType::get(retType, argTypes, node.isVariadic);

    // Create the function with ExternalLinkage
    llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, node.name,
                           bridge.getModule());
  }

  void visit(const AST::StructDecl &node) override {
    std::vector<llvm::Type *> fieldTypes;
    std::vector<std::string> fieldNames;
    for (auto &[name, type] : node.fields) {
      fieldTypes.push_back(toLLVMType(type));
      fieldNames.push_back(name);
    }
    auto *st = llvm::StructType::create(ctx(), fieldTypes, node.name);
    structTypes[node.name] = {st, std::move(fieldNames)};
  }
};
