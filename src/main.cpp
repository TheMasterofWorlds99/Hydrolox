#include <elpc/elpc.hpp>
#include <fstream>
#include <generator.hpp>
#include <lexer.hpp>
#include <parser.hpp>
#include <sema.hpp>

#ifdef ELPC_ENABLE_LLVM
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#endif

bool compileToObject(const std::string &inputPath,
                     const std::string &outputPath, bool emitIR);

int main(int argc, char *argv[]) {
  std::string inputPath =
      ""; // Empty since the user needs to give this as the input file
  std::string outputPath =
      "a.out"; // Not empty, but rather has a default of a.out given that other
               // compilers, when not given a name for the .out file, defaults
               // to a.out
  bool emitIR = false;

  // For loop to go over all of the arguments that the user puts in for the
  // compiler
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      outputPath = argv[++i];
    } else if (arg == "--emit-ir") {
      emitIR = true;
    } else if (arg[0] != '-') {
      inputPath = arg;
    } else {
      std::cerr << "[hydrolox] error: unrecongnized command-line option '"
                << arg << "'\n";
      return 1;
    }
  }

  if (inputPath.empty()) {
    std::cerr << "[hydrolox] error: no input files\n";
    return 1;
  }

  std::string objectFile = outputPath + ".o";
  if (!compileToObject(inputPath, objectFile, emitIR)) {
    return 1; // Compilation failed (Errors already printed);
  }

  std::string linkCommand = "cc " + objectFile + " -o " + outputPath;
  int linkStatus = std::system(linkCommand.c_str());

  if (linkStatus != 0) {
    std::cerr << "[hydrolox] error: linking failed\n";
    return 1;
  }

  std::remove(objectFile.c_str());
  return 0;
}

bool compileToObject(const std::string &inputPath,
                     const std::string &outputPath, bool emitIR) {
  std::ifstream file(inputPath);
  if (!file.is_open()) {
    std::cerr << "[hydrolox] Error: could not open file '" << inputPath
              << "'\n";
    return false;
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

    // Stop compilation if Sema found type errors or undeclared variables
    if (diag.hasErrors()) {
      std::cerr << "\n=== SEMANTIC ERRORS ===\n";
      diag.reportDiagnostics(std::cerr);
      return false;
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
      return false;
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

    MPM.run(*mod, MAM);

    if (emitIR) {
      std::cout << "=== LLVM IR ===\n";
      bridge.dumpIR();
    }

    bridge.writeObject(outputPath);

    if (diag.hasErrors()) {
      std::cerr << "\n=== BACKEND ERRORS ===\n";
      diag.reportDiagnostics(std::cerr);
      return false;
    }

    std::cout << "Compiling Successful!\n";
    return true;
#else
    std::cout << "\n[hydrolox] LLVM disabled.\n";
    return false;
#endif

  } catch (const std::exception &e) {
    std::cerr << "\n[hydrolox] Syntax Error: " << e.what() << "\n";
    return false;
  }
}
