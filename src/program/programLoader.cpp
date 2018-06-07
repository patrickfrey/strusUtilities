/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/programLoader.hpp"
#include "lexems.hpp"
#include "strus/lib/pattern_serialize.hpp"
#include "strus/constants.hpp"
#include "strus/numericVariant.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/weightingFunctionInterface.hpp"
#include "strus/weightingFunctionInstanceInterface.hpp"
#include "strus/summarizerFunctionInterface.hpp"
#include "strus/summarizerFunctionInstanceInterface.hpp"
#include "strus/normalizerFunctionInterface.hpp"
#include "strus/normalizerFunctionInstanceInterface.hpp"
#include "strus/tokenizerFunctionInterface.hpp"
#include "strus/tokenizerFunctionInstanceInterface.hpp"
#include "strus/aggregatorFunctionInterface.hpp"
#include "strus/aggregatorFunctionInstanceInterface.hpp"
#include "strus/scalarFunctionInterface.hpp"
#include "strus/patternLexerInterface.hpp"
#include "strus/patternLexerInstanceInterface.hpp"
#include "strus/patternTermFeederInterface.hpp"
#include "strus/patternTermFeederInstanceInterface.hpp"
#include "strus/patternMatcherInterface.hpp"
#include "strus/patternMatcherInstanceInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/metaDataRestrictionInterface.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/queryAnalyzerInstanceInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/storageDocumentUpdateInterface.hpp"
#include "strus/vectorStorageClientInterface.hpp"
#include "strus/vectorStorageTransactionInterface.hpp"
#include "strus/analyzer/queryTermExpression.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/reference.hpp"
#include "strus/base/snprintf.h"
#include "strus/base/string_format.hpp"
#include "strus/base/dll_tags.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/hton.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/symbolTable.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/string_conv.hpp"
#include "private/internationalization.hpp"
#include "metadataExpression.hpp"
#include "termExpression.hpp"
#include "errorPosition.hpp"
#include "patternMatchProgramParser.hpp"
#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>

using namespace strus;
using namespace strus::parser;

#undef STRUS_LOWLEVEL_DEBUG

static std::string parseQueryTerm( char const*& src)
{
	if (isTextChar( *src))
	{
		return parse_TEXTWORD( src);
	}
	else if (isStringQuote( *src))
	{
		return parse_STRING( src);
	}
	else
	{
		throw std::runtime_error( _TXT("query term (identifier,word,number or string) expected"));
	}
}

static void parseTermConfig(
		QueryEvalInterface& qeval,
		QueryDescriptors& qdescr,
		char const*& src)
{
	if (isAlpha(*src))
	{
		std::string termset = string_conv::tolower( parse_IDENTIFIER( src));
		if (!isStringQuote( *src) && !isTextChar( *src))
		{
			throw std::runtime_error( _TXT( "term value (string,identifier,number) after the feature identifier"));
		}
		std::string termvalue = parseQueryTerm( src);
		if (!isColon( *src))
		{
			throw std::runtime_error( _TXT( "colon (':') expected after term value"));
		}
		(void)parse_OPERATOR(src);
		if (!isAlpha( *src))
		{
			throw std::runtime_error( _TXT( "term type identifier expected after colon and term value"));
		}
		std::string termtype = string_conv::tolower( parse_IDENTIFIER( src));
		qeval.addTerm( termset, termtype, termvalue);
	}
	else
	{
		throw std::runtime_error( _TXT( "feature set identifier expected as start of a term declaration in the query"));
	}
}

static NumericVariant parseNumericValue( char const*& src)
{
	if (is_INTEGER(src))
	{
		if (isMinus(*src) || isPlus(*src))
		{
			return NumericVariant( (int64_t)parse_INTEGER( src));
		}
		else
		{
			if (isPlus(*src))
			{
				parse_OPERATOR( src);
				if (isMinus(*src)) throw strus::runtime_error( "%s",  _TXT( "unexpected minus '-' operator after plus '+'"));
			}
			while (*src == '0') ++src;
			if (*src >= '1' && *src <= '9')
			{
				return NumericVariant( (uint64_t)parse_UNSIGNED( src));
			}
			else
			{
				skipSpaces(src);
				return NumericVariant( (int64_t)0);
			}
		}
	}
	else
	{
		return NumericVariant( parse_FLOAT( src));
	}
}

static void parseWeightingFormula(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* queryproc,
		char const*& src)
{
	std::string langName;
	if (isAlpha( *src))
	{
		langName = parse_IDENTIFIER( src);
	}
	if (!isStringQuote( *src))
	{
		throw std::runtime_error( _TXT( "weighting formula string expected"));
	}
	std::string funcsrc = parse_STRING( src);
	const ScalarFunctionParserInterface* scalarfuncparser = queryproc->getScalarFunctionParser(langName);
	strus::local_ptr<ScalarFunctionInterface> scalarfunc( scalarfuncparser->createFunction( funcsrc, std::vector<std::string>()));
	if (!scalarfunc.get())
	{
		throw std::runtime_error( _TXT( "failed to create scalar function (weighting formula) from source"));
	}
	qeval.defineWeightingFormula( scalarfunc.get());
	scalarfunc.release();
}

static void parseWeightingConfig(
		QueryEvalInterface& qeval,
		QueryDescriptors& qdescr,
		const QueryProcessorInterface* queryproc,
		char const*& src)
{
	if (!isAlpha( *src))
	{
		throw std::runtime_error( _TXT( "weighting function identifier expected"));
	}
	std::string functionName = parse_IDENTIFIER( src);
	std::string debuginfoName;

	const WeightingFunctionInterface* wf = queryproc->getWeightingFunction( functionName);
	if (!wf) throw strus::runtime_error(_TXT( "weighting function '%s' not defined"), functionName.c_str());

	strus::local_ptr<WeightingFunctionInstanceInterface> function( wf->createInstance( queryproc));
	if (!function.get()) throw strus::runtime_error(_TXT( "failed to create weighting function '%s'"), functionName.c_str());

	typedef QueryEvalInterface::FeatureParameter FeatureParameter;
	std::vector<FeatureParameter> featureParameters;

	if (!isOpenOvalBracket( *src))
	{
		throw std::runtime_error( _TXT( "open oval bracket '(' expected after weighting function identifier"));
	}
	(void)parse_OPERATOR(src);

	if (!isCloseOvalBracket( *src)) for (;;)
	{
		bool isFeatureParam = false;
		if (isDot(*src))
		{
			(void)parse_OPERATOR(src);
			isFeatureParam = true;
		}
		if (!isAlpha( *src))
		{
			throw std::runtime_error( _TXT( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected"));
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw std::runtime_error( _TXT( "assingment operator '=' expected after weighting function parameter name"));
		}
		(void)parse_OPERATOR(src);
		if (!isFeatureParam && strus::caseInsensitiveEquals( parameterName, "debug"))
		{
			if (!debuginfoName.empty())
			{
				throw std::runtime_error( _TXT("duplicate definition of 'debug' parameter"));
			}
			if (isStringQuote(*src))
			{
				debuginfoName = parse_STRING( src);
			}
			else if (isStringQuote( *src))
			{
				debuginfoName = parse_STRING( src);
			}
			else
			{
				throw std::runtime_error( _TXT("identifier or string expected as argument of 'debug' parameter"));
			}
		}
		else if (isDigit(*src) || isMinus(*src) || isPlus(*src))
		{
			if (isFeatureParam)
			{
				throw std::runtime_error( _TXT( "feature parameter argument must be an identifier or string and not a number"));
			}
			NumericVariant parameterValue = parseNumericValue( src);
			function->addNumericParameter( parameterName, parameterValue);
		}
		else if (isStringQuote(*src))
		{
			std::string parameterValue = parse_STRING( src);
			if (isFeatureParam)
			{
				featureParameters.push_back( FeatureParameter( parameterName, parameterValue));
			}
			else
			{
				function->addStringParameter( parameterName, parameterValue);
			}
		}
		else if (isAlpha(*src))
		{
			std::string parameterValue = parse_IDENTIFIER( src);
			if (isFeatureParam)
			{
				featureParameters.push_back( FeatureParameter( parameterName, parameterValue));
			}
			else
			{
				function->addStringParameter( parameterName, parameterValue);
			}
		}
		else
		{
			throw std::runtime_error( _TXT("parameter value (identifier,string,number) expected"));
		}
		if (!isComma( *src))
		{
			break;
		}
		(void)parse_OPERATOR(src);
	}
	if (!isCloseOvalBracket( *src))
	{
		throw std::runtime_error( _TXT( "close oval bracket ')' expected at end of weighting function parameter list"));
	}
	(void)parse_OPERATOR(src);
	qeval.addWeightingFunction( functionName, function.get(), featureParameters, debuginfoName); 
	(void)function.release();
}


static void parseSummarizerConfig(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* queryproc,
		char const*& src)
{
	std::string functionName;
	typedef QueryEvalInterface::FeatureParameter FeatureParameter;
	std::vector<FeatureParameter> featureParameters;

	if (!isAlpha( *src))
	{
		throw std::runtime_error( _TXT( "name of summarizer function expected at start of summarizer definition"));
	}
	functionName = string_conv::tolower( parse_IDENTIFIER( src));
	std::string debuginfoName;

	const SummarizerFunctionInterface* sf = queryproc->getSummarizerFunction( functionName);
	if (!sf) throw strus::runtime_error(_TXT( "summarizer function not defined: '%s'"), functionName.c_str());

	strus::local_ptr<SummarizerFunctionInstanceInterface> function( sf->createInstance( queryproc));
	if (!function.get()) throw strus::runtime_error(_TXT( "failed to create summarizer function instance '%s'"), functionName.c_str());

	if (!isOpenOvalBracket( *src))
	{
		throw std::runtime_error( _TXT( "open oval bracket '(' expected after summarizer function identifier"));
	}
	(void)parse_OPERATOR(src);

	if (!isCloseOvalBracket( *src)) for (;;)
	{
		bool isFeatureParam = false;
		if (isDot(*src))
		{
			(void)parse_OPERATOR(src);
			isFeatureParam = true;
		}
		if (!isAlpha( *src))
		{
			throw std::runtime_error( _TXT( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected"));
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw std::runtime_error( _TXT( "assignment operator '=' expected after summarizer function parameter name"));
		}
		(void)parse_OPERATOR(src);
		if (!isFeatureParam && strus::caseInsensitiveEquals( parameterName, "debug"))
		{
			if (!debuginfoName.empty())
			{
				throw std::runtime_error( _TXT("duplicate definition of 'debug' parameter"));
			}
			if (isStringQuote(*src))
			{
				debuginfoName = parse_STRING( src);
			}
			else if (isStringQuote( *src))
			{
				debuginfoName = parse_STRING( src);
			}
			else
			{
				throw std::runtime_error( _TXT("identifier or string expected as argument of 'debug' parameter"));
			}
		}
		else if (isDigit(*src) || isMinus(*src) || isPlus(*src))
		{
			if (isFeatureParam)
			{
				throw std::runtime_error( _TXT( "feature parameter argument must be an identifier or string and not a number"));
			}
			NumericVariant parameterValue = parseNumericValue( src);
			function->addNumericParameter( parameterName, parameterValue);
		}
		else if (isStringQuote(*src))
		{
			std::string parameterValue = parse_STRING( src);
			if (isFeatureParam)
			{
				featureParameters.push_back( FeatureParameter( parameterName, parameterValue));
			}
			else
			{
				function->addStringParameter( parameterName, parameterValue);
			}
		}
		else
		{
			std::string parameterValue = parse_IDENTIFIER( src);
			if (isFeatureParam)
			{
				featureParameters.push_back( FeatureParameter( parameterName, parameterValue));
			}
			else
			{
				function->addStringParameter( parameterName, parameterValue);
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
		throw std::runtime_error( _TXT( "close oval bracket ')' expected at end of summarizer function parameter list"));
	}
	(void)parse_OPERATOR(src);
	qeval.addSummarizerFunction( functionName, function.get(), featureParameters, debuginfoName);
	(void)function.release();
}

DLL_PUBLIC bool strus::loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		QueryDescriptors& qdescr,
		const QueryProcessorInterface* queryproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	enum StatementKeyword {e_FORMULA, e_EVAL, e_SELECT, e_WEIGHT, e_RESTRICTION, e_TERM, e_SUMMARIZE};
	std::string id;

	skipSpaces( src);
	try
	{
		while (*src)
		{
			switch ((StatementKeyword)parse_KEYWORD( src, 7, "FORMULA", "EVAL", "SELECT", "WEIGHT", "RESTRICT", "TERM", "SUMMARIZE"))
			{
				case e_TERM:
					parseTermConfig( qeval, qdescr, src);
					break;
				case e_SELECT:
				{
					if (!qdescr.selectionFeatureSet.empty())
					{
						throw std::runtime_error( _TXT("cannot handle more than one SELECT feature definition yet"));
					}
					qdescr.selectionFeatureSet = parse_IDENTIFIER(src);
					qeval.addSelectionFeature( qdescr.selectionFeatureSet);
					break;
				}
				case e_WEIGHT:
				{
					if (!qdescr.weightingFeatureSet.empty())
					{
						throw std::runtime_error( _TXT("cannot handle more than one WEIGHT feature definition yet"));
					}
					qdescr.weightingFeatureSet = parse_IDENTIFIER(src);
					break;
				}
				case e_RESTRICTION:
					while (*src && isAlnum( *src))
					{
						qeval.addRestrictionFeature( parse_IDENTIFIER(src));
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
				case e_EVAL:
					parseWeightingConfig( qeval, qdescr, queryproc, src);
					break;
				case e_FORMULA:
					parseWeightingFormula( qeval, queryproc, src);
					break;
				case e_SUMMARIZE:
					parseSummarizerConfig( qeval, queryproc, src);
					break;
			}
			if (*src)
			{
				if (!isSemiColon(*src))
				{
					throw std::runtime_error( _TXT( "semicolon expected as delimiter of query eval program instructions"));
				}
				(void)parse_OPERATOR( src);
			}
		}
		if (qdescr.selectionFeatureSet.empty())
		{
			throw std::runtime_error( _TXT("no selection feature set (SELECT) defined in query evaluation configuration"));
		}
		if (qdescr.weightingFeatureSet.empty())
		{
			qdescr.weightingFeatureSet = qdescr.selectionFeatureSet;
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory parsing query evaluation program %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error in query evaluation program %s: %s"), pos.c_str(), e.what());
		return false;
	}
}

static std::string parseQueryFieldType( char const*& src)
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
			throw strus::runtime_error( "%s",  _TXT("query analyze phrase type (identifier) expected after colon ':' in query"));
		}
	}
	else
	{
		return std::string();
	}
}

static std::string parseVariableRef( char const*& src)
{
	std::string rt;
	if (isAssign(*src))
	{
		(void)parse_OPERATOR(src);
		rt = parse_IDENTIFIER( src);
	}
	return rt;
}

static bool isQueryStructureExpression( char const* src)
{
	if (isAlpha(*src))
	{
		++src;
		while (isAlnum(*src)) ++src;
		skipSpaces(src);
		return (isOpenOvalBracket(*src));
	}
	else
	{
		return false;
	}
}

static bool isQueryMetaDataExpression( char const* src)
{
	if (isAlpha(*src))
	{
		++src;
		while (isAlnum(*src)) ++src;
		skipSpaces(src);
		return (isCompareOperator(src));
	}
	else
	{
		return false;
	}
}

static MetaDataRestrictionInterface::CompareOperator invertCompareOperator( const MetaDataRestrictionInterface::CompareOperator& opr)
{
	switch (opr)
	{
		case MetaDataRestrictionInterface::CompareLess: return MetaDataRestrictionInterface::CompareGreaterEqual;
		case MetaDataRestrictionInterface::CompareLessEqual: return MetaDataRestrictionInterface::CompareGreater;
		case MetaDataRestrictionInterface::CompareEqual: return MetaDataRestrictionInterface::CompareNotEqual;
		case MetaDataRestrictionInterface::CompareNotEqual: return MetaDataRestrictionInterface::CompareEqual;
		case MetaDataRestrictionInterface::CompareGreater: return MetaDataRestrictionInterface::CompareLessEqual;
		case MetaDataRestrictionInterface::CompareGreaterEqual: return MetaDataRestrictionInterface::CompareLess;
	}
	throw std::runtime_error( _TXT("unknown metadata compare operator"));
}

static void parseMetaDataExpression( 
		MetaDataExpression& metadataExpression,
		char const*& src)
{
	std::string fieldName;
	std::vector<std::string> values;
	MetaDataRestrictionInterface::CompareOperator opr;
	if (isAlpha(*src))
	{
		fieldName = parse_IDENTIFIER(src);
		opr = parse_CompareOperator( src);
		values.push_back( parseQueryTerm( src));
		while (isComma(*src))
		{
			parse_OPERATOR(src);
			values.push_back( parseQueryTerm( src));
		}
	}
	else
	{
		values.push_back( parseQueryTerm( src));
		while (isComma(*src))
		{
			parse_OPERATOR(src);
			values.push_back( parseQueryTerm( src));
		}
		opr = invertCompareOperator( parse_CompareOperator( src));
		fieldName = parse_IDENTIFIER(src);
	}
	std::vector<std::string>::const_iterator vi = values.begin(), ve = values.end();
	for (; vi != ve; ++vi)
	{
		metadataExpression.pushCompare( opr, fieldName, *vi);
	}
	if (values.size() > 1)
	{
		metadataExpression.pushOperator( MetaDataExpression::OperatorOR, values.size());
	}
}

static void parseQueryTermExpression(
		TermExpression& termExpression,
		TermExpression& selectedTermExpression,
		const QueryDescriptors& qdescr,
		char const*& src)
{
	bool isSelection = true;
	if (isTilde( *src))
	{
		(void)parse_OPERATOR( src);
		isSelection = false;
	}
	std::string field;
	std::string fieldType;
	if (isTextChar( *src) || isStringQuote( *src))
	{
		field = parseQueryTerm( src);
		if (isColon( *src))
		{
			fieldType = parseQueryFieldType( src);
		}
		else
		{
			if (!qdescr.defaultFieldTypeDefined)
			{
				throw std::runtime_error( _TXT("no query field with name 'default' defined in query analyzer configuration, cannot handle query fields without explicit naming"));
			}
			fieldType = "default";
		}
	}
	else if (isColon( *src))
	{
		fieldType = parseQueryFieldType( src);
	}
	else
	{
		throw strus::runtime_error( "%s",  _TXT("syntax error in query, query expression or term expected"));
	}
	if (qdescr.fieldset.find( fieldType) == qdescr.fieldset.end())
	{
		throw strus::runtime_error( _TXT("query field type '%s' not defined in analyzer configuration"), fieldType.c_str());
	}
	if (isSelection)
	{
		selectedTermExpression.pushField( fieldType, field);
	}
	termExpression.pushField( fieldType, field);
	std::string variableName = parseVariableRef( src);
	if (!variableName.empty())
	{
		termExpression.attachVariable( variableName);
	}
}

static void parseQueryStructureExpression(
		TermExpression& termExpression,
		TermExpression& selectedTermExpression,
		const QueryDescriptors& qdescr,
		char const*& src)
{
	std::string functionName = parse_IDENTIFIER(src);
	if (!isOpenOvalBracket(*src)) throw std::runtime_error( _TXT("internal: bad lookahead in query parser"));

	(void)parse_OPERATOR( src);
	std::size_t argc = 0;

	if (!isCloseOvalBracket( *src) && !isOr( *src) && !isExp( *src)) while (*src)
	{
		argc++;
		if (isQueryStructureExpression( src))
		{
			parseQueryStructureExpression( termExpression, selectedTermExpression, qdescr, src);
		}
		else
		{
			parseQueryTermExpression( termExpression, selectedTermExpression, qdescr, src);
		}
		if (isComma( *src))
		{
			(void)parse_OPERATOR( src);
			continue;
		}
		else if (!isCloseOvalBracket( *src) && !isExp( *src) && !isOr(*src))
		{
			throw std::runtime_error( _TXT("expected a comma ',' (argument separator) or a close bracket ')' (end of argument list) or a '|' (range specififier), or a '^' (cardinality specifier)"));
		}
		break;
	}
	int range = 0;
	unsigned int cardinality = 0;
	while (isOr( *src) || isExp( *src))
	{
		if (isOr( *src))
		{
			if (range != 0) throw strus::runtime_error( "%s",  _TXT("range specified twice"));
			(void)parse_OPERATOR( src);
			if (isPlus(*src))
			{
				parse_OPERATOR(src);
				range = parse_UNSIGNED( src);
			}
			else
			{
				range = parse_INTEGER( src);
			}
			if (range == 0) throw strus::runtime_error( "%s",  _TXT("range should be a non null number"));
		}
		else
		{
			if (cardinality != 0) throw strus::runtime_error( "%s",  _TXT("cardinality specified twice"));
			(void)parse_OPERATOR( src);
			cardinality = parse_UNSIGNED1( src);
		}
	}
	if (!isCloseOvalBracket( *src))
	{
		throw strus::runtime_error( "%s",  _TXT("close oval bracket ')' expected as end of a query structure expression expected"));
	}
	(void)parse_OPERATOR( src);
	std::string variableName = parseVariableRef( src);
	termExpression.pushExpression( functionName, argc, range, cardinality);
	if (!variableName.empty())
	{
		termExpression.attachVariable( variableName);
	}
}

DLL_PUBLIC bool strus::loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInstanceInterface* analyzer,
		const QueryProcessorInterface* queryproc,
		const std::string& source,
		const QueryDescriptors& qdescr,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	try
	{
		QueryAnalyzerStruct queryAnalyzerStruct;
		std::set<std::string>::const_iterator si = qdescr.fieldset.begin(), se = qdescr.fieldset.end();
		for (; si != se; ++si)
		{
			// Group elements in one field implicitely as sequence:
			queryAnalyzerStruct.autoGroupBy( *si, "sequence_imm", 0, 0, QueryAnalyzerContextInterface::GroupAll, false/*group single*/);
		}
		MetaDataExpression metaDataExpression( analyzer, errorhnd);
		TermExpression termExpression( &queryAnalyzerStruct, analyzer, errorhnd);
		TermExpression selectedTermExpression( &queryAnalyzerStruct, analyzer, errorhnd);

		skipSpaces(src);
		while (*src)
		{
			// Parse query section:
			if (isQueryMetaDataExpression( src))
			{
				parseMetaDataExpression( metaDataExpression, src);
			}
			else
			{
				if (isQueryStructureExpression( src))
				{
					parseQueryStructureExpression( termExpression, selectedTermExpression, qdescr, src);
				}
				else
				{
					parseQueryTermExpression( termExpression, selectedTermExpression, qdescr, src);
				}
				double featureWeight = 1.0;
				if (isAsterisk(*src))
				{
					(void)parse_OPERATOR(src);
					if (isDigit(*src))
					{
						featureWeight = parse_FLOAT( src);
					}
					else
					{
						throw std::runtime_error( _TXT("feature weight expected after term expression and following asterisk '*'"));
					}
				}
				termExpression.assignFeature( qdescr.weightingFeatureSet, featureWeight);
			}
		}
		{
			// Define selection term expression
			unsigned int argc = selectedTermExpression.nofExpressionsDefined();
			unsigned int cardinality = std::min( argc, (unsigned int)(qdescr.defaultSelectionTermPart * argc + 1));
			selectedTermExpression.pushExpression( qdescr.defaultSelectionJoin, argc, 0/*range*/, cardinality);
			selectedTermExpression.assignFeature( qdescr.selectionFeatureSet, 1.0);
		}
		metaDataExpression.analyze();
		metaDataExpression.translate( query);
		termExpression.analyze();
		termExpression.translate( query, queryproc);
		selectedTermExpression.analyze();
		selectedTermExpression.translate( query, queryproc);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory parsing query source %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error in query source %s: %s"), pos.c_str(), e.what());
		return false;
	}
}


DLL_PUBLIC bool strus::scanNextProgram(
		std::string& segment,
		std::string::const_iterator& si,
		const std::string::const_iterator& se,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		for (; si != se && (unsigned char)*si <= 32; ++si){}
		if (si == se) return false;
	
		std::string::const_iterator start = si;
		while (si != se)
		{
			for (; si != se && *si != '\n'; ++si){}
			if (si != se)
			{
				++si;
				std::string::const_iterator end = si;
	
				if (si != se && *si == '.')
				{
					++si;
					if (si != se && (*si == '\r' || *si == '\n'))
					{
						++si;
						segment = std::string( start, end);
						return true;
					}
				}
			}
		}
		segment = std::string( start, si);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory scanning next program"));
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error scanning next program: %s"), e.what());
		return false;
	}
}

static Index parseDocno( StorageClientInterface& storage, char const*& itr)
{
	if (isDigit(*itr) && is_INTEGER(itr))
	{
		return parse_UNSIGNED1( itr);
	}
	else if (isStringQuote(*itr))
	{
		std::string docid = parse_STRING(itr);
		return storage.documentNumber( docid);
	}
	else
	{
		std::string docid;
		for (; !isSpace(*itr); ++itr)
		{
			docid.push_back( *itr);
		}
		skipSpaces( itr);
		return storage.documentNumber( docid);
	}
}

static std::string parseDocKey( char const*& itr)
{
	if (isStringQuote(*itr))
	{
		return parse_STRING(itr);
	}
	else
	{
		std::string id;
		for (; !isSpace(*itr); ++itr)
		{
			id.push_back( *itr);
		}
		skipSpaces( itr);
		return id;
	}
}

static void storeMetaDataValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const NumericVariant& val)
{
	strus::local_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	if (!update.get()) throw strus::runtime_error( "%s",  _TXT("failed to create document update structure"));

	update->setMetaData( name, val);
	update->done();
}

static void storeAttributeValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const std::string& val)
{
	strus::local_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	if (!update.get()) throw strus::runtime_error( "%s",  _TXT("failed to create document update structure"));
	if (val.empty())
	{
		update->clearAttribute( name);
	}
	else
	{
		update->setAttribute( name, val);
	}
	update->done();
}

static void storeUserRights( StorageTransactionInterface& transaction, const Index& docno, const std::string& val)
{
	strus::local_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	if (!update.get()) throw strus::runtime_error( "%s",  _TXT("failed to create document update structure"));
	char const* itr = val.c_str();
	if (itr[0] == '+' && (itr[1] == ',' || !itr[1]))
	{
		itr += (itr[1])?2:1;
	}
	else
	{
		update->clearUserAccessRights();
	}
	while (*itr)
	{
		bool positive = true;
		if (*itr == '+')
		{
			(void)parse_OPERATOR( itr);
		}
		else if (*itr == '-')
		{
			positive = false;
			(void)parse_OPERATOR( itr);
		}
		std::string username = parse_IDENTIFIER(itr);
		if (positive)
		{
			update->setUserAccessRight( username);
		}
		else
		{
			update->clearUserAccessRight( username);
		}
		if (*itr == ',')
		{
			(void)parse_OPERATOR( itr);
		}
		else if (*itr)
		{
			throw strus::runtime_error( "%s",  _TXT("unexpected token in user rigths specification"));
		}
	}
}


enum StorageValueType
{
	StorageValueMetaData,
	StorageValueAttribute,
	StorageUserRights
};

static bool updateStorageValue(
		StorageTransactionInterface* transaction,
		const Index& docno,
		const std::string& elementName,
		StorageValueType valueType,
		const char* value)
{
	char const* itr = value;
	switch (valueType)
	{
		case StorageValueMetaData:
		{
			NumericVariant val( parseNumericValue( itr));
			storeMetaDataValue( *transaction, docno, elementName, val);
			return true;
		}
		case StorageValueAttribute:
		{
			std::string val;
			if (isTextChar( *itr))
			{
				val = parse_TEXTWORD( itr);
			}
			else if (isStringQuote( *itr))
			{
				val = parse_STRING( itr);
			}
			else
			{
				val = std::string( itr);
				itr = std::strchr( itr, '\0');
			}
			storeAttributeValue( *transaction, docno, elementName, val);
			return true;
		}
		case StorageUserRights:
		{
			std::string val( itr);
			itr = std::strchr( itr, '\0');
			storeUserRights( *transaction, docno, val);
			return true;
		}
	}
	if (*itr)
	{
		throw strus::runtime_error( "%s",  _TXT("extra characters after value assignment"));
	}
	return false;
}

typedef std::multimap<std::string,strus::Index> KeyDocnoMap;
static unsigned int loadStorageValues(
		StorageClientInterface& storage,
		const std::string& elementName,
		const KeyDocnoMap* attributemapref,
		const std::string& file,
		StorageValueType valueType,
		unsigned int commitsize)
{
	InputStream stream( file);
	if (stream.error()) throw strus::runtime_error(_TXT("failed to open storage value file '%s': %s"), file.c_str(), ::strerror(stream.error()));
	unsigned int rt = 0;
	strus::local_ptr<StorageTransactionInterface>
		transaction( storage.createTransaction());
	if (!transaction.get()) throw strus::runtime_error( "%s",  _TXT("failed to create storage transaction"));
	std::size_t linecnt = 1;
	unsigned int commitcnt = 0;
	try
	{
		char line[ 2048];
		for (; stream.readLine( line, sizeof(line)); ++linecnt)
		{
			char const* itr = line;
			if (attributemapref)
			{
				std::string attr = parseDocKey( itr);
				std::pair<KeyDocnoMap::const_iterator,KeyDocnoMap::const_iterator>
					range = attributemapref->equal_range( attr);
				KeyDocnoMap::const_iterator ki = range.first, ke = range.second;
				for (; ki != ke; ++ki)
				{
					if (updateStorageValue( transaction.get(), ki->second, elementName, valueType, itr))
					{
						rt += 1;
					}
				}
			}
			else
			{
				Index docno = parseDocno( storage, itr);
				if (!docno) continue;
				if (updateStorageValue( transaction.get(), docno, elementName, valueType, itr))
				{
					rt += 1;
				}
			}
			if (++commitcnt == commitsize)
			{
				if (!transaction->commit())
				{
					throw std::runtime_error( _TXT("transaction commit failed"));
				}
				commitcnt = 0;
				transaction.reset( storage.createTransaction());
				if (!transaction.get()) throw strus::runtime_error( "%s",  _TXT("failed to recreate storage transaction after commit"));
			}
		}
		if (stream.error())
		{
			throw strus::runtime_error(_TXT("failed to read from storage value file '%s': %s"), file.c_str(), ::strerror(stream.error()));
		}
		if (commitcnt)
		{
			if (!transaction->commit())
			{
				throw std::runtime_error( _TXT("transaction commit failed"));
			}
			commitcnt = 0;
			transaction.reset( storage.createTransaction());
			if (!transaction.get()) throw strus::runtime_error( "%s",  _TXT("failed to recreate storage transaction after commit"));
		}
		return rt;
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("error on line %u: %s"), (unsigned int)linecnt, err.what());
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentMetaDataAssignments(
		StorageClientInterface& storage,
		const std::string& metadataName,
		const std::multimap<std::string,strus::Index>* attributemapref,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		return loadStorageValues( storage, metadataName, attributemapref, file, StorageValueMetaData, commitsize);
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory loading meta data assignments"));
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error loading meta data assignments: %s"), e.what());
		return 0;
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentAttributeAssignments(
		StorageClientInterface& storage,
		const std::string& attributeName,
		const std::multimap<std::string,strus::Index>* attributemapref,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		return loadStorageValues( storage, attributeName, attributemapref, file, StorageValueAttribute, commitsize);
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory loading attribute assignments"));
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error loading attribute assignments: %s"), e.what());
		return 0;
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentUserRightsAssignments(
		StorageClientInterface& storage,
		const std::multimap<std::string,strus::Index>* attributemapref,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		return loadStorageValues( storage, std::string(), attributemapref, file, StorageUserRights, commitsize);
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory loading user right assignments"));
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error loading user right assignments: %s"), e.what());
		return 0;
	}
}

#ifdef STRUS_LOWLEVEL_DEBUG
static void print_value_seq( const void* sq, unsigned int sqlen)
{
	static const char* HEX = "0123456789ABCDEF";
	unsigned char const* si = (const unsigned char*) sq;
	unsigned const char* se = (const unsigned char*) sq + sqlen;
	for (; si != se; ++si)
	{
		unsigned char lo = *si % 16, hi = *si / 16;
		printf( " %c%c", HEX[hi], HEX[lo]);
	}
	printf(" |");
}
#endif

static void loadVectorStorageVectors_word2vecBin( 
		VectorStorageClientInterface* client,
		const std::string& vectorfile,
		bool networkOrder,
		ErrorBufferInterface* errorhnd)
{
	unsigned int linecnt = 0;
	try
	{
		Reference<VectorStorageTransactionInterface> transaction( client->createTransaction());
		if (!transaction.get()) throw std::runtime_error( _TXT("create transaction failed"));

		InputStream infile( vectorfile);
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to open word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
		unsigned int collsize;
		unsigned int vecsize;
		std::size_t linesize;
		{
			// Read first text line, that contains two numbers, the collection size and the vector size:
			char firstline[ 256];
			linesize = infile.readAhead( firstline, sizeof(firstline)-1);
			firstline[ linesize] = '\0';
			char const* si = firstline;
			const char* se = std::strchr( si, '\n');
			if (!se) throw std::runtime_error( _TXT("failed to parse header line"));
			skipSpaces( si);
			if (!is_UNSIGNED(si)) throw std::runtime_error( _TXT("expected collection size as first element of the header line"));
			collsize = parse_UNSIGNED1( si);
			skipSpaces( si);
			if (!is_UNSIGNED(si)) throw std::runtime_error( _TXT("expected vector size as second element of the header line"));
			vecsize = parse_UNSIGNED1( si);
			if (*(si-1) != '\n')
			{
				skipToEoln( si);
				++si;
			}
			infile.read( firstline, si - firstline);
		}
		// Declare buffer for reading lines:
		struct charp_scope
		{
			charp_scope( char* ptr_)	:ptr(ptr_){}
			~charp_scope()			{if (ptr) std::free(ptr);}
			char* ptr;
		};
		enum {MaxIdSize = 2048};
		std::size_t linebufsize = MaxIdSize + vecsize * sizeof(float);
		char* linebuf = (char*)std::malloc( linebufsize);
		charp_scope linebuf_scope( linebuf);
	
		// Parse vector by vector and add them to the transaction till EOF:
		linesize = infile.readAhead( linebuf, linebufsize);
		while (linesize)
		{
			++linecnt;
			char const* si = linebuf;
			const char* se = linebuf + linesize;
			for (; si < se && (unsigned char)*si > 32; ++si){}
			const char* term = linebuf;
			std::size_t termsize = si - linebuf;
			++si;
			if (si+vecsize*sizeof(float) > se)
			{
				throw strus::runtime_error( "%s",  _TXT("wrong file format"));
			}
#ifdef STRUS_LOWLEVEL_DEBUG
			for (std::size_t ti=0; ti<termsize; ++ti) printf("%c",term[ti]);
#endif
			std::vector<double> vec;
			vec.reserve( vecsize);
			unsigned int ii = 0;
			if (networkOrder)
			{
				for (; ii < vecsize; ii++)
				{
					float_net_t val;
#ifdef STRUS_LOWLEVEL_DEBUG
					print_value_seq( si, sizeof( float));
#endif
					std::memcpy( (void*)&val, si, sizeof( val));
					si += sizeof( float);
					vec.push_back( ByteOrder<float>::ntoh( val));
				}
			}
			else
			{
				for (; ii < vecsize; ii++)
				{
					float val;
#ifdef STRUS_LOWLEVEL_DEBUG
					print_value_seq( si, sizeof( float));
#endif
					std::memcpy( (void*)&val, si, sizeof( val));
					si += sizeof( float);
					vec.push_back( val);
				}
			}
#ifdef STRUS_LOWLEVEL_DEBUG
			printf("\n");
#endif
			double len = 0;
			std::vector<double>::iterator vi = vec.begin(), ve = vec.end();
			for (; vi != ve; ++vi)
			{
				double vv = *vi;
				len += vv * vv;
			}
			len = sqrt( len);
			vi = vec.begin(), ve = vec.end();
			for (; vi != ve; ++vi)
			{
				*vi /= len;
				if (*vi >= -1.0 && *vi <= 1.0)
				{/*OK*/}
				else
				{
					throw strus::runtime_error( _TXT("illegal value in vector: %f %f"), *vi, len);
				}
			}
			transaction->addFeature( std::string(term, termsize), vec);
			if (errorhnd->hasError())
			{
				throw strus::runtime_error(_TXT("add vector failed: %s"), errorhnd->fetchError());
			}
			if (*si == '\n')
			{
				++si;
			}
			else
			{
				throw strus::runtime_error(_TXT("end of line marker expected after binary vector instead of '%x'"), (unsigned int)(unsigned char)*si);
			}
			infile.read( linebuf, si - linebuf);
			linesize = infile.readAhead( linebuf, linebufsize);
		}
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to read from word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
		if (collsize != linecnt)
		{
			throw std::runtime_error( _TXT("collection size does not match"));
		}
		if (!transaction->commit())
		{
			throw strus::runtime_error(_TXT("vector storage transaction failed: %s"), errorhnd->fetchError());
		}
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("in word2vec binary file in record %u: %s"), linecnt, err.what());
	}
}

static void loadVectorStorageVectors_word2vecText( 
		VectorStorageClientInterface* client,
		const std::string& vectorfile,
		ErrorBufferInterface* errorhnd)
{
	unsigned int linecnt = 0;
	try
	{
		Reference<VectorStorageTransactionInterface> transaction( client->createTransaction());
		if (!transaction.get()) throw std::runtime_error( _TXT("create transaction failed"));
		InputStream infile( vectorfile);
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to open word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
		enum {LineBufSize=1<<20};
		struct charp_scope
		{
			charp_scope( char* ptr_)	:ptr(ptr_){}
			~charp_scope()			{if (ptr) std::free(ptr);}
			char* ptr;
		};
		char* linebuf = (char*)std::malloc( LineBufSize);
		charp_scope linebuf_scope(linebuf);
		const char* line = infile.readLine( linebuf, LineBufSize);
		for (; line; line = infile.readLine( linebuf, LineBufSize))
		{
			char const* si = line;
			const char* se = si + std::strlen(si);
			if (se - si == LineBufSize-1) throw std::runtime_error( _TXT("input line too long"));
			++linecnt;
			const char* term;
			std::size_t termsize;
			std::vector<double> vec;
			while (isSpace( *si)) ++si;
			term = si;
		AGAIN:
			for (; *si && *si != ' ' && *si != '\t'; ++si){}
			if (!*si)
			{
				throw std::runtime_error( _TXT("unexpected end of file"));
			}
			termsize = si - term;
			++si;
			if (!isMinus(*si) && !isDigit(*si))
			{
				goto AGAIN;
			}
			while (isSpace( *si)) ++si;
			while (si < se && is_FLOAT(si))
			{
				vec.push_back( parse_FLOAT( si));
				while (isSpace( *si)) ++si;
			}
			if (si < se)
			{
				throw std::runtime_error( _TXT("expected vector of double precision floating point numbers after term definition"));
			}
			double len = 0;
			std::vector<double>::iterator vi = vec.begin(), ve = vec.end();
			for (; vi != ve; ++vi)
			{
				double vv = *vi;
				len += vv * vv;
			}
			len = sqrt( len);
			for (; vi != ve; vi++)
			{
				*vi /= len;
				if (*vi >= -1.0 && *vi <= 1.0)
				{/*OK*/}
				else
				{
					throw strus::runtime_error( _TXT("illegal value in vector: %f %f"), *vi, len);
				}
			}
			transaction->addFeature( std::string(term, termsize), vec);
			if (errorhnd->hasError())
			{
				throw strus::runtime_error(_TXT("add vector failed: %s"), errorhnd->fetchError());
			}
		}
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to read from word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
		if (!transaction->commit())
		{
			throw strus::runtime_error(_TXT("vector storage transaction failed: %s"), errorhnd->fetchError());
		}
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("in word2vec text file on line %u: %s"), linecnt, err.what());
	}
}

DLL_PUBLIC bool strus::loadVectorStorageVectors( 
		VectorStorageClientInterface* client,
		const std::string& vectorfile,
		bool networkOrder,
		ErrorBufferInterface* errorhnd)
{
	char const* filetype = 0;
	try
	{
		if (isTextFile( vectorfile))
		{
			filetype = "word2vec text file";
			loadVectorStorageVectors_word2vecText( client, vectorfile, errorhnd);
		}
		else
		{
			filetype = "word2vec binary file";
			loadVectorStorageVectors_word2vecBin( client, vectorfile, networkOrder, errorhnd);
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( ErrorCodeOutOfMem, _TXT("out of memory loading feature vectors from file %s (file format: %s)"), vectorfile.c_str(), filetype);
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( ErrorCodeRuntimeError, _TXT("error loading feature vectors from file %s (file format: %s): %s"), vectorfile.c_str(), filetype, e.what());
		return false;
	}
}


