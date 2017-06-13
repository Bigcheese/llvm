//===-- CFGProfile.cpp ----------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation.h"

#include <array>

using namespace llvm;

class CFGProfilePass : public ModulePass {
public:
  static char ID;

  CFGProfilePass() : ModulePass(ID) {
    initializeCFGProfilePassPass(
      *PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "CFGProfilePass"; }

private:
  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<BranchProbabilityInfoWrapperPass>();
  }
};

bool CFGProfilePass::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  llvm::DenseMap<std::pair<StringRef, StringRef>, uint64_t> Counts;

  for (auto &F : M) {
    if (F.isDeclaration())
      continue;
    getAnalysis<BranchProbabilityInfoWrapperPass>(F).getBPI();
    auto &BFI = getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
    for (auto &BB : F) {
      Optional<uint64_t> BBCount = BFI.getBlockProfileCount(&BB);
      if (!BBCount)
        continue;
      for (auto &I : BB) {
        auto *CI = dyn_cast<CallInst>(&I);
        if (!CI)
          continue;
        Function *CalledF = CI->getCalledFunction();
        if (!CalledF || CalledF->isIntrinsic())
          continue;

        uint64_t &Count =
            Counts[std::make_pair(F.getName(), CalledF->getName())];
        Count = SaturatingAdd(Count, *BBCount);
      }
    }
  }

  if (Counts.empty())
    return false;

  LLVMContext &Context = M.getContext();
  MDBuilder MDB(Context);
  std::vector<Metadata *> Nodes;

  for (auto E : Counts) {
    SmallVector<Metadata *, 3> Vals;
    Vals.push_back(MDB.createString(E.first.first));
    Vals.push_back(MDB.createString(E.first.second));
    Vals.push_back(MDB.createConstant(
        ConstantInt::get(Type::getInt64Ty(Context), E.second)));
    Nodes.push_back(MDNode::get(Context, Vals));
  }

  M.addModuleFlag(Module::Append, "CFG Profile", MDNode::get(Context, Nodes));

  return true;
}

char CFGProfilePass::ID = 0;
INITIALIZE_PASS_BEGIN(CFGProfilePass, "cfg-profile",
  "Generate profile information from the CFG.", false, false)
  INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(BranchProbabilityInfoWrapperPass)
  INITIALIZE_PASS_END(CFGProfilePass, "cfg-profile",
    "Generate profile information from the CFG.", false, false)

ModulePass *llvm::createCFGProfilePass() {
  return new CFGProfilePass();
}
