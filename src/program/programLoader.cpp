/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2013,2014 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#include "strus/programLoader.hpp"
#include "lexems.hpp"
#include "dll_tags.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/weightingConfigInterface.hpp"
#include "strus/summarizerConfigInterface.hpp"
#include "strus/summarizerFunctionInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/analyzer/term.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>

using namespace strus;
using namespace strus::parser;

static std::string errorPosition( const char* base, const char* itr)
{
	unsigned int line = 1;
	unsigned int col = 1;
	std::ostringstream msg;

	for (unsigned int ii=0,nn=itr-base; ii < nn; ++ii)
	{
		if (base[ii] == '\n')
		{
			col = 1;
			++line;
		}
		else
		{
			++col;
		}
	}
	msg << "at line " << line << " column " << col;
	return msg.str();
}

static void parseTermConfig(
		QueryEvalInterface& qeval,
		char const*& src)
{
	if (isAlpha(*src))
	{
		std::string termset = boost::algorithm::to_lower_copy( parse_IDENTIFIER( src));
		std::string termvalue;
		std::string termtype;

		if (isStringQuote( *src))
		{
			termvalue = parse_STRING( src);
		}
		else if (isAlpha( *src))
		{
			termvalue = parse_IDENTIFIER( src);
		}
		else
		{
			throw std::runtime_error( "term value (string,identifier,number) after the feature group identifier");
		}
		if (!isColon( *src))
		{
			throw std::runtime_error( "colon (':') expected after term value");
		}
		(void)parse_OPERATOR(src);
		if (!isAlpha( *src))
		{
			throw std::runtime_error( "term type identifier expected after colon and term value");
		}
		termtype = boost::algorithm::to_lower_copy( parse_IDENTIFIER( src));
		qeval.defineTerm( termset, termtype, termvalue);
	}
	else
	{
		throw std::runtime_error( "feature set identifier expected as start of a term declaration in the query");
	}
}

static bool isMember( const char** set, const std::string& name)
{
	if (!set) return false;
	std::size_t sidx = 0;
	for (; set[sidx]; ++sidx)
	{
		if (boost::algorithm::iequals( name, set[sidx]))
		{
			return true;
		}
	}
	return false;
}

static ArithmeticVariant parseNumericValue( char const*& src)
{
	if (is_INTEGER(src))
	{
		if (isMinus(*src))
		{
			return ArithmeticVariant( parse_INTEGER( src));
		}
		else
		{
			return ArithmeticVariant( parse_UNSIGNED( src));
		}
	}
	else
	{
		return ArithmeticVariant( parse_FLOAT( src));
	}
}

static void parseWeightingConfig(
		QueryEvalInterface& qeval,
		char const*& src)
{
	if (!isAlpha( *src))
	{
		throw std::runtime_error( "weighting function identifier expected");
	}
	boost::scoped_ptr<WeightingConfigInterface> wcfg(
		qeval.createWeightingConfig( parse_IDENTIFIER( src)));

	if (!isOpenOvalBracket( *src))
	{
		throw std::runtime_error( "open oval bracket '(' expected after weighting function identifier");
	}
	(void)parse_OPERATOR(src);

	if (!isCloseOvalBracket( *src)) for (;;)
	{
		if (!isAlpha( *src))
		{
			throw std::runtime_error( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected");
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw std::runtime_error( "assingment operator '=' expected after weighting function parameter name");
		}
		(void)parse_OPERATOR(src);
		wcfg->defineNumericParameter( parameterName, parseNumericValue( src));

		if (!isComma( *src))
		{
			break;
		}
		(void)parse_OPERATOR(src);
	}
	if (!isCloseOvalBracket( *src))
	{
		throw std::runtime_error( "close oval bracket ')' expected at end of weighting function parameter list");
	}
	(void)parse_OPERATOR(src);
	wcfg->done();
}


static void parseFeatureSets(
		QueryEvalInterface& qeval,
		char const*& src)
{
	enum QueryEvalKeyword {e_ON, e_WITH};

	while (!isSemiColon(*src))
	{
		switch ((QueryEvalKeyword)parse_KEYWORD( src, 2, "ON", "WITH"))
		{
			case e_ON:
				while (*src)
				{
					qeval.defineSelectorFeature( parse_IDENTIFIER(src));
					if (isComma( *src))
					{
						(void)parse_OPERATOR( src);
					}
					else
					{
						break;
					}
				}
				break;
			case e_WITH:
				while (*src)
				{
					qeval.defineWeightingFeature( parse_IDENTIFIER(src));
					if (isComma( *src))
					{
						(void)parse_OPERATOR( src);
					}
					else
					{
						break;
					}
				}
				break;
		}
	}
}


static void parseSummarizerConfig(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface& qproc,
		char const*& src)
{
	std::string functionName;
	std::string resultAttribute;

	if (!isAlpha( *src))
	{
		throw std::runtime_error( "name of result attribute expected after SUMMARIZE");
	}
	resultAttribute = parse_IDENTIFIER( src);
	if (!isAssign(*src))
	{
		throw std::runtime_error( "assignment operator '=' expected after the name of result attribute in summarizer definition");
	}
	(void)parse_OPERATOR( src);
	if (!isAlpha( *src))
	{
		throw std::runtime_error( "name of summarizer function expected after assignment in summarizer definition");
	}
	functionName = boost::algorithm::to_lower_copy( parse_IDENTIFIER( src));
	boost::scoped_ptr<SummarizerConfigInterface> summarizer(
		qeval.createSummarizerConfig( resultAttribute, functionName));
	const SummarizerFunctionInterface* function = qproc.getSummarizerFunction( functionName);

	if (!isOpenOvalBracket( *src))
	{
		throw std::runtime_error( "open oval bracket '(' expected after summarizer function identifier");
	}
	(void)parse_OPERATOR(src);

	if (!isCloseOvalBracket( *src)) for (;;)
	{
		if (!isAlpha( *src))
		{
			throw std::runtime_error( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected");
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw std::runtime_error( "assignment operator '=' expected after summarizer function parameter name");
		}
		(void)parse_OPERATOR(src);
		if (isStringQuote(*src) || isAlpha(*src))
		{
			std::string parameterValue;
			if (isStringQuote(*src))
			{
				parameterValue = parse_STRING( src);
			}
			else
			{
				parameterValue = parse_IDENTIFIER( src);
			}
			if (isMember( function->textualParameterNames(), parameterName))
			{
				summarizer->defineTextualParameter( parameterName, parameterValue);
			}
			else if (isMember( function->featureParameterClassNames(), parameterName))
			{
				summarizer->defineFeatureParameter( parameterName, parameterValue);
			}
			else if (isMember( function->numericParameterNames(), parameterName))
			{
				char const* cc = parameterValue.c_str();
				summarizer->defineNumericParameter( parameterName, parseNumericValue( cc));
			}
			else
			{
				throw std::runtime_error( std::string( "unknown summarizer function parameter name '") + parameterName + "'");
			}
		}
		else
		{
			if (isMember( function->textualParameterNames(), parameterName))
			{
				throw std::runtime_error( std::string( "string or identifier expected as value of summarizer function textual parameter '") + parameterName + "'");
			}
			else if (isMember( function->featureParameterClassNames(), parameterName))
			{
				throw std::runtime_error( std::string( "string or identifier expected as value of summarizer function feature parameter '") + parameterName + "'");
			}
			else if (isMember( function->numericParameterNames(), parameterName))
			{
				summarizer->defineNumericParameter( parameterName, parseNumericValue( src));
			}
			else
			{
				throw std::runtime_error( std::string( "unknown summarizer function parameter name '") + parameterName + "'");
			}
		}
		if (!isComma( *src))
		{
			break;
		}
		(void)parse_OPERATOR(src);
	}
	if (!isCloseOvalBracket( *src))
	{
		throw std::runtime_error( "close oval bracket ')' expected at end of summarizer function parameter list");
	}
	(void)parse_OPERATOR(src);
	summarizer->done();
}


DLL_PUBLIC void strus::loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface& qproc,
		const std::string& source)
{
	char const* src = source.c_str();
	enum StatementKeyword {e_EVAL, e_TERM, e_SUMMARIZE};
	std::string id;

	skipSpaces( src);
	try
	{
		while (*src)
		{
			switch ((StatementKeyword)parse_KEYWORD( src, 3, "EVAL", "TERM", "SUMMARIZE"))
			{
				case e_TERM:
					parseTermConfig( qeval, src);
					break;
				case e_EVAL:
					parseWeightingConfig( qeval, src);
					parseFeatureSets( qeval, src);
					break;
				case e_SUMMARIZE:
					parseSummarizerConfig( qeval, qproc, src);
					break;
			}
			if (*src)
			{
				if (!isSemiColon(*src))
				{
					throw std::runtime_error("semicolon expected as delimiter of query eval program instructions");
				}
				(void)parse_OPERATOR( src);
			}
		}
	}
	catch (const std::runtime_error& e)
	{
		throw std::runtime_error(
			std::string( "error in query evaluation program ")
			+ errorPosition( source.c_str(), src)
			+ ":" + e.what());
	}
}

enum FeatureClass
{
	FeatSearchIndexTerm,
	FeatForwardIndexTerm,
	FeatMetaData,
	FeatAttribute
};

static FeatureClass featureClassFromName( const std::string& name)
{
	if (isEqual( name, "SearchIndex"))
	{
		return FeatSearchIndexTerm;
	}
	if (isEqual( name, "ForwardIndex"))
	{
		return FeatForwardIndexTerm;
	}
	if (isEqual( name, "MetaData"))
	{
		return FeatMetaData;
	}
	if (isEqual( name, "Attribute"))
	{
		return FeatAttribute;
	}
	throw std::runtime_error( std::string( "illegal feature class name '") + name + " (expected one of {SearchIndex, ForwardIndex, MetaData, Attribute})");
}

static std::vector<std::string> parseArgumentList( char const*& src)
{
	std::vector<std::string> rt;
	while (*src)
	{
		std::string value;
		if (isAlpha(*src))
		{
			value = parse_IDENTIFIER( src);
		}
		else if (isDigit(*src) || isMinus(*src))
		{
			char const* src_bk = src;
			if (isMinus(*src))
			{
				if (is_INTEGER( src))
				{
					(void)parse_INTEGER( src);
				}
				else
				{
					(void)parse_FLOAT( src);
				}
			}
			else
			{
				if (is_INTEGER( src))
				{
					(void)parse_UNSIGNED( src);
				}
				else
				{
					(void)parse_FLOAT( src);
				}
			}
			value = std::string( src_bk, src - src_bk);
		}
		else if (isStringQuote(*src))
		{
			value = parse_STRING( src);
		}
		else
		{
			throw std::runtime_error("unknown type in argument list");
		}
		rt.push_back( value);
		if (isComma(*src))
		{
			(void)parse_OPERATOR(src);
			continue;
		}
		break;
	}
	return rt;
}

static void parseFunctionDef( const char* functype, std::string& name, std::vector<std::string> arg, char const*& src)
{
	if (isAlpha(*src))
	{
		name = parse_IDENTIFIER( src);
		if (isOpenOvalBracket( *src))
		{
			arg = parseArgumentList( src);
			if (isCloseOvalBracket( *src))
			{
				(void)parse_OPERATOR( src);
			}
			else
			{
				throw std::runtime_error( std::string( "comma ',' as argument separator or close oval brakcet ')' expected at end of ") + functype + " argument list");
			}
		}
	}
	else
	{
		throw std::runtime_error( std::string(functype) + " definition (identifier) expected");
	}
}

static void parseFeatureDef(
	DocumentAnalyzerInterface& analyzer,
	const std::string& featurename,
	char const*& src,
	FeatureClass featureClass)
{
	std::string xpathexpr;
	std::string normalizerName;
	std::vector<std::string> normalizerArg;
	std::string tokenizerName;
	std::vector<std::string> tokenizerArg;

	parseFunctionDef( "normalizer", normalizerName, normalizerArg, src);
	parseFunctionDef( "tokenizer", tokenizerName, tokenizerArg, src);

	if (isStringQuote(*src))
	{
		xpathexpr = parse_STRING( src);
	}
	else
	{
		char const* start = src;
		while (*src && !isSpace(*src) && *src != ';') ++src;
		xpathexpr.append( start, src-start);
	}

	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.defineSearchIndexFeature(
				featurename, xpathexpr,
				TokenizerConfig( tokenizerName, tokenizerArg),
				NormalizerConfig( normalizerName, normalizerArg));
			break;

		case FeatForwardIndexTerm:
			analyzer.defineForwardIndexFeature(
				featurename, xpathexpr,
				TokenizerConfig( tokenizerName, tokenizerArg),
				NormalizerConfig( normalizerName, normalizerArg));
			break;

		case FeatMetaData:
			analyzer.defineMetaDataFeature(
				featurename, xpathexpr,
				TokenizerConfig( tokenizerName, tokenizerArg),
				NormalizerConfig( normalizerName, normalizerArg));
			break;

		case FeatAttribute:
			analyzer.defineAttributeFeature(
				featurename, xpathexpr,
				TokenizerConfig( tokenizerName, tokenizerArg),
				NormalizerConfig( normalizerName, normalizerArg));
			break;
	}
}


DLL_PUBLIC void strus::loadDocumentAnalyzerProgram(
		DocumentAnalyzerInterface& analyzer,
		const std::string& source)
{
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		FeatureClass featclass = FeatSearchIndexTerm;
		
		while (*src)
		{
			if (!isAlnum(*src))
			{
				throw std::runtime_error( "alphanumeric identifier (feature class name or feature name) expected at start of a feature declaration");
			}
			std::string name = parse_IDENTIFIER( src);
			if (isColon( *src))
			{
				(void)parse_OPERATOR(src);

				featclass = featureClassFromName( name);
				if (!isAlnum(*src))
				{
					throw std::runtime_error( "alphanumeric identifier (feature set name) expected at start of a feature declaration after class name declaration");
				}
				name = parse_IDENTIFIER( src);
			}
			if (isAssign( *src))
			{
				(void)parse_OPERATOR(src);
				parseFeatureDef( analyzer, name, src, featclass);
			}
			else
			{
				throw std::runtime_error( "assignment operator '=' expected after set identifier in a feature declaration");
			}
			if (!isSemiColon(*src))
			{
				throw std::runtime_error( "semicolon ';' expected at end of feature declaration");
			}
			(void)parse_OPERATOR(src);
		}
	}
	catch (const std::runtime_error& e)
	{
		throw std::runtime_error(
			std::string( "error in document analyzer program ")
			+ errorPosition( source.c_str(), src)
			+ ":" + e.what());
	}
}


DLL_PUBLIC void strus::loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		const std::string& source)
{
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		while (*src)
		{
			(void)parse_OPERATOR(src);
			if (!isAlnum(*src))
			{
				throw std::runtime_error( "alphanumeric identifier (feature class name or feature name) expected at start of a feature declaration");
			}
			std::string method = parse_IDENTIFIER( src);
			if (!isAssign( *src))
			{
				throw std::runtime_error( "assignment operator '=' expected after type identifier in a query segment type declaration");
			}
			(void)parse_OPERATOR(src);
	
			if (!isAlpha(*src))
			{
				throw std::runtime_error( "identifier (feature name) expected after assign '=' in a query segment type declaration");
			}
			std::string featureType = parse_IDENTIFIER( src);
			(void)parse_OPERATOR(src);
	
			std::string normalizerName;
			std::vector<std::string> normalizerArg;
			std::string tokenizerName;
			std::vector<std::string> tokenizerArg;
		
			parseFunctionDef( "normalizer", normalizerName, normalizerArg, src);
			parseFunctionDef( "tokenizer", tokenizerName, tokenizerArg, src);
	
			analyzer.defineMethod(
					method, featureType, 
					TokenizerConfig( tokenizerName, tokenizerArg),
					NormalizerConfig( normalizerName, normalizerArg));

			if (!isSemiColon(*src))
			{
				throw std::runtime_error( "semicolon ';' expected at end of query segment type declaration");
			}
			(void)parse_OPERATOR(src);
		}
	}
	catch (const std::runtime_error& e)
	{
		throw std::runtime_error(
			std::string( "error in query analyzer program ")
			+ errorPosition( source.c_str(), src)
			+ ":" + e.what());
	}
}


static std::string parseAnalyzeMethodName( char const*& src)
{
	if (isColon( *src))
	{
		(void)parse_OPERATOR( src);
		if (isAlpha( *src))
		{
			return parse_IDENTIFIER(src);
		}
		else
		{
			throw std::runtime_error( "analyze method name (identifier) expected after colon ':' in query");
		}
	}
	else
	{
		return "default";
	}
}


static void pushQueryTextSegment(
		QueryInterface& query,
		const QueryAnalyzerInterface& analyzer,
		const std::string& analyzeMethodName,
		const std::string& content)
{
	std::vector<analyzer::Term> 
		queryTerms = analyzeSegment( analyzeMethodName, querySegment);
	std::vector<analyzer::Term>::const_iterator
		ti = queryTerms.begin(), te = queryTerms.end();

	unsigned int pos = 0;
	std::size_t seq_argc = 0;
	while (ti != te)
	{
		if (pos == 0 || ti->pos() != pos)
		{
			++seq_argc;
			std::size_t join_argc = 0;
			pos = ti->pos();
			for (; ti != te && ti->pos() == pos; ++ti)
			{
				join_argc++;
				query.pushTerm( ti->type(), ti->value);
			}
			if (join_argc > 1)
			{
				query.pushExpression(
					operator_query_phrase_same_position(),
					join_argc, 0);
			}
		}
	}
	if (seq_argc > 1)
	{
		query.pushExpression(
			operator_query_phrase_sequence(),
			join_argc, pos);
	}
}


static void parseQueryExpression(
		QueryInterface& query,
		const QueryAnalyzerInterface& analyzer,
		char const*& src)
{
	std::string functionName;
	if (isAlpha( *src))
	{
		functionName = parse_IDENTIFIER(src);
		if (isOpenOvalBracket(*src))
		{
			range = 0;
			(void)parse_OPERATOR( src);
			std::size_t argc = 0;

			if (!isCloseOvalBracket( *src)) while (*src)
			{
				argc++;
				parseQueryExpression( query, analyzer, src);
				if (isComma( *src))
				{
					(void)parse_OPERATOR( src);
					continue;
				}
				break;
			}
			if (isColon( *src))
			{
				range = parse_INTEGER( src);
				range_set = true;
				if (!isCloseOvalBracket( *src))
				{
					throw std::runtime_error( "comma ',' as query argument separator or close oval bracket ')' as end of a query expression expected");
				}
			}
			else if (!isCloseOvalBracket( *src))
			{
				throw std::runtime_error( "comma ',' as query argument separator or colon ':' as range specifier or close oval bracket ')' as end of a query expression expected");
			}
			query.pushExpression( functionName, argc, range);
		}
		else
		{
			std::string querySegment = functionName;
			std::string analyzeMethodName = parseAnalyzeMethodName( src);
			if (ti == te)
			{
				throw std::runtime_error( std::string( "query analyzer returned empty list of terms for query segment '") + querySegment + "'");
			}
			pushQueryTextSegment( query, analyzer, analyzeMethodName, querySegment);
		}
	}
	else if (isDigit( *src))
	{
		std::string querySegment = parse_IDENTIFIER( src);
		std::string analyzeMethodName = parseAnalyzeMethodName( src);
		if (ti == te)
		{
			throw std::runtime_error( std::string( "query analyzer returned empty list of terms for query segment '") + querySegment + "'");
		}
		pushQueryTextSegment( query, analyzer, analyzeMethodName, querySegment);
	}
	else if (isStringQuote( *src))
	{
		std::string querySegment = parse_STRING( src);
		std::string analyzeMethodName = parseAnalyzeMethodName( src);
		if (ti == te)
		{
			throw std::runtime_error( std::string( "query analyzer returned empty list of terms for query segment '") + querySegment + "'");
		}
		pushQueryTextSegment( query, analyzer, analyzeMethodName, querySegment);
	}
	else
	{
		throw std::runtime_error( "syntax error in query, query expression or term expected");
	}
}

static ArithmeticVariant parseMetaDataOperand( char const*& src)
{
	ArithmeticVariant rt;
	if (is_INTEGER( src))
	{
		if (isMinus(*src))
		{
			rt = parse_INTEGER( src);
		}
		else
		{
			rt = parse_UNSIGNED( src);
		}
	}
	else
	{
		rt = parse_FLOAT( src);
	}
	return rt;
}

static QueryInterface::CompareOperator invertedOperator( QueryInterface::CompareOperator op)
{
	switch (op)
	{
		case QueryInterface::CompareLess: return QueryInterface::CompareGreaterEqual;
		case QueryInterface::CompareLessEqual: return QueryInterface::CompareGreater;
		case QueryInterface::CompareEqual: return QueryInterface::CompareNotEqual;
		case QueryInterface::CompareNotEqual: return QueryInterface::CompareEqual;
		case QueryInterface::CompareGreater: return QueryInterface::CompareLessEqual;
		case QueryInterface::CompareGreaterEqual: return QueryInterface::CompareLess;
	}
	throw std::logic_error( "bad query meta data operator");
}

static QueryInterface::CompareOperator parseMetaDataComparionsOperator( char const*& src)
{
	QueryInterface::CompareOperator rt;
	if (*src == '=')
	{
		parse_OPERATOR( src);
		rt = QueryInterface::CompareEqual;
	}
	else if (*src == '>')
	{
		++src;
		if (*src == '=')
		{
			++src;
			rt = QueryInterface::CompareGreaterEqual;
		}
		else
		{
			rt = QueryInterface::CompareGreater;
		}
	}
	else if (*src == '<')
	{
		++src;
		if (*src == '=')
		{
			++src;
			rt = QueryInterface::CompareLessEqual;
		}
		else
		{
			rt = QueryInterface::CompareLess;
		}
	}
	else if (*src == '!')
	{
		if (*src == '=')
		{
			++src;
			rt = QueryInterface::CompareNotEqual;
		}
		else
		{
			throw std::runtime_error( "unknown meta data comparison operator");
		}
	}
	else
	{
		throw std::runtime_error( "expected meta data comparison operator");
	}
	skipSpaces( src);
	if (*src && !isAlnum( *src) && !isStringQuote( *src))
	{
		throw std::runtime_error( "unexpected character after meta data comparison operator");
	}
	return rt;
}

static void parseQueryRestriction(
		QueryInterface& query,
		const QueryAnalyzerInterface& analyzer,
		char const*& src)
{
	if (isAlpha( *src))
	{
		
	}
	else if (isStringQuote( *src) || isDigit( *src) || isMinus( *src))
	{
		ArithmeticVariant operand;
		if (isStringQuote( *src))
		{
			std::string value = parse_STRING( src);
			operand = parseMetaDataOperand( value.c_str());
		}
		else
		{
			operand = parseMetaDataOperand( src);
		}
		QueryInterface::CompareOperator
			cmpop = parseMetaDataComparionsOperator( src);
		cmpop = invertedOperator( cmpop);

		if (!isAlpha( *src))
		{
			throw std::runtime_error("expected meta data field identifier in query restriction expression");
		}
		std::string fieldname = parse_IDENTIFIER( src);

		query.defineMetaDataRestriction( cmpop
					CompareOperator opr, const char* name,
					const ArithmeticVariant& operand, bool newGroup=true)=0;
)
	}
}

DLL_PUBLIC void strus::loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface& analyzer,
		const std::string& source)
{
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		enum Section
		{
			SectionSearch,
			SectionRestriction
		};
		Section section = SectionQuery;

		while (*src)
		{
			// Parse query section, if defined:
			std::string name;
			if (isAlpha( *src))
			{
				char const* src_bk = src;
				name = parse_IDENTIFIER( src);
				if (isColon( *src))
				{
					(void)parse_OPERATOR( src);
					if (isEqual( name, "Search"))
					{
						section = SectionSearch;
					}
					else if (isEqual( name, "Restriction"))
					{
						section = SectionRestriction;
					}
					else
					{
						throw std::runtime_error( std::string( "unknown section identifier '") + name + "' in query");
					}
				}
				else
				{
					src = src_bk;
				}
			}
			// Parse expression, depending on section defined:
			switch (section)
			{
				case SectionSearch:
					parseQueryExpression( query, analyzer, src);
					break;
				case SectionRestriction:
					break;
			};
		}
	}
	catch (const std::runtime_error& e)
	{
		throw std::runtime_error(
			std::string( "error in query ")
			+ errorPosition( source.c_str(), src)
			+ ":" + e.what());
	}
}


