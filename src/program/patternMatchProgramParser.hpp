/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Implements the parser for a pattern match program
/// \file patternMatchProgramParser.hpp
#ifndef _STRUS_UTILITIES_PROGRAM_PATTERN_MATCH_PROGRAM_PARSER_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_PATTERN_MATCH_PROGRAM_PARSER_INCLUDED
#include "strus/patternMatcherInstanceInterface.hpp"
#include "strus/patternLexerInstanceInterface.hpp"
#include "strus/patternTermFeederInstanceInterface.hpp"
#include "strus/reference.hpp"
#include "strus/base/stdint.h"
#include "strus/base/symbolTable.hpp"
#include "strus/analyzer/patternMatcherOptions.hpp"
#include "strus/analyzer/patternLexerOptions.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>

/// \brief strus toplevel namespace
namespace strus {

/// \brief Forward declaration
class PatternLexerInterface;
/// \brief Forward declaration
class PatternTermFeederInterface;
/// \brief Forward declaration
class PatternMatcherInterface;
/// \brief Forward declaration
class ErrorBufferInterface;
/// \brief Forward declaration
class PatternMatcherProgram;

enum {MaxRegularExpressionNameId=(1<<24)};

class PatternMatcherProgramParser
{
public:
	/// \brief Constructor
	PatternMatcherProgramParser(
			const PatternLexerInterface* crm,
			const PatternMatcherInterface* tpm,
			ErrorBufferInterface* errorhnd_);

	/// \brief Constructor
	PatternMatcherProgramParser(
			const PatternTermFeederInterface* tfm,
			const PatternMatcherInterface* tpm,
			ErrorBufferInterface* errorhnd_);

	void fetchResult( PatternMatcherProgram& result);
	bool load( const std::string& source);
	bool compile();

private:
	struct SubExpressionInfo
	{
		explicit SubExpressionInfo( unsigned int minrange_=0,unsigned int maxrange_=0)
			:minrange(minrange_),maxrange(maxrange_){}
		SubExpressionInfo( const SubExpressionInfo& o)
			:minrange(o.minrange),maxrange(o.maxrange){}

		unsigned int minrange;
		unsigned int maxrange;
	};

	uint32_t getOrCreateSymbol( unsigned int regexid, const std::string& name);
	const char* getSymbolRegexId( unsigned int id) const;
	void defineAnalyzerTermType( const std::string& type);
	unsigned int getAnalyzerTermType( const std::string& type);
	void loadExpressionNode( const std::string& name, char const*& si, SubExpressionInfo& exprinfo);
	void loadExpression( char const*& si, SubExpressionInfo& exprinfo);
	void loadOption( char const*& si);

private:
	ErrorBufferInterface* m_errorhnd;
	std::vector<std::string> m_patternMatcherOptionNames;
	std::vector<std::string> m_patternLexerOptionNames;
	analyzer::PatternMatcherOptions m_patternMatcherOptions;
	analyzer::PatternLexerOptions m_patternLexerOptions;
	Reference<PatternMatcherInstanceInterface> m_patternMatcher;
	Reference<PatternLexerInstanceInterface> m_patternLexer;
	Reference<PatternTermFeederInstanceInterface> m_patternTermFeeder;
	SymbolTable m_regexNameSymbolTab;
	SymbolTable m_patternNameSymbolTab;
	std::vector<uint32_t> m_symbolRegexIdList;
	std::set<uint32_t> m_unresolvedPatternNameSet;
};

}//namespace
#endif

