#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_ASM_TOKEN_COLLECTOR_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_ASM_TOKEN_COLLECTOR_H

#include "clang/Lex/Preprocessor.h"

namespace clang {

class AsmToken {
public:
	AsmToken(const Token &T);

	void dump(SourceManager &SM) const;

	llvm::StringRef text(SourceManager &SM) const;

	SourceLocation location() const;

	SourceLocation endLocation() const;

	SourceRange range() const;

	size_t size() const;

private:
	Token Tok;
};

class AsmTokenBuffer {
public:
	AsmTokenBuffer(std::vector<AsmToken> &&Tokens, std::vector<size_t> &&T, std::string &&Buffer);

	void dump(SourceManager &SM) const;

	/* Reverses an offset in the ASM buffer into a Token from the CPP output */
	std::optional<AsmToken> getToken(size_t AsmOffset) const;

	std::optional<SourceRange> getTokenRange(size_t AsmOffset) const;

	/* Forward maps a source location to the offset in the ASM buffer it ends up at */
	std::optional<unsigned int> getAsmOffset(SourceLocation Loc, SourceManager &SM) const;

	std::optional<std::string> getMacroExpansion(SourceLocation MacroLoc, SourceManager &SM) const;

public:
	std::vector<AsmToken> ExpandedTokens;

	std::vector<size_t> TokenOffsets;

	std::string AsmBuffer;
};

/**
 * Gathers all tokens emitted by the preprocessor. Required because we need
 * newline information to construct correct assembly statements, but the'
 * syntax::Token class used by the builtin TokenCollector throws away this
 * property. There is also an issue with the ASM mode preprocessor emitting
 * EOD (end of directive) tokens that the TokenCollector does not like.
 *
 * Instead, we register this class and forward the interesting tokens
 */
class AsmTokenCollector {
public:
	AsmTokenCollector(Preprocessor &PP, llvm::unique_function<void(const clang::Token &)> &&OnToken);

	[[nodiscard]] AsmTokenBuffer consume() &&;

private:
	void pushAsmText(std::string_view Text);

	SourceManager &SM;

	/* The original token watcher on the preprocessor */
	llvm::unique_function<void(const clang::Token &)> OnToken;

	/* The token expansions we have collected */
	std::vector<AsmToken> Expanded;

	/* List of ASM offsets of each token. Indexed by CPP token index, result is the index that token was placed in the ASM buffer */
	std::vector<size_t> TokenOffsets;

	/* The stringified result of running the preprocessor */
	std::string ExpandedBuffer;

	/* The stream over ExpandedBuffer to build the ASM output source */
	llvm::raw_string_ostream ExpandedStr;

	size_t AsmOffset = 0;
};

} // namespace clang

#endif /* LLVM_CLANG_TOOLS_EXTRA_CLANGD_ASM_ASM_TOKEN_COLLECTOR_H */
