/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "private/programLoader.hpp"
#include "strus/constants.hpp"
#include "strus/numericVariant.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/metaDataRestrictionInterface.hpp"
#include "strus/queryAnalyzerInstanceInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/analyzer/queryTermExpression.hpp"
#include "strus/reference.hpp"
#include "strus/base/snprintf.h"
#include "strus/base/string_format.hpp"
#include "strus/base/dll_tags.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/programLexer.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/numstring.hpp"
#include "private/internationalization.hpp"
#include "private/errorUtils.hpp"
#include "metadataExpression.hpp"
#include "termExpression.hpp"
#include <string>
#include <vector>
#include <stdexcept>

using namespace strus;

enum Tokens {
	TokIdentifier,
	TokFloat,
	TokInteger,
	TokOpenOvalBracket,
	TokCloseOvalBracket,
	TokOpenCurlyBracket,
	TokCloseCurlyBracket,
	TokOpenSquareBracket,
	TokCloseSquareBracket,
	TokOr,
	TokAssign,
	TokCompareNotEqual,
	TokCompareEqual,
	TokCompareGreaterEqual,
	TokCompareGreater,
	TokCompareLessEqual,
	TokCompareLess,
	TokDot,
	TokComma,
	TokColon,
	TokSemiColon,
	TokTilde,
	TokExp,
	TokAsterisk,
	TokLeftArrow,
	TokPath
};
static const char* g_tokens[] = {
	"[a-zA-Z_][a-zA-Z0-9_]*",
	"[+-]*[0-9][0-9_]*[.][0-9]*",
	"[+-]*[0-9][0-9_]*",
	"\\(",
	"\\)",
	"\\{",
	"\\}",
	"\\[",
	"\\]",
	"\\|",
	"\\=",
	"\\!\\=",
	"\\=\\=",
	"\\>\\=",
	"\\>",
	"\\<\\=",
	"\\<",
	"[.]",
	"\\,",
	"\\:",
	"\\;",
	"\\~",
	"\\^",
	"\\*"
	"<-",
	"[/][^;,{} ]*",
	NULL
};
static const char* g_token_names[] = {
	"identifier",
	"flating point number",
	"integer",
	"open oval bracket '('",
	"close oval bracket ')'",
	"open curly bracket '{'",
	"close curly bracket '}'",
	"open square bracket '['",
	"close square bracket ']'",
	"or operator '|'",
	"assign '='",
	"not equl (\\!\\=)",
	"equality comparis operator \\=\\=",
	"greater equal comparis operator \\>\\=",
	"greater comparis operator \\>",
	"lesser equal comparin operator \\<\\=",
	"lesser equal comparison operator \\<",
	"dot '.'",
	"comma ','",
	"colon ':'",
	"semicolon ';'",
	"tilde '^",
	"exponent '^",
	"asterisk';'",
	"left arrow '<-'",
	"path",
	NULL
};
static const char* g_errtokens[] = {
	"[0-9][0-9]*[a-zA-Z_]",
	NULL
};
static const char* g_eolncomment = "#";

static const char* tokenName( const ProgramLexem& cur)
{
	switch (cur.type())
	{
		case ProgramLexem::Eof:		return "EOF";
		case ProgramLexem::SQString:	return "string";
		case ProgramLexem::DQString:	return "string";
		case ProgramLexem::Error:	return "bad lexem";
		case ProgramLexem::Token:	return g_token_names[ cur.id()];
	}
	return "?";
}

static void reportErrorWithLocation( ErrorBufferInterface* errorhnd, ProgramLexer& lexer, const char* msg, const char* what)
{
	try
	{
		std::string errorlocation = lexer.currentLocationString( -30, 80, "<!>");
		std::string errormsg;
		if (what)
		{
			errormsg = strus::string_format(
				_TXT("error in source on line %d (at %s): %s: %s"),
				(int)lexer.lineno(), errorlocation.c_str(), msg, what);
		}
		else
		{
			errormsg = strus::string_format(
				_TXT("error in source on line %d (at %s): %s"),
				(int)lexer.lineno(), errorlocation.c_str(), msg);
		}
		errorhnd->report( ErrorCodeSyntax, "%s", errormsg.c_str());
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( ErrorCodeOutOfMem, _TXT("%s: out of memory handling error"), msg);
	}
}

/// \brief Some default settings for parsing and building the query
struct QueryDescriptors
{
	std::set<std::string> fieldset;			///< set of defined query fields
	std::set<std::string> typeset;			///< set of defined query fields
	std::string defaultFieldType;			///< true if a field type name with name default has been specified
	std::string selectionFeatureSet;		///< feature sets used for document selection
	std::string weightingFeatureSet;		///< feature sets used for document weighting
	float defaultSelectionTermPart;			///< default percentage of weighting terms required in selection
	std::string defaultSelectionJoin;		///< default operator used to join terms for selection

	QueryDescriptors( const std::vector<std::string>& fieldnames, const std::vector<std::string>& termtypes)
		:fieldset(),typeset(),defaultFieldType(),selectionFeatureSet(),weightingFeatureSet()
		,defaultSelectionTermPart(1.0),defaultSelectionJoin("contains")
	{
		std::vector<std::string>::const_iterator fi = fieldnames.begin(), fe = fieldnames.end();
		for (; fi != fe; ++fi)
		{
			fieldset.insert( *fi);
		}
		std::vector<std::string>::const_iterator ti = termtypes.begin(), te = termtypes.end();
		for (; ti != te; ++ti)
		{
			typeset.insert( *ti);
		}
		if (fieldset.size() >= 1)
		{
			defaultFieldType = fieldnames[0];
		}
	}
	QueryDescriptors( const QueryDescriptors& o)
		:fieldset(o.fieldset)
		,typeset(o.typeset)
		,defaultFieldType(o.defaultFieldType)
		,selectionFeatureSet(o.selectionFeatureSet)
		,weightingFeatureSet(o.weightingFeatureSet)
		,defaultSelectionTermPart(o.defaultSelectionTermPart)
		,defaultSelectionJoin(o.defaultSelectionJoin)
		{}
};


static std::string parseVariableRef( ProgramLexer& lexer)
{
	std::string rt;
	if (lexer.current().isToken(TokAssign))
	{
		rt = lexer.next().value();
		lexer.next();
	}
	return rt;
}

static bool isQueryStructureExpression( ProgramLexer& lexer)
{
	bool rt = false;
	const char* curpos = lexer.currentpos();
	if (lexer.current().isToken(TokIdentifier))
	{
		lexer.next();
		rt = lexer.current().isToken(TokOpenOvalBracket);
	}
	lexer.skipto( curpos);
	return rt;
}

static bool isCompareOperator( MetaDataRestrictionInterface::CompareOperator& opr, ProgramLexer& lexer)
{
	if (lexer.current().isToken(TokAssign))
	{
		opr = MetaDataRestrictionInterface::CompareEqual;
		return true;
	}
	else if (lexer.current().isToken(TokCompareNotEqual))
	{
		opr = MetaDataRestrictionInterface::CompareNotEqual;
		return true;
	}
	else if (lexer.current().isToken(TokCompareEqual))
	{
		opr = MetaDataRestrictionInterface::CompareEqual;
		return true;
	}
	else if (lexer.current().isToken(TokCompareGreaterEqual))
	{
		opr = MetaDataRestrictionInterface::CompareGreaterEqual;
		return true;
	}
	else if (lexer.current().isToken(TokCompareGreater))
	{
		opr = MetaDataRestrictionInterface::CompareGreater;
		return true;
	}
	else if (lexer.current().isToken(TokCompareLessEqual))
	{
		opr = MetaDataRestrictionInterface::CompareLessEqual;
		return true;
	}
	else if (lexer.current().isToken(TokCompareLess))
	{
		opr = MetaDataRestrictionInterface::CompareLess;
		return true;
	}
	else
	{
		return false;
	}
}

static bool isQueryMetaDataExpression( ProgramLexer& lexer)
{
	bool rt = false;
	const char* curpos = lexer.currentpos();
	if (lexer.current().isToken(TokIdentifier))
	{
		lexer.next();
		MetaDataRestrictionInterface::CompareOperator opr;
		rt = isCompareOperator( opr, lexer);
	}
	lexer.skipto( curpos);
	return rt;
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
		ProgramLexer& lexer)
{
	std::string fieldName;
	std::vector<std::string> values;
	MetaDataRestrictionInterface::CompareOperator opr;
	if (lexer.current().isToken(TokIdentifier))
	{
		fieldName = lexer.current().value();
		lexer.next();
		if (!isCompareOperator( opr, lexer))
		{
			throw strus::runtime_error( _TXT("expected compare operator instead of %s"), tokenName( lexer.current()));
		}
		do
		{
			lexer.next();
			if (!lexer.current().isToken(TokIdentifier) && !lexer.current().isToken(TokInteger) && !lexer.current().isToken(TokFloat) && !lexer.current().isString())
			{
				throw strus::runtime_error( _TXT("metadata value expected instead of %s"), tokenName( lexer.current()));
			}
			values.push_back( lexer.current().value());
		} while (lexer.next().isToken(TokComma));
	}
	else if (lexer.current().isString() || lexer.current().isToken(TokInteger) || lexer.current().isToken(TokFloat))
	{
		values.push_back( lexer.current().value());
		lexer.next();

		while (lexer.current().isToken(TokComma))
		{
			lexer.next();
			if (!lexer.current().isToken(TokIdentifier) && !lexer.current().isToken(TokInteger) && !lexer.current().isToken(TokFloat) && !lexer.current().isString())
			{
				throw strus::runtime_error( _TXT("metadata value expected instead of %s"), tokenName( lexer.current()));
			}
			values.push_back( lexer.current().value());
			lexer.next();
		}
		if (!isCompareOperator( opr, lexer))
		{
			throw strus::runtime_error( _TXT("expected compare operator instead of %s"), tokenName( lexer.current()));
		}
		if (lexer.next().isToken(TokIdentifier))
		{
			fieldName = lexer.current().value();
			lexer.next();
		}
		opr = invertCompareOperator( opr);
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
		QueryDescriptors& qdescr,
		ProgramLexer& lexer)
{
	bool isSelection = true;
	if (lexer.current().isToken( TokTilde))
	{
		lexer.next();
		isSelection = false;
	}
	std::string field;
	std::string fieldType;

	if (lexer.current().isString() || lexer.current().isToken(TokIdentifier))
	{
		field = lexer.current().value();
		if (lexer.next().isToken(TokColon))
		{
			if (lexer.next().isToken(TokIdentifier))
			{
				fieldType = lexer.current().value();
				qdescr.fieldset.insert( fieldType);
				lexer.next();
			}
		}
		else
		{
			if (qdescr.defaultFieldType.empty())
			{
				throw std::runtime_error( _TXT("cannot handle query fields without explicit naming"));
			}
			fieldType = qdescr.defaultFieldType;
		}
	}
	else if (lexer.current().isToken(TokColon))
	{
		if (lexer.next().isToken(TokIdentifier))
		{
			fieldType = lexer.current().value();
			lexer.next();
		}
		else
		{
			throw strus::runtime_error( _TXT("feature type (identifier) expected after colon ':' in query"));
		}
	}
	else
	{
		throw strus::runtime_error( _TXT("syntax error in query, query expression or term expected"));
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
	std::string variableName = parseVariableRef( lexer);
	if (!variableName.empty())
	{
		termExpression.attachVariable( variableName);
	}
}

static void parseQueryStructureExpression(
		TermExpression& termExpression,
		TermExpression& selectedTermExpression,
		QueryDescriptors& qdescr,
		ProgramLexer& lexer)
{
	if (!lexer.current().isToken(TokIdentifier))
	{
		throw std::runtime_error( _TXT("identifier expected at start of query expression"));
	}
	std::string functionName = lexer.current().value();
	lexer.next();
	if (!lexer.current().isToken(TokOpenOvalBracket))
	{
		throw std::runtime_error( _TXT("internal: bad lookahead in query parser"));
	}
	lexer.next();
	std::size_t argc = 0;

	if (!lexer.current().isEof() && !lexer.current().isToken(TokCloseOvalBracket) && !lexer.current().isToken(TokOr) && !lexer.current().isToken(TokExp))
	{
		do
		{
			argc++;
			if (isQueryStructureExpression(lexer))
			{
				parseQueryStructureExpression( termExpression, selectedTermExpression, qdescr, lexer);
			}
			else
			{
				parseQueryTermExpression( termExpression, selectedTermExpression, qdescr, lexer);
			}
		} while (lexer.consumeToken(TokComma));
	}
	int range = 0;
	unsigned int cardinality = 0;
	while (lexer.current().isToken(TokOr) || lexer.current().isToken(TokExp))
	{
		if (lexer.consumeToken(TokOr))
		{
			if (range != 0) throw strus::runtime_error( _TXT("%s specified twice"), "range");
			if (lexer.current().isToken(TokInteger))
			{
				range = numstring_conv::toint( lexer.current().value(), std::numeric_limits<int>::max());
				lexer.next();
			}
			else
			{
				throw strus::runtime_error( _TXT("%s should be an integer"), "range");
			}
			if (range == 0) throw strus::runtime_error( _TXT("%s should be a non null number"), "range");
		}
		else if (lexer.consumeToken(TokExp))
		{
			if (cardinality != 0) throw strus::runtime_error( _TXT("%s specified twice"), "cardinality");
			if (lexer.current().isToken(TokInteger))
			{
				cardinality = numstring_conv::toint( lexer.current().value(), std::numeric_limits<int>::max());
				lexer.next();
			}
			else
			{
				throw strus::runtime_error( _TXT("%s should be an integer"), "cardinality");
			}
		}
	}
	if (!lexer.current().isToken(TokCloseOvalBracket))
	{
		throw strus::runtime_error( _TXT("close oval bracket ')' expected as end of a query structure expression expected"));
	}
	lexer.next();
	std::string variableName = parseVariableRef( lexer);
	termExpression.pushExpression( functionName, argc, range, cardinality);
	if (!variableName.empty())
	{
		termExpression.attachVariable( variableName);
	}
}


DLL_PUBLIC bool strus::loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInstanceInterface* analyzer,
		const std::string& selectionFeatureSet,
		const std::string& weightingFeatureSet,
		const QueryProcessorInterface* queryproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	ProgramLexer lexer( source.c_str(), g_eolncomment, g_tokens, g_errtokens, errorhnd);
	try
	{
		QueryDescriptors qdescr( analyzer->queryFieldTypes(), analyzer->queryTermTypes());
		qdescr.selectionFeatureSet = selectionFeatureSet;
		qdescr.weightingFeatureSet = weightingFeatureSet;

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

		lexer.next();
		while (!lexer.current().isEof())
		{
			// Parse query section:
			if (isQueryMetaDataExpression( lexer))
			{
				parseMetaDataExpression( metaDataExpression, lexer);
			}
			else
			{
				if (isQueryStructureExpression( lexer))
				{
					parseQueryStructureExpression( termExpression, selectedTermExpression, qdescr, lexer);
				}
				else
				{
					parseQueryTermExpression( termExpression, selectedTermExpression, qdescr, lexer);
				}
				double featureWeight = 1.0;
				if (lexer.current().isToken( TokAsterisk))
				{
					lexer.next();
					if (lexer.current().isToken(TokInteger) || lexer.current().isToken(TokFloat))
					{
						featureWeight = numstring_conv::todouble( lexer.current().value());
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
		reportErrorWithLocation( errorhnd, lexer, _TXT("out or memory loading query"), 0);
		return false;
	}
	catch (const std::runtime_error& e)
	{
		reportErrorWithLocation( errorhnd, lexer, _TXT("error loading query"), e.what());
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
	CATCH_ERROR_MAP_RETURN( "error scanning next program: %s", *errorhnd, false);
}
