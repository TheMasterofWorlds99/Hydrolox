#pragma once

#include "ast.hpp"
#include <iostream>

class ASTPrinter : public AST::ASTVisitor {
  int indentLevel = 0;

  void printIndent() {
    for (int i = 0; i < indentLevel; ++i)
      std::cout << "  ";
  }

public:
  void visit(const AST::IntLiteral &node) override { std::cout << node.value; }

  void visit(const AST::Identifier &node) override { std::cout << node.name; }

  void visit(const AST::BinaryExpr &node) override {
    std::cout << "(";
    // Map TokenType to string representation
    std::string opStr;
    if (node.op == TokenType::PLUS)
      opStr = "+";
    else if (node.op == TokenType::MINUS)
      opStr = "-";
    else if (node.op == TokenType::STAR)
      opStr = "*";
    else if (node.op == TokenType::SLASH)
      opStr = "/";

    std::cout << " " << opStr << " ";
    node.left->accept(*this);
    std::cout << " ";
    node.right->accept(*this);
    std::cout << ")";
  }

  void visit(const AST::ReturnStmt &node) override {
    printIndent();
    std::cout << "ReturnStmt\n";
    indentLevel++;
    printIndent();
    node.value->accept(*this);
    std::cout << "\n";
    indentLevel--;
  }

  void visit(const AST::BlockStmt &node) override {
    printIndent();
    std::cout << "BlockStmt {\n";
    indentLevel++;
    for (const auto &stmt : node.statements) {
      stmt->accept(*this);
    }
    indentLevel--;
    printIndent();
    std::cout << "}\n";
  }

  void visit(const AST::FunctionDecl &node) override {
    printIndent();
    std::cout << "FunctionDecl '" << node.name << "' -> i32\n";
    node.body->accept(*this);
  }
};
