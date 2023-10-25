#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_ASM_AST_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_ASM_AST_H

#include "clang/Frontend/CompilerInstance.h"
#include "CollectMacros.h"
#include "Headers.h"
#include "Compiler.h"
#include "asm/AsmTokens.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"

#include <string>

namespace clang {

namespace clangd {

class RunAsmPreprocessorAction : public PreprocessorFrontendAction {
public:
	RunAsmPreprocessorAction() = default;

protected:
	void ExecuteAction() override;

	bool hasPCHSupport() const override { return false; }
};

struct AsmAnnotatedInstruction {
	/** The raw instruction */
	llvm::MCInst Inst;

	/** Number of arguments (not including the dot suffix the parser splits out as an arg) */
	unsigned int Argc;

	/** Whether the instruction name should have a '.' suffix appended to it */
	bool IsDot;

	AsmAnnotatedInstruction(const llvm::MCInst &Inst, unsigned int Argc, bool IsDot);
};

raw_ostream& operator<<(raw_ostream &OS, const AsmAnnotatedInstruction &AsmInst);

class MCStreamerClangdWrapper final : public llvm::MCStreamer {
public:
	std::vector<AsmAnnotatedInstruction> InstructionsWithArgc {};

	MCStreamerClangdWrapper(llvm::MCContext &Context);

	// We only want to intercept the emission of new instructions.
	void emitInstruction(const llvm::MCInst &Inst,
	                     const llvm::MCSubtargetInfo & /* unused */) override;

	void declareInstruction(SmallVector<std::unique_ptr<llvm::MCParsedAsmOperand>, 8> &&Operands);
	bool emitSymbolAttribute(llvm::MCSymbol *Symbol, llvm::MCSymbolAttr Attribute) override;

	void emitCommonSymbol(llvm::MCSymbol *Symbol, uint64_t Size,
				llvm::Align ByteAlignment) override {}
	void emitZerofill(llvm::MCSection *Section, llvm::MCSymbol *Symbol = nullptr,
			uint64_t Size = 0, llvm::Align ByteAlignment = llvm::Align(1),
			llvm::SMLoc Loc = llvm::SMLoc()) override {}
	void emitGPRel32Value(const llvm::MCExpr *Value) override {}
	void beginCOFFSymbolDef(const llvm::MCSymbol *Symbol) override {}
	void emitCOFFSymbolStorageClass(int StorageClass) override {}
	void emitCOFFSymbolType(int Type) override {}
	void endCOFFSymbolDef() override {}
};

class AsmAst {
public:
	AsmAst(std::string_view Filename);

	void setInvocation(std::unique_ptr<CompilerInvocation> &&CI);

	void setSourceBuffer(std::unique_ptr<llvm::MemoryBuffer> &&Buffer);

	CompilerInvocation *getInvocation() const;

	void parseAsm(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS);

public:
	std::string Filename;

	llvm::Triple TheTriple;

	std::string TheTripleName;

	const llvm::Target *TheTarget = nullptr;

	std::vector<Diag> Diags;

	std::unique_ptr<CompilerInvocation> CI;

	std::unique_ptr<CompilerInstance> Clang;

	std::unique_ptr<RunAsmPreprocessorAction> Action;

	MainFileMacros Macros;

	IncludeStructure Includes;

	std::unique_ptr<syntax::TokenBuffer> SyntaxTokenBuffer;

	/** Buffer of user source text */
	std::unique_ptr<llvm::MemoryBuffer> SourceBuffer;

	/** C diagnostics consumer, used to ignore bogus C diagnostics */
	IgnoreDiagnostics DiagnosticsConsumer;

	/** SourceManager equivalent for MC* style libraries */
	llvm::SourceMgr AsmSM;

	/** MC* style library diagnostics */
	std::vector<llvm::SMDiagnostic> Diagnostics;

	std::unique_ptr<AsmTokenBuffer> TokenBuffer;

	std::unique_ptr<llvm::MCRegisterInfo> MRI;

	llvm::MCTargetOptions MCOptions;

	std::unique_ptr<llvm::MCAsmInfo> MAI;

	std::unique_ptr<llvm::MCInstrInfo> MCII;

	llvm::SubtargetFeatures Features;

	std::unique_ptr<llvm::MCSubtargetInfo> STI;

	std::unique_ptr<llvm::MCContext> Ctx;

	std::unique_ptr<llvm::MCObjectFileInfo> MOFI;

	std::unique_ptr<llvm::MCInstPrinter> IP;

	std::unique_ptr<llvm::MCAsmBackend> MAB;

	std::unique_ptr<MCStreamerClangdWrapper> Str;

	std::unique_ptr<llvm::MCAsmParser> Parser;

	std::unique_ptr<llvm::MCTargetAsmParser> TAP;
};

} // namespace clangd

} // namespace clang

#endif /* LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_ASM_AST_H */
