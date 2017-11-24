/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Implements the parser for a pattern match program
/// \file patternMatchProgramParser.hpp
#include "patternMatchProgramParser.hpp"
#include "strus/programLoader.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/patternMatcherInterface.hpp"
#include "strus/patternLexerInterface.hpp"
#include "strus/patternTermFeederInterface.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/utf8.hpp"
#include "private/internationalization.hpp"
#include "private/utils.hpp"
#include "lexems.hpp"
#include "errorPosition.hpp"
#include <iostream>
#include <sstream>

using namespace strus;
using namespace strus::parser;

PatternMatcherProgramParser::PatternMatcherProgramParser(
		PatternLexerInstanceInterface* crm,
		PatternMatcherInstanceInterface* tpm,
		ErrorBufferInterface* errorhnd_)
	:m_errorhnd(errorhnd_)
	,m_patternMatcher(tpm)
	,m_patternLexer(crm)
	,m_patternTermFeeder(0)
	,m_regexNameSymbolTab()
	,m_patternNameSymbolTab()
	,m_lexemSymbolTab()
	,m_patternLengthMap()
	,m_symbolRegexIdList()
	,m_unresolvedPatternNameSet()
{
	if (!m_patternMatcher || !m_patternLexer) throw strus::runtime_error( "%s", "failed to create pattern matching structures to instrument");
}

PatternMatcherProgramParser::PatternMatcherProgramParser(
		PatternTermFeederInstanceInterface* tfm,
		PatternMatcherInstanceInterface* tpm,
		ErrorBufferInterface* errorhnd_)
	:m_errorhnd(errorhnd_)
	,m_patternMatcher(tpm)
	,m_patternLexer(0)
	,m_patternTermFeeder(tfm)
	,m_regexNameSymbolTab()
	,m_patternNameSymbolTab()
	,m_lexemSymbolTab()
	,m_symbolRegexIdList()
	,m_unresolvedPatternNameSet()
{
	if (!m_patternMatcher || !m_patternTermFeeder) throw strus::runtime_error( "%s", "failed to create pattern matching structures to instrument");
}

bool PatternMatcherProgramParser::load( const std::string& source)
{
	char const* si = source.c_str();
	try
	{
		skipSpaces( si);
		while (*si)
		{
			if (isPercent(*si))
			{
				//... we got a lexem match option or a token pattern match option
				(void)parse_OPERATOR( si);
				if (!isAlpha(*si))
				{
					throw strus::runtime_error( "%s", _TXT("expected key word 'LEXER' or 'MATCHER' after percent '%' (option)"));
				}
				unsigned int dupf = 0;
				int id = parse_KEYWORD( dupf, si, 3, "LEXER", "MATCHER", "FEEDER");
				if (id < 0)
				{
					throw strus::runtime_error( "%s", _TXT("expected key word 'LEXER' or 'MATCHER' after percent '%' (option)"));
				}
				else if (id == 0)
				{
					if (!m_patternLexer)
					{
						throw strus::runtime_error( "%s", _TXT("defined 'LEXER' option without lexer defined"));
					}
					for (;;)
					{
						loadLexerOption( si);
						if (!isComma(*si)) break;
						parse_OPERATOR( si);
					}
				}
				else if (id == 1)
				{
					for (;;)
					{
						loadMatcherOption( si);
						if (!isComma(*si)) break;
						parse_OPERATOR( si);
					}
				}
				else
				{
					if (!m_patternTermFeeder)
					{
						throw strus::runtime_error( "%s", _TXT("defined 'FEEDER' option without feeder defined"));
					}
					for (;;)
					{
						loadFeederOption( si);
						if (!isComma(*si)) break;
						parse_OPERATOR( si);
					}
				}
				continue;
			}
			bool visible = true;
			if (isDot(*si))
			{
				//... declare rule as invisible (private)
				(void)parse_OPERATOR( si);
				visible = false;
			}
			if (isAlpha(*si) || isStringQuote(*si))
			{
				bool nameIsString = isStringQuote(*si);
				std::string name = nameIsString ? parse_STRING( si) : parse_IDENTIFIER( si);
				if (name.empty())
				{
					throw strus::runtime_error( "%s", _TXT("pattern name is empty"));
				}
				unsigned int level = 0;
				bool has_level = false;
				if (isExp(*si))
				{
					(void)parse_OPERATOR(si);
					level = parse_UNSIGNED( si);
					has_level = true;
				}
				if (isColon( *si))
				{
					if (m_patternLexer)
					{
						//... lexem expression declaration
						if (nameIsString)
						{
							throw strus::runtime_error( "%s", _TXT("string not allowed as lexem type"));
						}
						if (!visible)
						{
							throw strus::runtime_error( "%s", _TXT("unexpected colon ':' after dot '.' followed by an identifier, that starts an token pattern declaration marked as private (invisible in output)"));
						}
						unsigned int nameid = m_regexNameSymbolTab.getOrCreate( name);
						if (nameid == 0)
						{
							throw strus::runtime_error( "%s", _TXT("failed to define lexem name symbol"));
						}
						if (nameid >= MaxPatternTermNameId)
						{
							throw strus::runtime_error(_TXT("too many regular expression tokens defined: %u"), nameid);
						}
						if (m_regexNameSymbolTab.isNew())
						{
							m_patternLexer->defineLexemName( nameid, name);
						}
						std::string regex;
						do
						{
							(void)parse_OPERATOR(si);
	
							//... lexem pattern def -> name : regex ;
							if ((unsigned char)*si > 32)
							{
								regex = parse_REGEX( si);
							}
							else
							{
								throw strus::runtime_error( "%s", _TXT("regular expression definition (inside chosen characters) expected after colon ':'"));
							}
							if (isTilde(*si) && isDigit(*(si+1)))
							{
								//... edit distance operator "~1","~2",....
								regex.push_back(*si);
								for (++si; isDigit( *si); ++si)
								{
									regex.push_back(*si);
								}
							}
							unsigned int resultIndex = 0;
							if (isOpenSquareBracket(*si))
							{
								(void)parse_OPERATOR(si);
								resultIndex = parse_UNSIGNED( si);
								if (!isCloseSquareBracket(*si))
								{
									throw strus::runtime_error( "%s", _TXT("close square bracket ']' expected at end of result index definition"));
								}
								(void)parse_OPERATOR(si);
							}
							analyzer::PositionBind posbind = analyzer::BindContent;
							if (isLeftArrow(si))
							{
								si += 2; skipSpaces( si); //....parse_OPERATOR
								posbind = analyzer::BindPredecessor;
							}
							else if (isRightArrow(si))
							{
								si += 2; skipSpaces( si); //....parse_OPERATOR
								posbind = analyzer::BindSuccessor;
							}
							m_patternLexer->defineLexem(
								nameid, regex, resultIndex, level, posbind);
						}
						while (isOr(*si));
					}
					else if (m_patternTermFeeder)
					{
						throw strus::runtime_error( "%s", _TXT("pattern analyzer terms are defined by option %%lexem type and not with id : regex"));
					}
				}
				else if (isAssign(*si))
				{
					if (has_level)
					{
						throw strus::runtime_error( "%s", _TXT("unsupported definition of level \"^N\" in token pattern definition"));
					}
					//... token pattern expression declaration
					unsigned int nameid = m_patternNameSymbolTab.getOrCreate( name);
					if (nameid == 0)
					{
						throw strus::runtime_error( "%s", _TXT("failed to define pattern name symbol"));
					}
					do
					{
						//... Token pattern def -> name = expression ;
						(void)parse_OPERATOR(si);
						SubExpressionInfo exprinfo;
						loadExpression( si, exprinfo);
						std::set<uint32_t>::iterator ui = m_unresolvedPatternNameSet.find( nameid);
						if (ui != m_unresolvedPatternNameSet.end())
						{
							m_unresolvedPatternNameSet.erase( ui);
						}
						std::map<uint32_t,unsigned int>::iterator li = m_patternLengthMap.find( nameid);
						if (li != m_patternLengthMap.end())
						{
							li->second = std::max( li->second, exprinfo.maxrange);
						}
						else
						{
							m_patternLengthMap[ nameid] = exprinfo.maxrange;
						}
						m_patternMatcher->definePattern( name, visible);
					}
					while (isOr(*si));
				}
				else
				{
					throw strus::runtime_error( "%s", _TXT("assign '=' (token pattern definition) or colon ':' (lexem pattern definition) expected after name starting a pattern declaration"));
				}
				if (!isSemiColon(*si))
				{
					throw strus::runtime_error( "%s", _TXT("semicolon ';' expected at end of rule"));
				}
				(void)parse_OPERATOR(si);
				if (m_errorhnd->hasError())
				{
					throw strus::runtime_error( "%s", _TXT("error in rule definition"));
				}
			}
			else
			{
				throw strus::runtime_error( "%s", _TXT("identifier or string expected at start of rule"));
			}
		}
		return true;
	}
	catch (const std::runtime_error& err)
	{
		enum {MaxErrorSnippetLen=20};
		const char* se = (const char*)std::memchr( si, '\0', MaxErrorSnippetLen);
		std::size_t snippetSize = (!se)?MaxErrorSnippetLen:(se - si);
		char snippet[ MaxErrorSnippetLen+1];
		std::memcpy( snippet, si, snippetSize);
		snippet[ snippetSize] = 0;
		std::size_t ii=0;
		for (; snippet[ii]; ++ii)
		{
			if ((unsigned char)snippet[ii] < 32)
			{
				snippet[ii] = ' ';
			}
		}
		ErrorPosition errpos( source.c_str(), si);
		m_errorhnd->report( _TXT("error in pattern match program %s: \"%s\" [at '%s']"), errpos.c_str(), err.what(), snippet);
		return false;
	}
	catch (const std::bad_alloc&)
	{
		m_errorhnd->report( _TXT("out of memory when loading program source"));
		return false;
	}
}

bool PatternMatcherProgramParser::compile()
{
	try
	{
		if (m_errorhnd->hasError())
		{
			m_errorhnd->explain( _TXT("error before compile (while building program): %s"));
			return false;
		}
		if (!m_unresolvedPatternNameSet.empty())
		{
			std::ostringstream unresolved;
			std::set<uint32_t>::iterator
				ui = m_unresolvedPatternNameSet.begin(),
				ue = m_unresolvedPatternNameSet.end();
			for (std::size_t uidx=0; ui != ue && uidx<10; ++ui,++uidx)
			{
				m_warnings.push_back( string_format( _TXT("unresolved pattern reference '%s'"), m_patternNameSymbolTab.key(*ui)));
			}
		}
		bool rt = true;
		rt &= m_patternMatcher->compile();
		if (m_patternLexer)
		{
			rt &= m_patternLexer->compile();
		}
		return rt;
	}
	catch (const std::runtime_error& e)
	{
		m_errorhnd->report( _TXT("failed to compile pattern match program source: %s"), e.what());
		return false;
	}
	catch (const std::bad_alloc&)
	{
		m_errorhnd->report( _TXT("out of memory when compiling pattern match program source"));
		return false;
	}
}

typedef strus::PatternMatcherInstanceInterface::JoinOperation JoinOperation;
static JoinOperation joinOperation( const std::string& name)
{
	static const char* ar[] = {"sequence","sequence_imm","sequence_struct","within","within_struct","any","and",0};
	std::size_t ai = 0;
	for (; ar[ai]; ++ai)
	{
		if (utils::caseInsensitiveEquals( name, ar[ai]))
		{
			return (JoinOperation)ai;
		}
	}
	throw strus::runtime_error( _TXT("unknown join operation: '%s'"), name.c_str());
}

uint32_t PatternMatcherProgramParser::getOrCreateSymbol( unsigned int regexid, const std::string& name)
{
	char regexidbuf[ 16];
	std::size_t regexidsize = utf8encode( regexidbuf, regexid+1);
	std::string symkey( regexidbuf, regexidsize);
	symkey.append( name);
	uint32_t symid = m_lexemSymbolTab.getOrCreate( symkey) + MaxPatternTermNameId;
	if (m_lexemSymbolTab.isNew())
	{
		m_symbolRegexIdList.push_back( regexid);
		if ((std::size_t)( symid - MaxPatternTermNameId) != m_symbolRegexIdList.size())
		{
			throw strus::runtime_error( "%s", _TXT("internal: inconsisteny in lexem symbol map"));
		}
		if (m_patternLexer)
		{
			m_patternLexer->defineSymbol( symid, regexid, name);
			m_patternLexer->defineLexemName( symid, name);
		}
		else if (m_patternTermFeeder)
		{
			m_patternTermFeeder->defineSymbol( symid, regexid, name);
		}
		else
		{
			throw strus::runtime_error( "%s", _TXT("internal: no lexer or term feeder defined"));
		}
	}
	return symid;
}

const char* PatternMatcherProgramParser::getSymbolRegexId( unsigned int id) const
{
	const char* symkey = m_lexemSymbolTab.key( id);
	unsigned int symkeyhdrlen = utf8charlen( *symkey);
	if (!symkeyhdrlen) throw strus::runtime_error( "%s", _TXT("illegal key in pattern lexem symbol table"));
	int regexid = utf8decode( symkey, symkeyhdrlen) - 1;
	return m_regexNameSymbolTab.key( regexid);
}

unsigned int PatternMatcherProgramParser::defineAnalyzerTermType( const std::string& type)
{
	unsigned int typid = m_regexNameSymbolTab.getOrCreate( type);
	if (typid == 0)
	{
		throw strus::runtime_error( "%s", _TXT("failed to define term type symbol"));
	}
	if (typid >= MaxPatternTermNameId)
	{
		throw strus::runtime_error(_TXT("too many term types defined: %u"), typid);
	}
	if (m_regexNameSymbolTab.isNew())
	{
		m_patternTermFeeder->defineLexem( typid, type);
	}
	return typid;
}

unsigned int PatternMatcherProgramParser::getAnalyzerTermType( const std::string& type) const
{
	return m_regexNameSymbolTab.get( type);
}


void PatternMatcherProgramParser::loadExpressionNode( const std::string& name, char const*& si, SubExpressionInfo& exprinfo)
{
	exprinfo.minrange = 0;
	exprinfo.maxrange = 0;
	if (isOpenOvalBracket( *si))
	{
		JoinOperation operation = joinOperation( name);

		unsigned int cardinality = 0;
		unsigned int range = 0;
		unsigned int nofArguments = 0;
		char const* lookahead = si;
		(void)parse_OPERATOR( lookahead);

		if (isCloseOvalBracket( *lookahead))
		{
			si = lookahead;
		}
		else do
		{
			(void)parse_OPERATOR( si);
			SubExpressionInfo argexprinfo;
			loadExpression( si, argexprinfo);
			switch (operation)
			{
				case PatternMatcherInstanceInterface::OpSequence:
					exprinfo.minrange += argexprinfo.minrange;
					exprinfo.maxrange += argexprinfo.maxrange;
					break;
				case PatternMatcherInstanceInterface::OpSequenceImm:
					exprinfo.minrange += argexprinfo.minrange;
					exprinfo.maxrange += argexprinfo.maxrange;
					break;
				case PatternMatcherInstanceInterface::OpSequenceStruct:
					if (nofArguments)
					{
						exprinfo.minrange += argexprinfo.minrange;
						exprinfo.maxrange += argexprinfo.maxrange;
					}
					break;
				case PatternMatcherInstanceInterface::OpWithin:
					exprinfo.minrange += argexprinfo.minrange;
					exprinfo.maxrange += argexprinfo.maxrange;
					break;
				case PatternMatcherInstanceInterface::OpWithinStruct:
					if (nofArguments)
					{
						exprinfo.minrange += argexprinfo.minrange;
						exprinfo.maxrange += argexprinfo.maxrange;
					}
					break;
				case PatternMatcherInstanceInterface::OpAny:
					if (nofArguments == 0 || exprinfo.minrange < argexprinfo.minrange)
					{
						exprinfo.minrange = argexprinfo.minrange;
					}
					if (nofArguments == 0 || exprinfo.maxrange < argexprinfo.maxrange)
					{
						exprinfo.maxrange = argexprinfo.maxrange;
					}
					break;
				case PatternMatcherInstanceInterface::OpAnd:
					if (nofArguments == 0 || exprinfo.minrange > argexprinfo.minrange)
					{
						exprinfo.minrange = argexprinfo.minrange;
					}
					if (nofArguments == 0 || exprinfo.maxrange < argexprinfo.maxrange)
					{
						exprinfo.maxrange = argexprinfo.maxrange;
					}
					break;
			}
			++nofArguments;
			if (isOr( *si) || isExp( *si))
			{
				unsigned int mask = 0;
				while (isOr( *si) || isExp( *si))
				{
					if (isOr( *si) && (mask & 0x01) == 0)
					{
						mask |= 0x01;
						(void)parse_OPERATOR( si);
						if (!is_UNSIGNED( si))
						{
							throw strus::runtime_error( "%s", _TXT("unsigned integer expected as proximity range value after '|' in expression parameter list"));
						}
						range = parse_UNSIGNED( si);
					}
					else if (isExp( *si) && (mask & 0x02) == 0)
					{
						mask |= 0x02;
						(void)parse_OPERATOR( si);
						if (!is_UNSIGNED( si))
						{
							throw strus::runtime_error( "%s", _TXT("unsigned integer expected as cardinality value after '^' in expression parameter list"));
						}
						cardinality = parse_UNSIGNED( si);
					}
					else if (isComma(*si))
					{
						throw strus::runtime_error( "%s", _TXT("unexpected comma ',' after proximity range and/or cardinality specification than must only appear at the end of the arguments list"));
					}
				}
			}
		}
		while (isComma( *si));
		if (!isCloseOvalBracket( *si))
		{
			throw strus::runtime_error( "%s", _TXT("close bracket ')' expected at end of join operation expression"));
		}
		(void)parse_OPERATOR( si);
		if (range == 0 && exprinfo.maxrange == 0)
		{
			throw strus::runtime_error( "%s", _TXT("cannot evaluate length of expression, range has to be specified here"));
		}
		switch (operation)
		{
			case PatternMatcherInstanceInterface::OpSequenceImm:
				if (range == 0)
				{
					range = exprinfo.minrange;
				}
				else if (range < exprinfo.minrange)
				{
					throw strus::runtime_error(_TXT("rule cannot match in such a within such a small position range span: %u (required %u)"), range, exprinfo.minrange);
				}
				break;
			case PatternMatcherInstanceInterface::OpSequence:
			case PatternMatcherInstanceInterface::OpSequenceStruct:
			case PatternMatcherInstanceInterface::OpWithin:
			case PatternMatcherInstanceInterface::OpWithinStruct:
			case PatternMatcherInstanceInterface::OpAny:
			case PatternMatcherInstanceInterface::OpAnd:
				if (range == 0)
				{
					range = exprinfo.maxrange;
				}
				else if (range < exprinfo.minrange)
				{
					throw strus::runtime_error(_TXT("rule cannot match in such a small position range span specified: %u (required %u)"), range, exprinfo.minrange);
				}
				break;
		}
		m_patternMatcher->pushExpression( operation, nofArguments, range, cardinality);
	}
	else if (isAssign(*si))
	{
		throw strus::runtime_error( "%s", _TXT("unexpected assignment operator '=', only one assignment allowed per node"));
	}
	else
	{
		unsigned int id;
		if (isStringQuote(*si))
		{
			if (m_patternLexer)
			{
				id = m_regexNameSymbolTab.get( name);
				if (!id) throw strus::runtime_error(_TXT("undefined lexem '%s'"), name.c_str());
			}
			else
			{
				id = defineAnalyzerTermType( name);
			}
			std::string symbol( parse_STRING( si));
			id = getOrCreateSymbol( id, symbol);
			if (id)
			{
				m_patternMatcher->pushTerm( id);
				exprinfo.minrange = 1;
				exprinfo.maxrange = 1;
			}
		}
		else
		{
			if (m_patternLexer)
			{
				id = m_regexNameSymbolTab.get( name);
			}
			else
			{
				id = getAnalyzerTermType( name);
			}
			if (id)
			{
				m_patternMatcher->pushTerm( id);
				exprinfo.minrange = 1;
				exprinfo.maxrange = 1;
			}
			else
			{
				id = m_patternNameSymbolTab.get( name);
				if (!id)
				{
					id = m_patternNameSymbolTab.getOrCreate( name);
					if (id == 0)
					{
						throw strus::runtime_error( "%s", _TXT("failed to define pattern name symbol"));
					}
					m_unresolvedPatternNameSet.insert( id);
					exprinfo.minrange = 0;
					exprinfo.maxrange = 0;
				}
				else
				{
					std::map<uint32_t,unsigned int>::const_iterator li = m_patternLengthMap.find( id);
					if (li == m_patternLengthMap.end())
					{
						throw strus::runtime_error( "%s",  _TXT("cannot evaluate length of pattern"));
					}
					exprinfo.minrange = li->second;
					exprinfo.maxrange = li->second;
				}
				m_patternMatcher->pushPattern( name);
			}
		}
	}
}

void PatternMatcherProgramParser::loadExpression( char const*& si, SubExpressionInfo& exprinfo)
{
	std::string name = parse_IDENTIFIER( si);
	if (name.empty())
	{
		throw strus::runtime_error( "%s", _TXT("name in expression is empty"));
	}
	if (isAssign(*si))
	{
		(void)parse_OPERATOR( si);
		if (!isAlpha(*si))
		{
			throw strus::runtime_error( "%s", _TXT("expected variable after assign '='"));
		}
		std::string op = parse_IDENTIFIER( si);
		loadExpressionNode( op, si, exprinfo);
		m_patternMatcher->attachVariable( name);
	}
	else
	{
		loadExpressionNode( name, si, exprinfo);
	}
}

void PatternMatcherProgramParser::loadMatcherOption( char const*& si)
{
	if (isAlpha(*si))
	{
		std::string name = parse_IDENTIFIER( si);
		if (isAssign(*si))
		{
			(void)parse_OPERATOR( si);
			if (is_INTEGER(si) || is_FLOAT(si))
			{
				double value = parse_FLOAT( si);
				m_patternMatcher->defineOption( name, value);
			}
			else
			{
				throw strus::runtime_error( "%s", _TXT("expected number as matcher option value after assign"));
			}
		}
		else
		{
			m_patternMatcher->defineOption( name, 0.0);
		}
	}
	else
	{
		throw strus::runtime_error( "%s", _TXT("identifier expected at start of pattern matcher option declaration"));
	}
}

void PatternMatcherProgramParser::loadLexerOption( char const*& si)
{
	if (isAlpha(*si))
	{
		std::string name = parse_IDENTIFIER( si);
		m_patternLexer->defineOption( name, 0);
	}
	else
	{
		throw strus::runtime_error( "%s", _TXT("identifier expected at start of pattern lexer option declaration"));
	}
}

void PatternMatcherProgramParser::loadFeederOption( char const*& si)
{
	if (isAlpha(*si))
	{
		std::string name = parse_IDENTIFIER( si);
		if (isEqual(name,"lexem"))
		{
			if (!isAlpha(*si))
			{
				throw strus::runtime_error( "%s", _TXT("identifier expected as argument of feeder option 'lexem'"));
			}
			std::string lexemid( parse_IDENTIFIER( si));
			(void)defineAnalyzerTermType( lexemid);
		}
		else
		{
			throw strus::runtime_error(_TXT("unknown feeder option '%s'"), name.c_str());
		}
	}
	else
	{
		throw strus::runtime_error( "%s", _TXT("option name expected at start of pattern feeder option declaration"));
	}
}

