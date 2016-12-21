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
#include "private/internationalization.hpp"
#include "private/utils.hpp"
#include "lexems.hpp"
#include "errorPosition.hpp"
#include <iostream>
#include <sstream>

using namespace strus;
using namespace strus::parser;

PatternMatcherProgramParser::PatternMatcherProgramParser(
		const PatternLexerInterface* crm,
		const PatternMatcherInterface* tpm,
		ErrorBufferInterface* errorhnd_)
	:m_errorhnd(errorhnd_)
	,m_patternMatcherOptionNames(tpm->getCompileOptions())
	,m_patternLexerOptionNames(crm->getCompileOptions())
	,m_patternMatcherOptions()
	,m_patternLexerOptions()
	,m_patternMatcher(tpm->createInstance())
	,m_patternLexer(crm->createInstance())
	,m_patternTermFeeder()
	,m_regexNameSymbolTab()
	,m_patternNameSymbolTab()
	,m_symbolRegexIdList()
	,m_unresolvedPatternNameSet()
{
	if (!m_patternMatcher.get() || !m_patternLexer.get()) throw strus::runtime_error("failed to create pattern matching structures to instrument");
}

PatternMatcherProgramParser::PatternMatcherProgramParser(
		const PatternTermFeederInterface* tfm,
		const PatternMatcherInterface* tpm,
		ErrorBufferInterface* errorhnd_)
	:m_errorhnd(errorhnd_)
	,m_patternMatcherOptionNames(tpm->getCompileOptions())
	,m_patternLexerOptionNames()
	,m_patternMatcherOptions()
	,m_patternLexerOptions()
	,m_patternMatcher(tpm->createInstance())
	,m_patternLexer()
	,m_patternTermFeeder(tfm->createInstance())
	,m_regexNameSymbolTab()
	,m_patternNameSymbolTab()
	,m_symbolRegexIdList()
	,m_unresolvedPatternNameSet()
{
	if (!m_patternMatcher.get() || !m_patternTermFeeder.get()) throw strus::runtime_error("failed to create pattern matching structures to instrument");
}

void PatternMatcherProgramParser::fetchResult( PatternMatcherProgram& result)
{
	std::vector<std::string> regexidmap;
	SymbolTable::const_inv_iterator si = m_regexNameSymbolTab.inv_begin(), se = m_regexNameSymbolTab.inv_end();
	for (; si != se; ++si) regexidmap.push_back( *si);
	result.init( m_patternLexer.release(), m_patternTermFeeder.release(), m_patternMatcher.release(), regexidmap, m_symbolRegexIdList);
}

bool PatternMatcherProgramParser::load( const std::string& source)
{
	char const* si = source.c_str();
	try
	{
		while (*si)
		{
			if (isPercent(*si))
			{
				//... we got a lexem match option or a token pattern match option
				(void)parse_OPERATOR( si);
				loadOption( si);
				continue;
			}
			bool visible = true;
			if (isDot(*si))
			{
				//... declare rule as invisible (private)
				(void)parse_OPERATOR( si);
				visible = false;
			}
			if (m_patternTermFeeder.get() && isAssign(*si))
			{
				(void)parse_OPERATOR( si);
				if (!isAlpha(*si)) throw strus::runtime_error(_TXT("feature type expected after colon ':' delaring an unused lexem that contributes only to position counting"));
				std::string name = parse_IDENTIFIER( si);
				(void)getAnalyzerTermType( name);

				if (!isSemiColon(*si))
				{
					throw strus::runtime_error(_TXT("semicolon ';' expected at end of rule"));
				}
				(void)parse_OPERATOR(si);
			}
			else if (isAlpha(*si) || isStringQuote(*si))
			{
				bool nameIsString = isStringQuote(*si);
				std::string name = nameIsString ? parse_STRING( si) : parse_IDENTIFIER( si);
				unsigned int level = 0;
				bool has_level = false;
				if (isExp(*si))
				{
					(void)parse_OPERATOR(si);
					level = parse_UNSIGNED( si);
					has_level = true;
				}
				if (m_patternLexer.get() && isColon( *si))
				{
					//... lexem expression declaration
					if (nameIsString)
					{
						throw strus::runtime_error(_TXT("string not allowed as lexem type"));
					}
					if (!visible)
					{
						throw strus::runtime_error(_TXT("unexpected colon ':' after dot '.' followed by an identifier, that starts an token pattern declaration marked as private (invisible in output)"));
					}
					unsigned int nameid = m_regexNameSymbolTab.getOrCreate( name);
					if (nameid == 0)
					{
						throw strus::runtime_error(_TXT("failed to define lexem name symbol"));
					}
					if (nameid > MaxRegularExpressionNameId)
					{
						throw strus::runtime_error(_TXT("too many regular expression tokens defined: %u"), nameid);
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
							throw strus::runtime_error(_TXT("regular expression definition (inside chosen characters) expected after colon ':'"));
						}
						unsigned int resultIndex = 0;
						if (isOpenSquareBracket(*si))
						{
							(void)parse_OPERATOR(si);
							resultIndex = parse_UNSIGNED( si);
							if (!isOpenSquareBracket(*si))
							{
								throw strus::runtime_error(_TXT("close square bracket ']' expected at end of result index definition"));
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
				else if (isAssign(*si))
				{
					if (has_level)
					{
						throw strus::runtime_error(_TXT("unsupported definition of level \"^N\" in token pattern definition"));
					}
					//... token pattern expression declaration
					unsigned int nameid = m_patternNameSymbolTab.getOrCreate( name);
					if (nameid == 0)
					{
						throw strus::runtime_error(_TXT("failed to define pattern name symbol"));
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
						m_patternMatcher->definePattern( name, visible);
					}
					while (isOr(*si));
				}
				else
				{
					throw strus::runtime_error(_TXT("assign '=' (token pattern definition) or colon ':' (lexem pattern definition) expected after name starting a pattern declaration"));
				}
				if (!isSemiColon(*si))
				{
					throw strus::runtime_error(_TXT("semicolon ';' expected at end of rule"));
				}
				(void)parse_OPERATOR(si);
			}
			else
			{
				throw strus::runtime_error(_TXT("identifier or string expected at start of rule"));
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
				if (!uidx) unresolved << ", ";
				unresolved << "'" << m_patternNameSymbolTab.key(*ui) << "'";
			}
			std::string unresolvedstr( unresolved.str());
			throw strus::runtime_error(_TXT("unresolved pattern references: %s"), unresolvedstr.c_str());
		}
		bool rt = true;
		rt &= m_patternMatcher->compile( m_patternMatcherOptions);
		rt &= m_patternLexer->compile( m_patternLexerOptions);
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
	if (m_patternLexer.get())
	{
		unsigned int id = m_patternLexer->getSymbol( regexid, name);
		if (!id)
		{
			m_symbolRegexIdList.push_back( regexid);
			id = m_symbolRegexIdList.size() + MaxRegularExpressionNameId;
			m_patternLexer->defineSymbol( id, regexid, name);
		}
		return id;
	}
	else if (m_patternTermFeeder.get())
	{
		unsigned int id = m_patternTermFeeder->getSymbol( regexid, name);
		if (!id)
		{
			m_symbolRegexIdList.push_back( regexid);
			id = m_symbolRegexIdList.size() + MaxRegularExpressionNameId;
			m_patternTermFeeder->defineSymbol( id, regexid, name);
		}
		return id;
	}
	else
	{
		throw strus::runtime_error(_TXT("internal: no lexer or term feeder defined"));
	}
}

const char* PatternMatcherProgramParser::getSymbolRegexId( unsigned int id) const
{
	return m_regexNameSymbolTab.key( m_symbolRegexIdList[ id - MaxRegularExpressionNameId -1]);
}

unsigned int PatternMatcherProgramParser::getAnalyzerTermType( const std::string& type)
{
	unsigned int typid = m_regexNameSymbolTab.getOrCreate( type);
	if (typid == 0)
	{
		throw strus::runtime_error(_TXT("failed to define term type symbol"));
	}
	if (typid > MaxRegularExpressionNameId)
	{
		throw strus::runtime_error(_TXT("too many term types defined: %u"), typid);
	}
	if (m_regexNameSymbolTab.isNew( typid))
	{
		m_patternTermFeeder->defineLexem( typid, type);
	}
	return typid;
}


void PatternMatcherProgramParser::loadExpressionNode( const std::string& name, char const*& si, SubExpressionInfo& exprinfo)
{
	exprinfo.minrange = 0;
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
					else if (exprinfo.maxrange > argexprinfo.maxrange)
					{
						exprinfo.maxrange = argexprinfo.maxrange;
					}
					break;
				case PatternMatcherInstanceInterface::OpAnd:
					if (exprinfo.minrange > argexprinfo.minrange)
					{
						exprinfo.minrange = argexprinfo.minrange;
					}
					else if (exprinfo.maxrange > argexprinfo.maxrange)
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
							throw strus::runtime_error(_TXT("unsigned integer expected as proximity range value after '|' in expression parameter list"));
						}
						range = parse_UNSIGNED( si);
					}
					else if (isExp( *si) && (mask & 0x02) == 0)
					{
						mask |= 0x02;
						(void)parse_OPERATOR( si);
						if (!is_UNSIGNED( si))
						{
							throw strus::runtime_error(_TXT("unsigned integer expected as cardinality value after '^' in expression parameter list"));
						}
						cardinality = parse_UNSIGNED( si);
					}
					else if (isComma(*si))
					{
						throw strus::runtime_error(_TXT("unexpected comma ',' after proximity range and/or cardinality specification than must only appear at the end of the arguments list"));
					}
				}
			}
		}
		while (isComma( *si));
		if (!isCloseOvalBracket( *si))
		{
			throw strus::runtime_error(_TXT("close bracket ')' expexted at end of join operation expression"));
		}
		(void)parse_OPERATOR( si);
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
		throw strus::runtime_error(_TXT("unexpected assignment operator '=', only one assignment allowed per node"));
	}
	else
	{
		unsigned int id = m_patternLexer.get()
				? m_regexNameSymbolTab.get( name)
				: getAnalyzerTermType( name);
		if (id)
		{
			if (isStringQuote(*si))
			{
				std::string symbol( parse_STRING( si));
				id = getOrCreateSymbol( id, symbol);
			}
			m_patternMatcher->pushTerm( id);
		}
		else
		{
			id = m_patternNameSymbolTab.get( name);
			if (!id)
			{
				id = m_patternNameSymbolTab.getOrCreate( name);
				if (id == 0)
				{
					throw strus::runtime_error(_TXT("failed to define pattern name symbol"));
				}
				m_unresolvedPatternNameSet.insert( id);
			}
			m_patternMatcher->pushPattern( name);
		}
		exprinfo.minrange = 1;
	}
}

void PatternMatcherProgramParser::loadExpression( char const*& si, SubExpressionInfo& exprinfo)
{
	std::string name = parse_IDENTIFIER( si);
	if (isAssign(*si))
	{
		(void)parse_OPERATOR( si);
		float weight = 1.0f;
		if (isOpenSquareBracket(*si))
		{
			(void)parse_OPERATOR( si);
			if (!is_FLOAT(si))
			{
				throw strus::runtime_error(_TXT("floating point number expected for variable assignment weight in square brackets '[' ']' after assignment operator"));
			}
			weight = parse_FLOAT( si);
			if (!isCloseSquareBracket(*si))
			{
				throw strus::runtime_error(_TXT("close square bracket expected after floating point number in variable assignment weight specification"));
			}
			(void)parse_OPERATOR( si);
		}
		std::string op = parse_IDENTIFIER( si);
		loadExpressionNode( op, si, exprinfo);
		m_patternMatcher->attachVariable( name, weight);
	}
	else
	{
		loadExpressionNode( name, si, exprinfo);
	}
}

void PatternMatcherProgramParser::loadOption( char const*& si)
{
	if (isAlpha(*si))
	{
		std::string name = parse_IDENTIFIER( si);
		std::vector<std::string>::const_iterator
			oi = m_patternMatcherOptionNames.begin(),
			oe = m_patternMatcherOptionNames.end();
		for (; oi != oe && !utils::caseInsensitiveEquals( name, *oi); ++oi){}

		if (oi != oe)
		{
			if (isAssign(*si))
			{
				(void)parse_OPERATOR( si);
				if (is_FLOAT(si))
				{
					double value = parse_FLOAT( si);
					m_patternMatcherOptions( name, value);
				}
				else
				{
					throw strus::runtime_error(_TXT("expected number as value of token pattern match option declaration"));
				}
			}
			else
			{
				throw strus::runtime_error(_TXT("expected assignment operator in token pattern match option declaration"));
			}
			return;
		}
		oi = m_patternLexerOptionNames.begin(),
		oe = m_patternLexerOptionNames.end();
		for (; oi != oe && !utils::caseInsensitiveEquals( name, *oi); ++oi){}

		if (oi != oe)
		{
			m_patternLexerOptions( name);
		}
		else
		{
			throw strus::runtime_error(_TXT("unknown option: '%s'"), name.c_str());
		}
	}
	else
	{
		throw strus::runtime_error(_TXT("identifier expected at start of option declaration"));
	}
}

