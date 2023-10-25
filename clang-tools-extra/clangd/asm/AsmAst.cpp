#include "AsmAst.h"
#include "CollectMacros.h"
#include "Diagnostics.h"
#include "asm/AsmTokens.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/Support/TargetSelect.h"

namespace clang {

namespace clangd {

void RunAsmPreprocessorAction::ExecuteAction()
{
	auto &CI = getCompilerInstance();
	auto &PP = CI.getPreprocessor();
	Token Tok;

	PP.EnterMainSourceFile();

	do {
		PP.Lex(Tok);
	} while (Tok.getKind() != tok::TokenKind::eof);
}

AsmAnnotatedInstruction::AsmAnnotatedInstruction(const llvm::MCInst &Inst, unsigned int Argc, bool IsDot) : Inst(Inst), Argc(Argc), IsDot(IsDot)
{}

MCStreamerClangdWrapper::MCStreamerClangdWrapper(llvm::MCContext &Context) : MCStreamer(Context)
{}

// We only want to intercept the emission of new instructions.
void MCStreamerClangdWrapper::emitInstruction(const llvm::MCInst &Inst,
                                              const llvm::MCSubtargetInfo & /* unused */)
{
	InstructionsWithArgc.emplace_back(Inst, 0, false);
}

void MCStreamerClangdWrapper::declareInstruction(SmallVector<std::unique_ptr<llvm::MCParsedAsmOperand>, 8> &&Operands)
{
	if (Operands.size() > 1 && Operands[1]->isToken()) {
		const auto &Operand = *Operands[1];

		std::string TokName;
		llvm::raw_string_ostream TokNameS(TokName);
		Operand.print(TokNameS);

		if (TokName == "'.'") {
			InstructionsWithArgc.back().IsDot = true;
			InstructionsWithArgc.back().Argc = Operands.size() - 2;
			return;
		}
	}

	InstructionsWithArgc.back().Argc = Operands.size() - 1;
}

bool MCStreamerClangdWrapper::emitSymbolAttribute(llvm::MCSymbol *Symbol, llvm::MCSymbolAttr Attribute)
{
	return true;
}

AsmAst::AsmAst(std::string_view Filename) : Filename(Filename) {}

void AsmAst::setInvocation(std::unique_ptr<CompilerInvocation> &&CI)
{
	this->CI = std::move(CI);
}

void AsmAst::setSourceBuffer(std::unique_ptr<llvm::MemoryBuffer> &&Buffer)
{
	SourceBuffer = std::move(Buffer);
}

CompilerInvocation *AsmAst::getInvocation() const
{
	return CI.get();
}

void AsmAst::parseAsm(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS)
{
	// llvm::errs() << "A>>>>>>>>>>>>>>>>>>>>>>>\n" << SourceBuffer->getBuffer() << "\nA<<<<<<<<<<<<<<<<<<<<<<<\n";

	/* Configure the preprocessor for the assembly stuff we want */
	CI->getPreprocessorOutputOpts().ShowCPP = 1;
	CI->getPreprocessorOutputOpts().ShowComments = 0;
	CI->getPreprocessorOutputOpts().ShowMacroComments = 0;
	CI->getFrontendOpts().DisableFree = false;

	/* Construct the compiler instance */
	Clang = prepareCompilerInstance(std::move(CI), nullptr, std::move(SourceBuffer), VFS, DiagnosticsConsumer);

	/* Construct the C preprocessor (and whatever else) */
	Action = std::make_unique<RunAsmPreprocessorAction>();
	const auto &MainInput = Clang->getInvocation().getFrontendOpts().Inputs[0];
	Action->BeginSourceFile(*Clang, MainInput);  // Makes the preprocessor

	Clang->createASTContext();

	auto &PP = Clang->getPreprocessor();

	/* Now we have a preprocessor, we can attach our macro/include/token watchers */
	auto TokenCollector = syntax::TokenCollector(PP);
	auto OnToken = PP.yoinkTokenWatcher();
	auto TC = AsmTokenCollector(PP, std::move(OnToken));

	PP.addPPCallbacks(std::make_unique<CollectMainFileMacros>(PP, Macros));

	Includes.collect(*Clang);

	/* Run the preprocessor */
	if (auto Err = Action->Execute()) {
		llvm::errs() << "Error executing action: " << Err << "\n";
		return;
	}

	/* Retrieve the tokens we generated */

	SyntaxTokenBuffer = std::make_unique<syntax::TokenBuffer>(std::move(TokenCollector).consume());
	SyntaxTokenBuffer->indexExpandedTokens();

	TokenBuffer = std::make_unique<AsmTokenBuffer>(std::move(TC).consume());

	// auto &SM = Clang->getSourceManager();
	// TokenBuffer->dump(SM);
	// llvm::errs() << "TokenBuffer->AsmBuffer.size() = " << TokenBuffer->AsmBuffer.size() << "\n";
	// llvm::errs() << "B>>>>>>>>>>>>>>>>>>>>>>>\n" << TokenBuffer->AsmBuffer << "\nB<<<<<<<<<<<<<<<<<<<<<<<\n";

	llvm::InitializeAllTargetInfos();
	llvm::InitializeAllTargetMCs();
	llvm::InitializeAllAsmParsers();
	llvm::InitializeAllDisassemblers();

	TheTriple = llvm::Triple(Clang->getInvocation().getTargetOpts().Triple);
	TheTripleName = TheTriple.getTriple();

	std::unique_ptr<llvm::MemoryBuffer> AsmBuffer = llvm::MemoryBuffer::getMemBuffer(TokenBuffer->AsmBuffer, Filename, false);
	std::string Error;

	auto AsmHandleDiag = [](const llvm::SMDiagnostic &Diag, void *Context) -> void {
		((std::vector<llvm::SMDiagnostic> *)Context)->emplace_back(std::move(Diag));
	};

	AsmSM.AddNewSourceBuffer(std::move(AsmBuffer), llvm::SMLoc());
	AsmSM.setIncludeDirs(Clang->getInvocation().getPreprocessorOpts().Includes);
	AsmSM.setDiagHandler(AsmHandleDiag, &Diagnostics);

	TheTarget = llvm::TargetRegistry::lookupTarget("", TheTriple, Error);
	if (!TheTarget) {
		llvm::errs() << "Cannot find asm target for triple\n";
		return;
	}

	MRI.reset(TheTarget->createMCRegInfo(TheTripleName));
	if (!MRI) {
		llvm::errs() << "Missing MCRegisterInfo\n";
		return;
	}

	MAI.reset(TheTarget->createMCAsmInfo(*MRI, TheTripleName, MCOptions));
	if (!MAI) {
		llvm::errs() << "Unable to create target asm info!\n";
		return;
	}

	MCII.reset(TheTarget->createMCInstrInfo());
	if (!MCII) {
		llvm::errs() << "Unable to create instruction info!\n";
		return;
	}

	for (auto Ftr : Clang->getInvocation().getTargetOpts().Features)
		Features.AddFeature(Ftr);

	STI.reset(TheTarget->createMCSubtargetInfo(TheTripleName, Clang->getInvocation().getTargetOpts().CPU, Features.getString()));
	if (!STI) {
		llvm::errs() << "Unable to create subtarget info!\n";
		return;
	}

	Ctx = std::make_unique<llvm::MCContext>(TheTriple, MAI.get(), MRI.get(), STI.get(), &AsmSM, &MCOptions);

	MOFI.reset(TheTarget->createMCObjectFileInfo(*Ctx, false, false));
	Ctx->setObjectFileInfo(MOFI.get());

	IP.reset(TheTarget->createMCInstPrinter(TheTriple, 0, *MAI, *MCII, *MRI));
	if (!IP) {
		llvm::errs() << "unable to create instruction printer for target triple\n";
		return;
	}

	MAB.reset(TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));

	Str = std::make_unique<MCStreamerClangdWrapper>(*Ctx);

	Parser.reset(llvm::createMCAsmParser(AsmSM, *Ctx, *Str, *MAI));

	TAP.reset(TheTarget->createMCAsmParser(*STI, *Parser, *MCII, MCOptions));
	if (!TAP) {
		llvm::errs() << "this target does not support assembly parsing.\n";
		return;
	}

	Str->setUseAssemblerInfoForParsing(true);
	Parser->setShowParsedOperands(false); /* (when true) Adds notes diagnostics as well! */
	Parser->setTargetParser(*TAP);

	const auto InstrHandler = [](SmallVector<std::unique_ptr<llvm::MCParsedAsmOperand>, 8> &&Operands, void *Context) -> void {
		((AsmAst *)Context)->Str->declareInstruction(std::move(Operands));
	};

	Parser->EmitInstructionOperands = InstrHandler;
	Parser->EmitInstructionOperandsContext = this;

	Parser->Run(false);

	auto *AsmMemBuff = AsmSM.getMemoryBuffer(AsmSM.getMainFileID());
	auto *AsmMemStart = AsmMemBuff->getBufferStart();

	// for (const auto &Inst : Str->InstructionsWithArgc) {
	// 	std::string InstStr;
	// 	llvm::raw_string_ostream InstStream(InstStr);

	// 	IP->printInst(&Inst.Inst, 0, "", *STI, InstStream);

	// 	auto Loc = Inst.Inst.getLoc();
	// 	auto Buffer = AsmSM.FindBufferContainingLoc(Loc);

	// 	if (Buffer != AsmSM.getMainFileID())
	// 		continue;

	// 	Diag Result;
	// 	Result.Severity = DiagnosticsEngine::Note;
	// 	Result.Source = Diag::Clangd;
	// 	Result.AbsFile = Filename;

	// 	auto *DiagAsmLocPtr = Loc.getPointer();
	// 	auto DiagAsmOffset = DiagAsmLocPtr - AsmMemStart;

	// 	auto DiagToken = TokenBuffer->getToken(DiagAsmOffset);

	// 	if (!DiagToken)
	// 		continue;

	// 	auto DiagSource = DiagToken->location();

	// 	if (!Clang->getSourceManager().isInMainFile(DiagSource))
	// 		continue;

	// 	Result.InsideMainFile = true;

	// 	auto Line = (int)Clang->getSourceManager().getExpansionLineNumber(DiagSource, nullptr);
	// 	auto Col = (int)Clang->getSourceManager().getExpansionColumnNumber(DiagSource, nullptr);

	// 	Result.Range = {{Line - 1, Col}, {Line - 1, Col}};
	// 	std::string Msg = InstStr;
	// 	Result.Message = Msg;

	// 	Diags.emplace_back(Result);
	// }

	for (auto AsmDiag : Diagnostics) {
		if (AsmDiag.getFilename() != Filename)
			continue;

		Diag Result;
		Result.Message = AsmDiag.getMessage().str();

		switch (AsmDiag.getKind()) {
		case llvm::SourceMgr::DK_Error:
			Result.Severity = DiagnosticsEngine::Error;
			break;
		case llvm::SourceMgr::DK_Warning:
			Result.Severity = DiagnosticsEngine::Warning;
			break;
		default:
			break;
		}

		Result.Source = Diag::Clangd;
		Result.AbsFile = AsmDiag.getFilename().str();
		Result.InsideMainFile = true;

		auto *DiagAsmLocPtr = AsmDiag.getLoc().getPointer();
		auto DiagAsmOffset = DiagAsmLocPtr - AsmMemStart;
		auto DiagToken = TokenBuffer->getToken(DiagAsmOffset);

		if (!DiagToken)
			continue;

		auto DiagSource = DiagToken->location();

		if (!Clang->getSourceManager().isInMainFile(DiagSource))
			continue;

		Result.InsideMainFile = true;

		auto Line = (int)Clang->getSourceManager().getExpansionLineNumber(DiagSource, nullptr);
		auto Col = (int)Clang->getSourceManager().getExpansionColumnNumber(DiagSource, nullptr);

		Result.Range = {{Line - 1, Col}, {Line - 1, Col}};

		Diags.emplace_back(Result);
	}
}

raw_ostream& operator<<(raw_ostream &OS, const AsmAnnotatedInstruction &AsmInst) {
	OS << "AsmAnnotatedInstruction { ";
	AsmInst.Inst.print(OS);
	OS << ", Argc: " << AsmInst.Argc << ", IsDot: " << AsmInst.IsDot << " }";
	return OS;
}

} // namespace clangd

} // namespace clang
