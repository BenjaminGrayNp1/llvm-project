#include "InstructionDocs.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"

namespace clang {

namespace clangd {

AsmPowerpcExtendedMnemonic::AsmPowerpcExtendedMnemonic(std::string &&Name, std::optional<std::string> &&Args, unsigned int NumArgs, std::string &&BaseName, std::optional<std::string> &&BaseArgs)
: Name(std::move(Name)), Args(std::move(Args)), NumArgs(NumArgs), BaseName(std::move(BaseName)), BaseArgs(std::move(BaseArgs))
{}

std::string AsmPowerpcExtendedMnemonic::getMarkupString() const
{
	std::string Os = Name;

	if (Args.has_value())
		Os += " " + Args.value();

	Os += "  # extended mnemonic => " + BaseName;

	if (BaseArgs.has_value())
	Os += " " + BaseArgs.value();

	return Os;
}

AsmPowerpcSyntax::AsmPowerpcSyntax(std::string &&Name, std::optional<std::string> &&Args, int NumArgs, std::optional<std::string> Comment)
: Name(std::move(Name)), Args(std::move(Args)), NumArgs(NumArgs), Comment(std::move(Comment))
{}

std::string AsmPowerpcSyntax::getMarkupString() const
{
	std::string Os = Name;

	if (Args.has_value())
		Os += " " + Args.value();

	if (Comment.has_value())
		Os += "  # " + Comment.value();

	return Os;
}

AsmPowerpcEncoding::AsmPowerpcEncoding(std::string &&Heading, std::optional<std::string> Page, std::vector<AsmPowerpcSyntax> &&Syntaxes)
: Heading(std::move(Heading)), Page(std::move(Page)), Syntaxes(std::move(Syntaxes))
{}

AsmPowerpcInstruction::AsmPowerpcInstruction(std::vector<AsmPowerpcEncoding> &&Encodings, std::vector<AsmPowerpcExtendedMnemonic> &&Emnemonics, std::shared_ptr<markup::Document> &&Description)
: Encodings(std::move(Encodings)), Emnemonics(std::move(Emnemonics)), Description(std::move(Description))
{}

AsmPowerpcInstructionLookupResult::AsmPowerpcInstructionLookupResult(const AsmPowerpcInstruction *Instr, const AsmPowerpcEncoding *Encoding, const AsmPowerpcSyntax *Syntax, const AsmPowerpcExtendedMnemonic *Emnemonic)
: Instruction(Instr), Encoding(Encoding), Syntax(Syntax), Emnemonic(Emnemonic)
{}

void AsmPowerpcInstructionLookupResult::print(markup::Document &Os) const
{
	Os.addHeading(3).appendText(Encoding->Heading);

	if (Encoding->Page.has_value())
		Os.addParagraph().appendText("See more on page " + *Encoding->Page);

	if (Syntax != nullptr)
		Os.addCodeBlock(Syntax->getMarkupString(), "powerpc");
	else if (Emnemonic != nullptr)
		Os.addCodeBlock(Emnemonic->getMarkupString(), "powerpc");

	Os.addHeading(3).appendText("Description:");
	Os.append(*Instruction->Description);
}

std::optional<AsmPowerpcInstructionLookupResult> PpcAsmDocs::getDocsForInstr(llvm::StringRef Name, const AsmAnnotatedInstruction &Instr)
{
	const auto &AllDocs = getDocs();

	if (AllDocs.size() <= Instr.Argc)
		return std::nullopt;

	const auto &Docs = AllDocs[Instr.Argc];

	std::string NameUpper = Name.upper();

	if (Instr.IsDot)
		NameUpper += ".";

	const auto It = Docs.find(NameUpper);
	if (It == Docs.end())
		return std::nullopt;

	const auto DocInstr = It->second;

	for (const auto &Encoding : DocInstr->Encodings)
		for (const auto &Syntax : Encoding.Syntaxes)
			if (llvm::StringRef(Syntax.Name).upper() == NameUpper && Syntax.NumArgs == Instr.Argc)
				return AsmPowerpcInstructionLookupResult(DocInstr.get(), &Encoding, &Syntax, nullptr);

	for (const auto &Emnemonic : DocInstr->Emnemonics)
		if (llvm::StringRef(Emnemonic.Name).upper() == NameUpper && Emnemonic.NumArgs == Instr.Argc)
			return AsmPowerpcInstructionLookupResult(DocInstr.get(), &DocInstr->Encodings[0], nullptr, &Emnemonic);

	return std::nullopt;
}

std::vector<std::map<std::string, std::shared_ptr<AsmPowerpcInstruction>>> &PpcAsmDocs::getDocs(void)
{
	if (InstructionMap.empty())
		init();

	return InstructionMap;
}

std::vector<std::map<std::string, std::shared_ptr<AsmPowerpcInstruction>>> PpcAsmDocs::InstructionMap {};

void deserializeDocument(markup::Document &Os, const llvm::json::Value &Serialized);

void deserializeParagraph(markup::Paragraph &Os, const llvm::json::Value &Serialized)
{
	for (const auto &NodeVal : *Serialized.getAsArray()) {
		if (NodeVal.getAsString().has_value()) {
			Os.appendText(*NodeVal.getAsString());
			continue;
		}

		const auto &Node = *NodeVal.getAsObject();
		const auto &Type = Node.getString("type");

		if (Type == "Code") {
			Os.appendCode(*Node.getString("content"));
			continue;
		}

		llvm::errs() << "Unrecognised paragraph node\n";
	}
}

void deserializeBulletList(markup::BulletList &Os, const llvm::json::Value &Serialized)
{
	for (const auto &NodeVal : *Serialized.getAsArray())
		deserializeDocument(Os.addItem(), NodeVal);
}

void deserializeDocument(markup::Document &Os, const llvm::json::Value &Serialized)
{
	for (const auto &NodeVal : *Serialized.getAsArray()) {
		const auto &Node = *NodeVal.getAsObject();
		const auto &Type = Node.getString("type");

		if (Type == "Ruler") {
			Os.addRuler();
			continue;
		}

		if (Type == "Paragraph") {
			deserializeParagraph(Os.addParagraph(), *Node.get("content"));
			continue;
		}

		if (Type == "CodeBlock") {
			Os.addCodeBlock(Node.getString("content")->str(), Node.getString("language")->str());
			continue;
		}

		if (Type == "Heading") {
			deserializeParagraph(Os.addHeading(*Node.getNumber("level")), *Node.getObject("content")->get("content"));
			continue;
		}

		if (Type == "BulletList") {
			deserializeBulletList(Os.addBulletList(), *Node.get("content"));
			continue;
		}

		llvm::errs() << "Unrecognised Document node\n";
	}
}

std::optional<std::string> optionalStrRefToOwned(std::optional<llvm::StringRef> MaybeRef)
{
	if (MaybeRef.has_value())
		return MaybeRef->str();

	return std::nullopt;
}

std::shared_ptr<markup::Document> deserializeDescription(const llvm::json::Value &Serialized)
{
	const auto Document = std::make_shared<markup::Document>();

	deserializeDocument(*Document, Serialized);

	return Document;
}

AsmPowerpcSyntax deserializeSyntax(const llvm::json::Value &Serialized)
{
	const auto &JSyntax = Serialized.getAsObject();

	std::string Name = JSyntax->getString("name")->str();

	std::optional<std::string> Args = std::nullopt;
	if (JSyntax->getString("args").has_value())
		Args = JSyntax->getString("args")->str();

	int NumArgs = *JSyntax->getNumber("numargs");

	std::optional<std::string> Comment = std::nullopt;
	if (JSyntax->getString("comment").has_value())
		Comment = JSyntax->getString("comment")->str();

	return AsmPowerpcSyntax(std::move(Name), std::move(Args), NumArgs, std::move(Comment));
}

AsmPowerpcEncoding deserializeEncoding(const llvm::json::Value &Serialized)
{
	const auto &JEncoding = Serialized.getAsObject();

	std::string Heading = JEncoding->getString("heading")->str();

	const auto Page = optionalStrRefToOwned(JEncoding->getString("page"));

	std::vector<AsmPowerpcSyntax> Syntaxes;

	for (const auto &Syntax : *JEncoding->getArray("syntax"))
		Syntaxes.emplace_back(deserializeSyntax(Syntax));

	return AsmPowerpcEncoding(std::move(Heading), std::move(Page), std::move(Syntaxes));
}

AsmPowerpcExtendedMnemonic deserializeEmnemonics(const llvm::json::Value &Serialized)
{
	const auto &J = Serialized.getAsObject();

	return AsmPowerpcExtendedMnemonic(
		J->getString("name")->str(),
		optionalStrRefToOwned(J->getString("args")),
		*J->getNumber("numargs"),
		J->getString("baseName")->str(),
		optionalStrRefToOwned(J->getString("baseArgs"))
	);
}

std::shared_ptr<AsmPowerpcInstruction> deserializeInstruction(const llvm::json::Value &Serialized)
{
	const auto &Instr = *Serialized.getAsObject();

	std::vector<AsmPowerpcEncoding> Encodings {};

	for (const auto &Encoding : *Instr.getArray("encodings"))
		Encodings.emplace_back(deserializeEncoding(Encoding));

	std::vector<AsmPowerpcExtendedMnemonic> Emnemonics {};

	for (const auto &Emnemonic : *Instr.getArray("extendedMnemonics"))
		Emnemonics.emplace_back(deserializeEmnemonics(Emnemonic));

	auto Description = deserializeDescription(*Instr.get("description"));

	return std::make_shared<AsmPowerpcInstruction>(std::move(Encodings), std::move(Emnemonics), std::move(Description));
}

void PpcAsmDocs::init(void)
{
	const char *DocsPath = std::getenv("CLANGD_DOCS_PPC");
	if (DocsPath == nullptr)
		return;

	llvm::errs() << "Looking for PPC docs in " << DocsPath << "\n";

	auto Buffer = llvm::MemoryBuffer::getFile(DocsPath);

	if (auto Err = Buffer.getError()) {
		llvm::errs() << "failed to read Clangd docs JSON file\n";
		return;
	}

	auto Deserialized = llvm::json::parse(Buffer.get()->getBuffer());
	if (auto E = Deserialized.takeError()) {
		llvm::errs() << "failed to parse Clangd docs JSON file\n";
		return;
	}

	for (const auto &InstrVal : *Deserialized->getAsArray()) {
		const auto &Instr = deserializeInstruction(InstrVal);

		for (const auto &Encoding : Instr->Encodings) {
			for (const auto &Syntax : Encoding.Syntaxes) {
				if (InstructionMap.size() < Syntax.NumArgs + 1)
					InstructionMap.resize((Syntax.NumArgs + 1));

				InstructionMap[Syntax.NumArgs][llvm::StringRef(Syntax.Name).upper()] = Instr;
			}
		}

		for (const auto &Emnemonic : Instr->Emnemonics) {
			if (InstructionMap.size() < Emnemonic.NumArgs + 1)
				InstructionMap.resize((Emnemonic.NumArgs + 1));

			InstructionMap[Emnemonic.NumArgs][llvm::StringRef(Emnemonic.Name).upper()] = Instr;
		}
	}
}

} // namespace clangd

} // namespace clang
