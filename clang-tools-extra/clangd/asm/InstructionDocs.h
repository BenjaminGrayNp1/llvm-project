#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_INSTRUCTION_DOCS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_INSTRUCTION_DOCS_H

#include "ParsedAST.h"
#include "support/Markup.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clang {

namespace clangd {

struct AsmPowerpcExtendedMnemonic {
	/** Name of the extended mnemonic */
	std::string Name;

	/** Args of the extended mnemonic */
	std::optional<std::string> Args;

	/** Number of args of the extended mnemonic */
	unsigned int NumArgs;

	/** Name of the resolved mnemonic */
	std::string BaseName;

	/** Args of the resolved mnemonic */
	std::optional<std::string> BaseArgs;

	AsmPowerpcExtendedMnemonic(std::string &&Name, std::optional<std::string> &&Args, unsigned int NumArgs, std::string &&BaseName, std::optional<std::string> &&BaseArgs);

	std::string getMarkupString() const;
};

struct AsmPowerpcSyntax {
	std::string Name;

	std::optional<std::string> Args;

	unsigned int NumArgs;

	std::optional<std::string> Comment;

	AsmPowerpcSyntax(std::string &&Name, std::optional<std::string> &&Args, int NumArgs, std::optional<std::string> Comment);

	std::string getMarkupString() const;
};

struct AsmPowerpcEncoding {
	std::string Heading;

	std::optional<std::string> Page;

	std::vector<AsmPowerpcSyntax> Syntaxes;

	AsmPowerpcEncoding(std::string &&Heading, std::optional<std::string> Page, std::vector<AsmPowerpcSyntax> &&Syntaxes);
};

struct AsmPowerpcInstruction {
	std::vector<AsmPowerpcEncoding> Encodings;

	std::vector<AsmPowerpcExtendedMnemonic> Emnemonics;

	std::shared_ptr<markup::Document> Description;

	AsmPowerpcInstruction(std::vector<AsmPowerpcEncoding> &&Encodings, std::vector<AsmPowerpcExtendedMnemonic> &&Emnemonics, std::shared_ptr<markup::Document> &&Description);
};

struct AsmPowerpcInstructionLookupResult {
	const AsmPowerpcInstruction *Instruction;

	const AsmPowerpcEncoding *Encoding;

	const AsmPowerpcSyntax *Syntax;

	const AsmPowerpcExtendedMnemonic *Emnemonic;

	AsmPowerpcInstructionLookupResult(const AsmPowerpcInstruction *Instr, const AsmPowerpcEncoding *Encoding, const AsmPowerpcSyntax *Syntax, const AsmPowerpcExtendedMnemonic *Emnemonic);

	void print(markup::Document &Os) const;
};

class PpcAsmDocs {
	static std::vector<std::map<std::string, std::shared_ptr<AsmPowerpcInstruction>>> InstructionMap;

	static void init(void);

public:
	static std::optional<AsmPowerpcInstructionLookupResult> getDocsForInstr(llvm::StringRef Name, const AsmAnnotatedInstruction &Instr);

	static std::vector<std::map<std::string, std::shared_ptr<AsmPowerpcInstruction>>> &getDocs(void);
};

} // namespace clangd

} // namespace clang

#endif /* LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_INSTRUCTION_DOCS_H */
