#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "parser/parser.hh"

#include "util/common.hh"

namespace parser {

TokenList::TokenList(Lexer::Result tokens) : tokens_(tokens), current_(0) {}

Token const& TokenList::Current() const { return tokens_.at(current_); }

bool TokenList::Accept(Symbol symbol) {
  if (Current().symbol == symbol) {
    // Skip over the current symbol, unless we're at the end of file.
    if (symbol != kEOF) {
      Next();
    }
    return true;
  }
  return false;
}

bool TokenList::Next() {
  if (current_ != tokens_.size() - 1) {
    ++current_;
    return true;
  }
  return false;
}

bool TokenList::SkipOver(Symbol symbol, std::vector<Token>* output) {
  while (Current().symbol == symbol) {
    if (output != nullptr) {
      output->push_back(Current());
    }
    if (!Next()) {
      return false;
    }
  }
  return (Current().symbol != kEOF);
}

bool TokenList::SkipUntil(Symbol symbol, std::vector<Token>* output) {
  while (Current().symbol != symbol && Current().symbol != kEOF) {
    if (output != nullptr) {
      output->push_back(Current());
    }
    if (!Next()) {
      return false;
    }
  }
  return (Current().symbol != kEOF);
}

std::string TokenList::JoinSkipped(std::vector<Token> const& tokens) {
  static auto token_formatter = [](std::string* out, Token const& t) {
    absl::StrAppend(out, t.match);
  };
  return absl::StrJoin(tokens, "", token_formatter);
}

Parser::Parser(Lexer lexer, ParseCallback* entry_fn)
    : lexer_(lexer), parser_entry_fn_(entry_fn) {}

bool Parser::Parse(std::string const& buffer) const {
  Lexer::Result result;
  if (!lexer_.ProcessContents(buffer, &result)) {
    return false;
  }

  TokenList tokens{result};
  if (!(*parser_entry_fn_)(&tokens)) {
    return false;
  }

  // also verify the parser has consumed all content
  return (tokens.Current().symbol == kEOF);
}

}  // namespace parser
