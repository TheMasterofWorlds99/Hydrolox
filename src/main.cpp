#include <astPrinter.hpp>
#include <elpc/elpc.hpp>
#include <fstream>
#include <generator.hpp>
#include <lexer.hpp>
#include <parser.hpp>
#include <sema.hpp>

#ifdef ELPC_ENABLE_LLVM
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#endif

int main(int argc, char *argv[]) {
  std::string path = (argc > 1) ? argv[1] : "test/main.hlox";

  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "[hydrolox] Error: could not open file '" << path << "'\n";
    return 1;
  }

  std::ostringstream buf;
  buf << file.rdbuf();
  std::string src = buf.str();

  try {
    auto tokens = GenerateLexer(src).tokenize();

    HydroloxParser parser(tokens);
    auto astRoots = parser.parse();

    elpc::DiagnosticEngine diag;

    SemanticAnalyzer sema(diag);
    for (const auto &root : astRoots) {
      root->accept(sema);
    }

    // Stop compilation if Sema found type errors or undeclared variables!
    if (diag.hasErrors()) {
      std::cerr << "\n=== SEMANTIC ERRORS ===\n";
      diag.reportDiagnostics(std::cerr);
      return 1;
    }

#ifdef ELPC_ENABLE_LLVM
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("hydrolox_core", ctx);

    elpc::LLVMBridge bridge(ctx, *mod, diag);
    LLVMGenerator codegen(bridge);

    for (const auto &root : astRoots) {
      root->accept(codegen);
    }

    // Secondary error check (e.g. LLVM caught divide-by-zero)
    if (diag.hasErrors()) {
      std::cerr << "\n=== CODEGEN ERRORS ===\n";
      diag.reportDiagnostics(std::cerr);
      return 1;
    }

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Build the standard -O2 optimization pipeline
    llvm::ModulePassManager MPM =
        PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

    // Optimize the module! This instantly converts all your allocas to pure
    // SSA!
    MPM.run(*mod, MAM);

    std::cout << "=== LLVM IR ===\n";
    bridge.dumpIR();
    bridge.writeObject("output.o");
#else
    std::cout << "\n[hydrolox] LLVM disabled.\n";
#endif

  } catch (const std::exception &e) {
    std::cerr << "\n[hydrolox] Syntax Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
