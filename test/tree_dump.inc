// Canonical dump of what a BooleanTree produces for an input: the RPN queue, then the
// parse tree, or the exception (kind and message) from either stage. Included by two
// translation units (the current sources and the frozen original, whose class names
// are renamed by compile definitions) that define DUMP_FN differently and include
// the library headers first.
#include <sstream>
#include <string>
#include <string_view>


namespace {

void dump_node(BaseNode* p, std::string& out) {
	switch (p->getType()) {
		case NodeType::AND:
			out += "(AND ";
			dump_node(static_cast<AndNode*>(p)->getLeftNode(), out);
			out += ' ';
			dump_node(static_cast<AndNode*>(p)->getRightNode(), out);
			out += ')';
			break;
		case NodeType::MAYBE:
			out += "(MAYBE ";
			dump_node(static_cast<MaybeNode*>(p)->getLeftNode(), out);
			out += ' ';
			dump_node(static_cast<MaybeNode*>(p)->getRightNode(), out);
			out += ')';
			break;
		case NodeType::OR:
			out += "(OR ";
			dump_node(static_cast<OrNode*>(p)->getLeftNode(), out);
			out += ' ';
			dump_node(static_cast<OrNode*>(p)->getRightNode(), out);
			out += ')';
			break;
		case NodeType::XOR:
			out += "(XOR ";
			dump_node(static_cast<XorNode*>(p)->getLeftNode(), out);
			out += ' ';
			dump_node(static_cast<XorNode*>(p)->getRightNode(), out);
			out += ')';
			break;
		case NodeType::NOT:
			out += "(NOT ";
			dump_node(static_cast<NotNode*>(p)->getNode(), out);
			out += ')';
			break;
		case NodeType::ID: {
			auto id = static_cast<IdNode*>(p)->getId();
			out += std::to_string(id.size()) + ":" + id;
			break;
		}
	}
}

}  // namespace

std::string DUMP_FN(std::string_view input) {
	std::string out;
	try {
		BooleanTree tree(input);
		out += "size=" + std::to_string(tree.size()) + " empty=" + (tree.empty() ? "1" : "0") + "\n";
		if (!tree.empty()) {
			out += "back=" + std::to_string(static_cast<int>(tree.back().get_type())) + ":" + tree.back().get_lexeme() + "\n";
		}
		out += "RPN:";
		while (!tree.empty()) {
			const auto& t = tree.front();
			out += " " + std::to_string(static_cast<int>(t.get_type())) + "/" + std::to_string(t.get_lexeme().size()) + ":" + t.get_lexeme();
			tree.pop_front();
		}
		out += "\n";
	} catch (const LexicalException& e) {
		return out + "LexicalException: " + e.what();
	} catch (const SyntacticException& e) {
		return out + "SyntacticException: " + e.what();
	}
	try {
		BooleanTree tree(input);
		tree.Parse();
		out += "TREE: ";
		dump_node(tree.root.get(), out);
	} catch (const LexicalException& e) {
		out += std::string("LexicalException: ") + e.what();
	} catch (const SyntacticException& e) {
		out += std::string("SyntacticException: ") + e.what();
	}
	return out;
}

// Lex, build the RPN queue and the tree; returns the root node (for benchmarks).
const void* PARSE_FN(std::string_view input) {
	BooleanTree tree(input);
	tree.Parse();
	const void* root = tree.root.get();
	return root;
}
