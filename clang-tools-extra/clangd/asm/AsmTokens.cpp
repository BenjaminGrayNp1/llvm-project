#include "AsmTokens.h"
#include "clang/Tooling/Syntax/Tokens.h"

namespace clang {

AsmToken::AsmToken(const Token &T) : Tok(T)
{}

void AsmToken::dump(SourceManager &SM) const
{
	llvm::errs() << "Token { '" << text(SM) << "' isMacro:" << Tok.getLocation().isMacroID() << " ";

	Tok.getLocation().dump(SM);

	llvm::errs() << "}";
}

llvm::StringRef AsmToken::text(SourceManager &SM) const
{
	bool Invalid = false;
	const char *Start = SM.getCharacterData(Tok.getLocation(), &Invalid);
	assert(!Invalid);
	return llvm::StringRef(Start, Tok.getLength());
}

SourceLocation AsmToken::location() const
{
	return Tok.getLocation();
}

SourceLocation AsmToken::endLocation() const
{
	return Tok.getEndLoc();
}

SourceRange AsmToken::range() const
{
	return SourceRange(location(), endLocation());
}

size_t AsmToken::size() const
{
	return Tok.getLength();
}

AsmTokenBuffer::AsmTokenBuffer(std::vector<AsmToken> &&Tokens, std::vector<size_t> &&TokenOffsets, std::string &&Buffer)
: ExpandedTokens(std::move(Tokens)), TokenOffsets(std::move(TokenOffsets)), AsmBuffer(std::move(Buffer))
{}

void AsmTokenBuffer::dump(SourceManager &SM) const
{
	for (size_t I = 0; I < ExpandedTokens.size(); I++) {
		const auto &Token = ExpandedTokens[I];
		const auto Offset = TokenOffsets[I];

		llvm::errs() << "@ " << Offset << " -> ";
		Token.dump(SM);
	}

	// for (const auto &Token : Tokens) {
	// 	Token.dump(SM);
	// }

	llvm::errs() << "\nBuffer:>>>>>>\n" << AsmBuffer << "<<<<<<<<<<<<<<<<\n";
}

std::optional<AsmToken> AsmTokenBuffer::getToken(size_t AsmOffset) const
{
	auto It = std::partition_point(TokenOffsets.cbegin(), TokenOffsets.cend(), [=](auto TokOffset) { return TokOffset < AsmOffset; });

	if (It == TokenOffsets.cend())
		return std::nullopt;

	auto Index = std::distance(TokenOffsets.cbegin(), It);
	auto StartOffset = TokenOffsets[Index];
	auto Token = ExpandedTokens[Index];

	if (Token.size() < AsmOffset - StartOffset)
		return std::nullopt;

	return Token;
}

std::optional<unsigned int> AsmTokenBuffer::getAsmOffset(SourceLocation Loc, SourceManager &SM) const
{
	for (size_t I = 0; I < ExpandedTokens.size(); I++) {
		if (ExpandedTokens[I].location() == Loc) {
			return TokenOffsets[I];
		}
	}

	return std::nullopt;
}

std::optional<std::string> AsmTokenBuffer::getMacroExpansion(SourceLocation ExpandedLoc, SourceManager &SM) const
{
	// We receive the expansion loc; i.e., a location in an actual source file. So we can't(?) binary search
	// our token list properly, because they are all ethereal locs.
	// llvm::errs() << "Looking for expansion loc " << ExpandedLoc.printToString(SM) << "\n";

	size_t Index = 0;

	while (Index < ExpandedTokens.size()) {
		if (SM.getExpansionLoc(ExpandedTokens[Index].location()) == ExpandedLoc) {
			break;
		}

		Index++;
	}

	// llvm::errs() << "Index = " << Index << "\n";

	if (Index >= ExpandedTokens.size()) {
		// llvm::errs() << "No match\n";
		return std::nullopt;
	}

	auto EndIndex = Index;

	while (EndIndex + 1 < ExpandedTokens.size()) {
		auto &Tok = ExpandedTokens[EndIndex + 1];

		// Tok.dump(SM);

		if (SM.getExpansionLoc(Tok.location()) != ExpandedLoc) {
			break;
		}

		EndIndex++;
	}

	// llvm::errs() << "EndIndex = " << EndIndex << "\n";

	auto BeginOffset = TokenOffsets[Index];
	auto EndOffset = TokenOffsets[EndIndex] + ExpandedTokens[EndIndex].size();

	// llvm::errs() << "::::: " << BeginOffset << ":" << EndOffset << "\n";

	auto Content = AsmBuffer.substr(BeginOffset, EndOffset - BeginOffset);

	// llvm::errs() << ">>>>>>>>>\n" << Content << "\n<<<<<<<<<<<<<<\n";

	return Content;
}

AsmTokenCollector::AsmTokenCollector(Preprocessor &PP, llvm::unique_function<void(const clang::Token &)> &&OnToken) : SM(PP.getSourceManager()), OnToken(std::move(OnToken)), ExpandedBuffer(), ExpandedStr(ExpandedBuffer)
{
	PP.setTokenWatcher([this, &PP](const clang::Token &T) {
		if (T.isAnnotation())
			return;

		// eod token isn't emitted in raw mode, causing a mismatch with the TokenBuffer somehow
		// FIXME: Mark next token as starting a new line
		if (T.getKind() == tok::TokenKind::eod)
			return;

		if (T.isAtStartOfLine())
			pushAsmText("\n");

		if (T.hasLeadingSpace())
			pushAsmText(" ");


		// llvm::errs() << syntax::Token(T) << "\n";
		// TODO: Build expansion location -> macro text range index

		// llvm::errs() << "Saw token " << T.getLocation().printToString(SM) << "\n";

		AsmToken Tok(T);

		TokenOffsets.push_back(AsmOffset);

		if (IdentifierInfo *II = T.getIdentifierInfo()) {
			pushAsmText(II->getName());
		} else if (T.isLiteral() && !T.needsCleaning() && T.getLiteralData()) {
			pushAsmText(std::string_view(T.getLiteralData(), T.getLength()));
		} else {
			pushAsmText(PP.getSpelling(T));
		}

		Expanded.push_back(Tok);

		this->OnToken(T);
	});
}

void AsmTokenCollector::pushAsmText(std::string_view Text)
{
	ExpandedStr << Text;
	AsmOffset += Text.size();
}

AsmTokenBuffer AsmTokenCollector::consume() && {
	ExpandedStr.flush();

	return AsmTokenBuffer(std::move(Expanded), std::move(TokenOffsets), std::move(ExpandedBuffer));
}

} // namespace clang
