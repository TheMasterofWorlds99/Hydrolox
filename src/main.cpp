#include <elpc/elpc.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/TargetParser/Triple.h>
#include <sstream>
#include <string>

#include <generator.hpp>
#include <lexer.hpp>
#include <parser.hpp>
#include <sema.hpp>

#ifdef ELPC_ENABLE_LLVM
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#endif

struct CompilerOptions {
  std::string command;
  std::string inputPath;
  std::string outputPath;
#ifdef ELPC_ENABLE_LLVM
  llvm::OptimizationLevel optLevel = llvm::OptimizationLevel::O0;
#endif
};

void printUsage() {
  std::cout
      << "Usage: hydrolox <command> [options] <file.hlox>\n\n"
      << "Commands:\n"
      << "  build      Compile and link to an executable\n"
      << "  run        Compile, link, and run immediately\n"
      << "  emit-ir    Generate LLVM IR (.ll file)\n"
      << "  emit-obj   Generate object file (.o file)\n\n"
      << "Options:\n"
      << "  -o <file>  Specify output file\n"
      << "  -O<level>  Optimization level (0, 1, 2, 3, s, z). Default is 0.\n";
}

std::string getBaseName(const std::string &path) {
  std::filesystem::path p(path);
  return p.stem().string();
}

bool compile(const CompilerOptions &opts);

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  CompilerOptions opts;
  opts.command = argv[1];

  if (opts.command != "build" && opts.command != "run" &&
      opts.command != "emit-ir" && opts.command != "emit-obj") {
    std::cerr << "[hydrolox] error: unknown command '" << opts.command << "'\n";
    printUsage();
    return 1;
  }

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];

    // Skip redundant command words to prevent them being parsed as input files
    if (arg == "build" || arg == "run" || arg == "emit-ir" ||
        arg == "emit-obj") {
      continue;
    }

    if (arg == "-o") {
      if (i + 1 < argc) {
        opts.outputPath = argv[++i];
      } else {
        std::cerr << "[hydrolox] error: missing filename after '-o'\n";
        return 1;
      }
    } else if (arg.size() >= 3 && arg.substr(0, 2) == "-O") {
#ifdef ELPC_ENABLE_LLVM
      if (arg == "-O0")
        opts.optLevel = llvm::OptimizationLevel::O0;
      else if (arg == "-O1")
        opts.optLevel = llvm::OptimizationLevel::O1;
      else if (arg == "-O2")
        opts.optLevel = llvm::OptimizationLevel::O2;
      else if (arg == "-O3")
        opts.optLevel = llvm::OptimizationLevel::O3;
      else if (arg == "-Os")
        opts.optLevel = llvm::OptimizationLevel::Os;
      else if (arg == "-Oz")
        opts.optLevel = llvm::OptimizationLevel::Oz;
      else {
        std::cerr << "[hydrolox] error: unknown optimization level '" << arg
                  << "'\n";
        return 1;
      }
#endif
    } else if (arg[0] == '-') {
      std::cerr << "[hydrolox] error: unknown option '" << arg << "'\n";
      return 1;
    } else {
      if (opts.inputPath.empty()) {
        opts.inputPath = arg;
      } else {
        std::cerr
            << "[hydrolox] error: multiple input files not supported (found '"
            << opts.inputPath << "' and '" << arg << "')\n";
        return 1;
      }
    }
  }

  if (opts.inputPath.empty()) {
    std::cerr << "[hydrolox] error: no input files\n";
    return 1;
  }

  std::string baseName = getBaseName(opts.inputPath);

  if (opts.outputPath.empty()) {
    if (opts.command == "build" || opts.command == "run") {
      opts.outputPath = "a.out";
    } else if (opts.command == "emit-ir") {
      opts.outputPath = baseName + ".ll";
    } else if (opts.command == "emit-obj") {
      opts.outputPath = baseName + ".o";
    }
  }

  std::string objFile =
      (opts.command == "emit-obj") ? opts.outputPath : baseName + ".o";

  CompilerOptions compileOpts = opts;
  if (opts.command != "emit-ir") {
    compileOpts.outputPath = objFile;
  }

  if (!compile(compileOpts)) {
    return 1;
  }

  if (opts.command == "emit-ir" || opts.command == "emit-obj") {
    return 0;
  }

  std::string linkCommand =
      "cc \"" + objFile + "\" -o \"" + opts.outputPath + "\"";
  if (std::system(linkCommand.c_str()) != 0) {
    std::cerr << "[hydrolox] error: linking failed\n";
    return 1;
  }

  std::remove(objFile.c_str());

  if (opts.command == "run") {
    std::string runCommand = "./" + opts.outputPath;
    int runStatus = std::system(runCommand.c_str());
    std::remove(opts.outputPath.c_str());
    return runStatus;
  }

  return 0;
}

bool compile(const CompilerOptions &opts) {
  std::ifstream file(opts.inputPath);
  if (!file.is_open()) {
    std::cerr << "[hydrolox] error: could not open file '" << opts.inputPath
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

    if (diag.hasErrors()) {
      diag.reportDiagnostics(std::cerr);
      return false;
    }

#ifdef ELPC_ENABLE_LLVM
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("hydrolox_core", ctx);

    llvm::Triple targetTriple =
        llvm::Triple(llvm::sys::getDefaultTargetTriple());
    mod->setTargetTriple(targetTriple);

    std::string targetError;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, targetError);
    if (!target) {
      std::cerr << "[hydrolox] error: " << targetError << "\n";
      return false;
    }

    llvm::TargetOptions targetOpts;
    llvm::SubtargetFeatures features;
    llvm::StringMap<bool> hostFeatures = llvm::sys::getHostCPUFeatures();

    for (const auto &f : hostFeatures) {
      if (f.second)
        features.AddFeature(f.first());
    }

    auto TM = std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
        targetTriple, llvm::sys::getHostCPUName(), features.getString(),
        targetOpts, llvm::Reloc::PIC_));

    mod->setDataLayout(TM->createDataLayout());

    elpc::LLVMBridge bridge(ctx, *mod, diag);
    LLVMGenerator codegen(bridge);

    for (const auto &root : astRoots) {
      root->accept(codegen);
    }

    if (diag.hasErrors()) {
      diag.reportDiagnostics(std::cerr);
      return false;
    }

    if (opts.optLevel != llvm::OptimizationLevel::O0) {
      llvm::LoopAnalysisManager LAM;
      llvm::FunctionAnalysisManager FAM;
      llvm::CGSCCAnalysisManager CGAM;
      llvm::ModuleAnalysisManager MAM;
      llvm::PassBuilder PB(TM.get());

      PB.registerModuleAnalyses(MAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

      llvm::ModulePassManager MPM =
          PB.buildPerModuleDefaultPipeline(opts.optLevel);
      MPM.run(*mod, MAM);
    }

    if (opts.command == "emit-ir") {
      std::error_code EC;
      llvm::raw_fd_ostream dest(opts.outputPath, EC, llvm::sys::fs::OF_None);
      if (EC) {
        std::cerr << "[hydrolox] error: could not open file: " << EC.message()
                  << "\n";
        return false;
      }
      mod->print(dest, nullptr);
      return true;
    }

    std::error_code EC;
    llvm::raw_fd_ostream dest(opts.outputPath, EC, llvm::sys::fs::OF_None);
    if (EC) {
      std::cerr << "[hydrolox] error: " << EC.message() << "\n";
      return false;
    }
    llvm::legacy::PassManager codegenPM;
    TM->addPassesToEmitFile(codegenPM, dest, nullptr,
                            llvm::CodeGenFileType::ObjectFile);
    codegenPM.run(*mod);

    if (diag.hasErrors()) {
      diag.reportDiagnostics(std::cerr);
      return false;
    }
    return true;
#else
    std::cout << "[hydrolox] error: LLVM support is disabled.\n";
    return false;
#endif
  } catch (const std::exception &e) {
    std::cerr << "[hydrolox] error: " << e.what() << "\n";
    return false;
  }
}
