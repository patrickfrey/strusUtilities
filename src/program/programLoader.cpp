/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/programLoader.hpp"
#include "lexems.hpp"
#include "strus/constants.hpp"
#include "strus/numericVariant.hpp"
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
#include "strus/patternLexerInterface.hpp"
#include "strus/patternLexerInstanceInterface.hpp"
#include "strus/patternMatcherInterface.hpp"
#include "strus/patternMatcherInstanceInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/metaDataRestrictionInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/storageDocumentUpdateInterface.hpp"
#include "strus/vectorStorageBuilderInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/term.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/reference.hpp"
#include "strus/base/snprintf.h"
#include "strus/base/string_format.hpp"
#include "strus/base/dll_tags.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/hton.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/symbolTable.hpp"
#include "private/utils.hpp"
#include "private/internationalization.hpp"
#include "queryStruct.hpp"
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
		throw strus::runtime_error(_TXT("query term (identifier,word,number or string) expected"));
	}
}

static void parseTermConfig(
		QueryEvalInterface& qeval,
		QueryDescriptors& qdescr,
		char const*& src)
{
	if (isAlpha(*src))
	{
		std::string termset = utils::tolower( parse_IDENTIFIER( src));
		if (!isStringQuote( *src) && !isTextChar( *src))
		{
			throw strus::runtime_error(_TXT( "term value (string,identifier,number) after the feature identifier"));
		}
		std::string termvalue = parseQueryTerm( src);
		if (!isColon( *src))
		{
			throw strus::runtime_error(_TXT( "colon (':') expected after term value"));
		}
		(void)parse_OPERATOR(src);
		if (!isAlpha( *src))
		{
			throw strus::runtime_error(_TXT( "term type identifier expected after colon and term value"));
		}
		std::string termtype = utils::tolower( parse_IDENTIFIER( src));
		qeval.addTerm( termset, termtype, termvalue);
	}
	else
	{
		throw strus::runtime_error(_TXT( "feature set identifier expected as start of a term declaration in the query"));
	}
}

static NumericVariant parseNumericValue( char const*& src)
{
	if (is_INTEGER(src))
	{
		if (isMinus(*src) || isPlus(*src))
		{
			return NumericVariant( parse_INTEGER( src));
		}
		else
		{
			if (isPlus(*src))
			{
				parse_OPERATOR( src);
				if (isMinus(*src)) throw strus::runtime_error( _TXT( "unexpected minus '-' operator after plus '+'"));
			}
			while (*src == '0') ++src;
			if (*src >= '1' && *src <= '9')
			{
				return NumericVariant( parse_UNSIGNED( src));
			}
			else
			{
				skipSpaces(src);
				return NumericVariant( 0);
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
		throw strus::runtime_error(_TXT( "weighting formula string expected"));
	}
	std::string funcsrc = parse_STRING( src);
	const ScalarFunctionParserInterface* scalarfuncparser = queryproc->getScalarFunctionParser(langName);
	std::auto_ptr<ScalarFunctionInterface> scalarfunc( scalarfuncparser->createFunction( funcsrc, std::vector<std::string>()));
	if (!scalarfunc.get())
	{
		throw strus::runtime_error(_TXT( "failed to create scalar function (weighting formula) from source"));
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
		throw strus::runtime_error(_TXT( "weighting function identifier expected"));
	}
	std::string functionName = parse_IDENTIFIER( src);

	const WeightingFunctionInterface* wf = queryproc->getWeightingFunction( functionName);
	if (!wf) throw strus::runtime_error(_TXT( "weighting function '%s' not defined"), functionName.c_str());

	std::auto_ptr<WeightingFunctionInstanceInterface> function( wf->createInstance( queryproc));
	if (!function.get()) throw strus::runtime_error(_TXT( "failed to create weighting function '%s'"), functionName.c_str());

	typedef QueryEvalInterface::FeatureParameter FeatureParameter;
	std::vector<FeatureParameter> featureParameters;

	if (!isOpenOvalBracket( *src))
	{
		throw strus::runtime_error(_TXT( "open oval bracket '(' expected after weighting function identifier"));
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
			throw strus::runtime_error(_TXT( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected"));
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw strus::runtime_error(_TXT( "assingment operator '=' expected after weighting function parameter name"));
		}
		(void)parse_OPERATOR(src);
		if (isDigit(*src) || isMinus(*src) || isPlus(*src))
		{
			if (isFeatureParam)
			{
				throw strus::runtime_error(_TXT( "feature parameter argument must be an identifier or string and not a number"));
			}
			NumericVariant parameterValue = parseNumericValue( src);
			function->addNumericParameter( parameterName, parameterValue);
		}
		else if (isStringQuote(*src))
		{
			std::string parameterValue = parse_STRING( src);
			if (isFeatureParam)
			{
				if (qdescr.weightingFeatureSet.empty())
				{
					qdescr.weightingFeatureSet = parameterValue;
				}
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
				if (qdescr.weightingFeatureSet.empty())
				{
					qdescr.weightingFeatureSet = parameterValue;
				}
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
		throw strus::runtime_error(_TXT( "close oval bracket ')' expected at end of weighting function parameter list"));
	}
	(void)parse_OPERATOR(src);
	qeval.addWeightingFunction( functionName, function.get(), featureParameters); 
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
		throw strus::runtime_error(_TXT( "name of summarizer function expected at start of summarizer definition"));
	}
	functionName = utils::tolower( parse_IDENTIFIER( src));

	const SummarizerFunctionInterface* sf = queryproc->getSummarizerFunction( functionName);
	if (!sf) throw strus::runtime_error(_TXT( "summarizer function not defined: '%s'"), functionName.c_str());

	std::auto_ptr<SummarizerFunctionInstanceInterface> function( sf->createInstance( queryproc));
	if (!function.get()) throw strus::runtime_error(_TXT( "failed to create summarizer function instance '%s'"), functionName.c_str());

	if (!isOpenOvalBracket( *src))
	{
		throw strus::runtime_error(_TXT( "open oval bracket '(' expected after summarizer function identifier"));
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
			throw strus::runtime_error(_TXT( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected"));
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw strus::runtime_error(_TXT( "assignment operator '=' expected after summarizer function parameter name"));
		}
		(void)parse_OPERATOR(src);
		if (isDigit(*src) || isMinus(*src) || isPlus(*src))
		{
			if (isFeatureParam)
			{
				throw strus::runtime_error(_TXT( "feature parameter argument must be an identifier or string and not a number"));
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
		throw strus::runtime_error(_TXT( "close oval bracket ')' expected at end of summarizer function parameter list"));
	}
	(void)parse_OPERATOR(src);
	qeval.addSummarizerFunction( functionName, function.get(), featureParameters);
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
	enum StatementKeyword {e_FORMULA, e_EVAL, e_SELECTION, e_RESTRICTION, e_TERM, e_SUMMARIZE};
	std::string id;

	skipSpaces( src);
	try
	{
		while (*src)
		{
			switch ((StatementKeyword)parse_KEYWORD( src, 6, "FORMULA", "EVAL", "SELECT", "RESTRICT", "TERM", "SUMMARIZE"))
			{
				case e_TERM:
					parseTermConfig( qeval, qdescr, src);
					break;
				case e_SELECTION:
					while (*src && isAlnum( *src))
					{
						qdescr.selectionFeatureSet = parse_IDENTIFIER(src);
						qeval.addSelectionFeature( qdescr.selectionFeatureSet);
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
					throw strus::runtime_error(_TXT( "semicolon expected as delimiter of query eval program instructions"));
				}
				(void)parse_OPERATOR( src);
			}
		}
		if (qdescr.selectionFeatureSet.empty())
		{
			throw strus::runtime_error(_TXT("no selection defined in query evaluation configuration"));
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("out of memory parsing query evaluation program %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("error in query evaluation program %s: %s"), pos.c_str(), e.what());
		return false;
	}
}

enum FeatureClass
{
	FeatSearchIndexTerm,
	FeatForwardIndexTerm,
	FeatMetaData,
	FeatAttribute,
	FeatPatternLexem,
	FeatPatternMatch,
	FeatSubDocument,
	FeatAggregator
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
	if (isEqual( name, "PatternLexem"))
	{
		return FeatPatternLexem;
	}
	if (isEqual( name, "PatternMatch"))
	{
		return FeatPatternMatch;
	}
	if (isEqual( name, "Document"))
	{
		return FeatSubDocument;
	}
	if (isEqual( name, "Aggregator"))
	{
		return FeatAggregator;
	}
	throw strus::runtime_error( _TXT( "illegal feature class name '%s' (expected one of {SearchIndex, ForwardIndex, MetaData, Attribute, Document, Aggregator})"), name.c_str());
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
		else if (isDigit(*src) || isMinus(*src) || isPlus(*src))
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
				if (isPlus(*src))
				{
					parse_OPERATOR( src);
					if (isMinus(*src)) throw strus::runtime_error( _TXT( "unexpected minus '-' operator after plus '+'"));
				}
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
			throw strus::runtime_error( _TXT("unknown type in argument list"));
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

static void parseFunctionDef( const char* functype, std::string& name, std::vector<std::string>& arg, char const*& src)
{
	if (isAlpha(*src))
	{
		name = parse_IDENTIFIER( src);
		if (isOpenOvalBracket( *src))
		{
			(void)parse_OPERATOR(src);
			if (isCloseOvalBracket( *src))
			{
				(void)parse_OPERATOR( src);
			}
			else
			{
				arg = parseArgumentList( src);
				if (isCloseOvalBracket( *src))
				{
					(void)parse_OPERATOR( src);
				}
				else
				{
					throw strus::runtime_error( _TXT("comma ',' as argument separator or close oval brakcet ')' expected at end of %s argument list"), functype);
				}
			}
		}
		else
		{
			arg.clear();
		}
	}
	else
	{
		throw strus::runtime_error( _TXT("%s definition (identifier) expected"), functype);
	}
}


/// \brief Description of a function (tokenizer/normalizer)
class FunctionConfig
{
public:
	FunctionConfig( const std::string& name_, const std::vector<std::string>& args_)
		:m_name(name_),m_args(args_){}
	FunctionConfig( const FunctionConfig& o)
		:m_name(o.m_name),m_args(o.m_args){}

	/// \brief Get the name of the tokenizer
	const std::string& name() const			{return m_name;}
	/// \brief Get the arguments of the tokenizer
	const std::vector<std::string>& args() const	{return m_args;}

private:
	std::string m_name;
	std::vector<std::string> m_args;
};

static std::vector<FunctionConfig> parseNormalizerConfig( char const*& src)
{
	std::vector<FunctionConfig> rt;
	for(;;)
	{
		std::string name;
		std::vector<std::string> arg;
		parseFunctionDef( "normalizer", name, arg, src);
		rt.insert( rt.begin(), FunctionConfig( name, arg));

		if (!isColon(*src)) break;
		(void)parse_OPERATOR( src);
	}
	return rt;
}

static FunctionConfig parseTokenizerConfig( char const*& src)
{
	std::string name;
	std::vector<std::string> arg;
	parseFunctionDef( "tokenizer", name, arg, src);
	return FunctionConfig( name, arg);
}

static FunctionConfig parseAggregatorFunctionConfig( char const*& src)
{
	std::string name;
	std::vector<std::string> arg;
	parseFunctionDef( "aggregator function", name, arg, src);
	return FunctionConfig( name, arg);
}


static analyzer::FeatureOptions
	parseFeatureOptions( char const*& src)
{
	analyzer::FeatureOptions rt;
	if (isOpenCurlyBracket(*src))
	{
		do
		{
			(void)parse_OPERATOR(src);
			if (isAlpha(*src))
			{
				std::string optname = parse_IDENTIFIER( src);
				std::string optval;
				if (!isAssign(*src))
				{
					throw strus::runtime_error( _TXT("assign '=' expected after open curly brackets '{' and option identifier"));
				}
				(void)parse_OPERATOR(src);
				if (isStringQuote(*src))
				{
					optval = parse_STRING( src);
				}
				else if (isAlnum(*src))
				{
					optval = parse_IDENTIFIER( src);
				}
				else
				{
					throw strus::runtime_error( _TXT("identifier or string expected as option value"));
				}
				if (utils::caseInsensitiveEquals( optname, "position"))
				{
					if (utils::caseInsensitiveEquals( optval, "succ"))
					{
						rt.definePositionBind( analyzer::BindSuccessor);
					}
					else if (utils::caseInsensitiveEquals( optval, "pred"))
					{
						rt.definePositionBind( analyzer::BindPredecessor);
					}
					else
					{
						throw strus::runtime_error( _TXT("'pred' or 'succ' expected as 'position' option value instead of '%s'"), optval.c_str());
					}
				}
				else
				{
					throw strus::runtime_error( _TXT("unknown option '%s'"), optname.c_str());
				}
			}
		}
		while (isComma( *src));

		if (!isCloseCurlyBracket( *src))
		{
			throw strus::runtime_error( _TXT("close curly bracket '}' expected at end of option list"));
		}
		(void)parse_OPERATOR( src);
	}
	return rt;
}

static std::string parseSelectorExpression( char const*& src)
{
	if (isStringQuote(*src))
	{
		return parse_STRING( src);
	}
	else
	{
		std::string rt;
		char const* start = src;
		while (*src && *src != ',' && *src != ';' && *src != '{')
		{
			if (*src == '\'' || *src == '\"')
			{
				char eb = *src;
				for (++src; *src && *src != eb; ++src){}
				if (*src) ++src;
			}
			else
			{
				++src;
			}
		}
		rt.append( start, src-start);
		skipSpaces( src);
		return rt;
	}
}

struct FeatureDef
{
	std::auto_ptr<TokenizerFunctionInstanceInterface> tokenizer;
	std::vector<Reference<NormalizerFunctionInstanceInterface> > normalizer_ref;
	std::vector<NormalizerFunctionInstanceInterface*> normalizer;

	~FeatureDef(){}

	void parseNormalizer( char const*& src, const TextProcessorInterface* textproc)
	{
		std::vector<FunctionConfig> normalizercfg = parseNormalizerConfig( src);
		std::vector<FunctionConfig>::const_iterator ni = normalizercfg.begin(), ne = normalizercfg.end();
		for (; ni != ne; ++ni)
		{
			const NormalizerFunctionInterface* nm = textproc->getNormalizer( ni->name());
			if (!nm) throw strus::runtime_error(_TXT( "normalizer function '%s' not found"), ni->name().c_str());
	
			Reference<NormalizerFunctionInstanceInterface> nmi( nm->createInstance( ni->args(), textproc));
			if (!nmi.get()) throw strus::runtime_error(_TXT( "failed to create instance of normalizer function '%s'"), ni->name().c_str());
	
			normalizer_ref.push_back( nmi);
			normalizer.push_back( nmi.get());
		}
	}

	void parseTokenizer( char const*& src, const TextProcessorInterface* textproc)
	{
		FunctionConfig tokenizercfg = parseTokenizerConfig( src);
		const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
		if (!tk) throw strus::runtime_error(_TXT( "tokenizer function '%s' not found"), tokenizercfg.name().c_str());
	
		tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));
		if (!tokenizer.get()) throw strus::runtime_error(_TXT( "failed to create instance of tokenizer function '%s'"), tokenizercfg.name().c_str());
	}

	void release()
	{
		std::vector<Reference<NormalizerFunctionInstanceInterface> >::iterator
			ri = normalizer_ref.begin(), re = normalizer_ref.end();
		for (; ri != re; ++ri)
		{
			(void)ri->release();
		}
		(void)tokenizer.release();
	}
};

static void parseDocumentPatternFeatureDef(
	DocumentAnalyzerInterface& analyzer,
	const TextProcessorInterface* textproc,
	const std::string& featureName,
	char const*& src,
	FeatureClass featureClass)
{
	FeatureDef featuredef;

	// [1] Parse pattern item name:
	if (!isAlpha(*src)) throw strus::runtime_error(_TXT("identifier expected in pattern matcher feature definition after left arrow"));
	std::string patternTypeName = parse_IDENTIFIER(src);

	// [2] Parse normalizer, if defined:
	if (!isSemiColon(*src) && !isOpenCurlyBracket(*src))
	{
		featuredef.parseNormalizer( src, textproc);
	}
	// [3] Parse feature options, if defined:
	analyzer::FeatureOptions featopt( parseFeatureOptions( src));

	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.addSearchIndexFeatureFromPatternMatch( 
				featureName, patternTypeName, featuredef.normalizer, featopt);
			break;

		case FeatForwardIndexTerm:
			analyzer.addForwardIndexFeatureFromPatternMatch( 
				featureName, patternTypeName, featuredef.normalizer, featopt);
			break;

		case FeatMetaData:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for meta data feature"));
			}
			analyzer.defineMetaDataFromPatternMatch( 
				featureName, patternTypeName, featuredef.normalizer);
			break;

		case FeatAttribute:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for attribute feature"));
			}
			analyzer.defineAttributeFromPatternMatch( 
				featureName, patternTypeName, featuredef.normalizer);
			break;

		case FeatPatternLexem:
			throw std::logic_error("cannot define pattern match lexem from pattern match result");

		case FeatPatternMatch:
			throw std::logic_error("illegal call of parse feature definition for pattern match program definition");

		case FeatSubDocument:
			throw std::logic_error("illegal call of parse feature definition for sub document");

		case FeatAggregator:
			throw std::logic_error("illegal call of parse feature definition for aggregator");
	}
	featuredef.release();
}

static void parseQueryPatternFeatureDef(
	QueryAnalyzerInterface& analyzer,
	const TextProcessorInterface* textproc,
	const std::string& featureName,
	char const*& src,
	FeatureClass featureClass)
{
	FeatureDef featuredef;

	// [1] Parse pattern item name:
	if (!isAlpha(*src)) throw strus::runtime_error(_TXT("identifier expected in pattern matcher feature definition after left arrow"));
	std::string patternTypeName = parse_IDENTIFIER(src);

	// [2] Parse normalizer, if defined:
	if (!isSemiColon(*src))
	{
		featuredef.parseNormalizer( src, textproc);
	}
	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.addSearchIndexElementFromPatternMatch( 
				featureName, patternTypeName, featuredef.normalizer);
			break;

		case FeatMetaData:
			analyzer.addMetaDataElementFromPatternMatch( 
				featureName, patternTypeName, featuredef.normalizer);
			break;

		case FeatPatternLexem:
			throw std::logic_error("cannot define pattern match lexem from pattern match result in query");
		case FeatPatternMatch:
			throw std::logic_error("illegal call of parse feature definition for pattern match program definition in query");
		case FeatForwardIndexTerm:
			throw std::logic_error("illegal call of parse feature definition for forward index feature in query");
		case FeatAttribute:
			throw std::logic_error("illegal call of parse feature definition for attribute in query");
		case FeatSubDocument:
			throw std::logic_error("illegal call of parse feature definition for sub document in query");
		case FeatAggregator:
			throw std::logic_error("illegal call of parse feature definition for aggregator in query");
	}
	featuredef.release();
}

static void parseDocumentFeatureDef(
	DocumentAnalyzerInterface& analyzer,
	const TextProcessorInterface* textproc,
	const std::string& featureName,
	char const*& src,
	FeatureClass featureClass)
{
	FeatureDef featuredef;
	// [1] Parse normalizer:
	featuredef.parseNormalizer( src, textproc);
	// [2] Parse tokenizer:
	featuredef.parseTokenizer( src, textproc);

	// [3] Parse feature options, if defined:
	analyzer::FeatureOptions featopt( parseFeatureOptions( src));

	// [4] Parse selection expression:
	std::string xpathexpr( parseSelectorExpression( src));

	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.addSearchIndexFeature(
				featureName, xpathexpr,
				featuredef.tokenizer.get(), featuredef.normalizer,
				featopt);
			break;

		case FeatForwardIndexTerm:
			analyzer.addForwardIndexFeature(
				featureName, xpathexpr,
				featuredef.tokenizer.get(), featuredef.normalizer,
				featopt);
			break;

		case FeatMetaData:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for meta data feature"));
			}
			analyzer.defineMetaData(
				featureName, xpathexpr,
				featuredef.tokenizer.get(), featuredef.normalizer);
			break;

		case FeatAttribute:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for attribute feature"));
			}
			analyzer.defineAttribute(
				featureName, xpathexpr,
				featuredef.tokenizer.get(), featuredef.normalizer);
			break;

		case FeatPatternLexem:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for pattern lexem"));
			}
			analyzer.addPatternLexem(
				featureName, xpathexpr,
				featuredef.tokenizer.get(), featuredef.normalizer);
			break;

		case FeatPatternMatch:
			throw std::logic_error("illegal call of parse feature definition for pattern match program definition");

		case FeatSubDocument:
			throw std::logic_error("illegal call of parse feature definition for sub document");

		case FeatAggregator:
			throw std::logic_error("illegal call of parse feature definition for aggregator");
	}
	featuredef.release();
}

static void parseQueryFeatureDef(
	QueryAnalyzerInterface& analyzer,
	QueryDescriptors& qdescr,
	const TextProcessorInterface* textproc,
	const std::string& featureName,
	char const*& src,
	FeatureClass featureClass)
{
	FeatureDef featuredef;
	// [1] Parse normalizer:
	featuredef.parseNormalizer( src, textproc);
	// [2] Parse tokenizer:
	featuredef.parseTokenizer( src, textproc);

	// [3] Parse field type (corresponds to xpath selection in document):
	std::string fieldType;
	if (!isAlpha(*src))
	{
		if (featureClass == FeatMetaData)
		{
			fieldType = featureName;
		}
		else
		{
			throw strus::runtime_error(_TXT("expected field type name"));
		}
	}
	fieldType = parse_IDENTIFIER( src);

	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.addSearchIndexElement(
				featureName, fieldType,
				featuredef.tokenizer.get(), featuredef.normalizer);
			break;

		case FeatMetaData:
			analyzer.addMetaDataElement(
				featureName, fieldType,
				featuredef.tokenizer.get(), featuredef.normalizer);
			break;

		case FeatPatternLexem:
			analyzer.addPatternLexem(
				featureName, fieldType,
				featuredef.tokenizer.get(), featuredef.normalizer);
			break;

		case FeatPatternMatch:
			throw std::logic_error("illegal call of parse feature definition for pattern match program definition");
		case FeatForwardIndexTerm:
			throw std::logic_error("illegal call of parse feature definition for forward index feature in query");
		case FeatAttribute:
			throw std::logic_error("illegal call of parse feature definition for attribute in query");
		case FeatSubDocument:
			throw std::logic_error("illegal call of parse feature definition for sub document in query");
		case FeatAggregator:
			throw std::logic_error("illegal call of parse feature definition for aggregator in query");
	}
	featuredef.release();
}

static FeatureClass parseFeatureClassDef( char const*& src, std::string& domainid)
{
	FeatureClass rt = FeatSearchIndexTerm;
	if (isOpenSquareBracket( *src))
	{
		(void)parse_OPERATOR(src);
		if (!isAlnum(*src))
		{
			throw strus::runtime_error( _TXT("feature class identifier expected after open square bracket '['"));
		}
		rt = featureClassFromName( parse_IDENTIFIER( src));
		if (rt == FeatPatternMatch && isAlnum(*src))
		{
			domainid = parse_IDENTIFIER( src);
		}
		if (!isCloseSquareBracket( *src))
		{
			throw strus::runtime_error( _TXT("close square bracket ']' expected to close feature class section definition"));
		}
		(void)parse_OPERATOR(src);
	}
	return rt;
}


enum StatementType
{
	AssignNormalizedTerm,
	AssignPatternResult
};

template <class AnalyzerInterface>
static void parseAnalyzerPatternMatchProgramDef(
		AnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& patternModuleName,
		const std::string& patternTypeName,
		char const*& src,
		ErrorBufferInterface* errorhnd)
{
	std::vector<std::string> selectexprlist;
	if (isOpenCurlyBracket(*src))
	{
		do
		{
			(void)parse_OPERATOR( src);
			selectexprlist.push_back( parseSelectorExpression( src));
		} while (isComma(*src));
		if (!isCloseCurlyBracket(*src))
		{
			throw strus::runtime_error(_TXT("expected close curly bracket '}' at end of pattern lexer selection expressions"));
		}
		(void)parse_OPERATOR( src);
	}
	std::vector<std::pair<std::string,std::string> > ptsources;
	for(;;)
	{
		std::string filename = parseSelectorExpression( src);
		std::string filepath = textproc->getResourcePath( filename);
		if (filepath.empty() && errorhnd->hasError())
		{
			throw strus::runtime_error(_TXT( "failed to evaluate pattern match file path '%s': %s"), filename.c_str(), errorhnd->fetchError());
		}
		ptsources.push_back( std::pair<std::string,std::string>( filepath, std::string()));
		unsigned int ec = readFile( filepath, ptsources.back().second);
		if (ec)
		{
			throw strus::runtime_error(_TXT( "failed to read pattern match file '%s': %s"), filepath.c_str(), ::strerror(ec));
		}
		if (!isComma(*src)) break;
		(void)parse_OPERATOR( src);
	}
	if (selectexprlist.empty())
	{
		const PatternMatcherInterface* matcher = textproc->getPatternMatcher( patternModuleName);
		const PatternTermFeederInterface* feeder = textproc->getPatternTermFeeder();
		PatternMatcherProgram result;
		if (!feeder || !matcher || !loadPatternMatcherProgramForAnalyzerOutput(
				result, feeder, matcher, ptsources, errorhnd))
		{
			throw strus::runtime_error( _TXT("failed to create post proc pattern matching: %s"), errorhnd->fetchError());
		}
		std::auto_ptr<PatternTermFeederInstanceInterface> feederctx( result.fetchTermFeeder());
		std::auto_ptr<PatternMatcherInstanceInterface> matcherctx( result.fetchMatcher());
		if (!feederctx.get() || !matcherctx.get())
		{
			throw strus::runtime_error( _TXT("failed to create post proc pattern matching: %s"), errorhnd->fetchError());
		}
		analyzer.definePatternMatcherPostProc( patternTypeName, matcherctx.get(), feederctx.get());
		matcherctx.release();
		feederctx.release();
		if (errorhnd->hasError())
		{
			throw strus::runtime_error( _TXT("failed to create post proc pattern matching: %s"), errorhnd->fetchError());
		}
	}
	else
	{
		const PatternLexerInterface* lexer = textproc->getPatternLexer( patternModuleName);
		const PatternMatcherInterface* matcher = textproc->getPatternMatcher( patternModuleName);
		PatternMatcherProgram result;
		if (!lexer || !matcher || !loadPatternMatcherProgram( result, lexer, matcher, ptsources, errorhnd))
		{
			throw strus::runtime_error( _TXT("failed to create pre proc pattern matching: %s"), errorhnd->fetchError());
		}
		std::auto_ptr<PatternLexerInstanceInterface> lexerctx( result.fetchLexer());
		std::auto_ptr<PatternMatcherInstanceInterface> matcherctx( result.fetchMatcher());
		if (!lexerctx.get() || !matcherctx.get())
		{
			throw strus::runtime_error( _TXT("failed to create pre proc pattern matching: %s"), errorhnd->fetchError());
		}
		analyzer.definePatternMatcherPreProc( patternTypeName, matcherctx.get(), lexerctx.get(), selectexprlist);
		matcherctx.release();
		lexerctx.release();
		if (errorhnd->hasError())
		{
			throw strus::runtime_error( _TXT("failed to create pre proc pattern matching: %s"), errorhnd->fetchError());
		}
	}
}

static void expandIncludes(
		const std::string& source,
		const TextProcessorInterface* textproc,
		std::set<std::string>& visited,
		std::vector<std::pair<std::string,std::string> >& contents,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	while (isSpace( *src)) ++src;

	while (*src == '#' && std::memcmp( src, "#include", 8) == 0 && isSpace(src[8]))
	{
		src+= 8;
		while (isSpace( *src)) ++src;

		if (!isStringQuote(*src)) throw strus::runtime_error(_TXT("string expected as include file path"));
		std::string filename = parse_STRING_noskip( src);

		if (filename.empty()) throw strus::runtime_error(_TXT("include file name is empty"));
		std::string filepath = textproc->getResourcePath( filename);
		if (filepath.empty()) throw strus::runtime_error(_TXT("failed to find include file path '%s': %s"), filename.c_str(), errorhnd->fetchError());

		if (visited.find( filepath) == visited.end())
		{
			std::string include_source;
			unsigned int ec = strus::readFile( filepath, include_source);
			if (ec) throw strus::runtime_error(_TXT("failed to load include file '%s': %s"), filepath.c_str(), ::strerror( ec));

			visited.insert( filepath);
			expandIncludes( include_source, textproc, visited, contents, errorhnd);

			contents.push_back( std::pair<std::string,std::string>( filename, include_source));
		}
		while (isSpace( *src)) ++src;
	}
}

DLL_PUBLIC bool strus::loadDocumentAnalyzerProgram(
		DocumentAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source,
		bool allowIncludes,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	try
	{
		if (allowIncludes)
		{
			std::set<std::string> visited;
			std::vector<std::pair<std::string,std::string> > include_contents;

			expandIncludes( source, textproc, visited, include_contents, errorhnd);
			std::vector<std::pair<std::string,std::string> >::const_iterator
				ci = include_contents.begin(), ce = include_contents.end();
			for (; ci != ce; ++ci)
			{
				if (!strus::loadDocumentAnalyzerProgram(
						analyzer, textproc, ci->second,
						false/*!allowIncludes*/, errorhnd))
				{
					throw strus::runtime_error(_TXT("failed to load include file '%s': %s"), ci->first.c_str(), errorhnd->fetchError());
				}
			}
		}
		FeatureClass featclass = FeatSearchIndexTerm;
		std::string featclassid;

		skipSpaces(src);
		while (*src)
		{
			if (isOpenSquareBracket( *src))
			{
				featclass = parseFeatureClassDef( src, featclassid);
			}
			if (!isAlnum(*src))
			{
				throw strus::runtime_error( _TXT("feature type name (identifier) expected at start of a feature declaration"));
			}
			std::string identifier = parse_IDENTIFIER( src);
			StatementType statementType = AssignNormalizedTerm;

			if (isAssign( *src))
			{
				(void)parse_OPERATOR( src);
				statementType = AssignNormalizedTerm;
			}
			else if (isLeftArrow( src))
			{
				src += 2; skipSpaces( src); //....parse_OPERATOR
				statementType = AssignPatternResult;
			}
			else
			{
				throw strus::runtime_error( _TXT("assignment operator '=' or '<-' expected after set identifier in a feature declaration"));
			}
			if (featclass == FeatSubDocument)
			{
				if (statementType == AssignPatternResult) throw strus::runtime_error(_TXT("pattern result assignment '<-' not allowed in sub document section"));

				std::string xpathexpr( parseSelectorExpression( src));
				analyzer.defineSubDocument( identifier, xpathexpr);
			}
			else if (featclass == FeatAggregator)
			{
				if (statementType == AssignPatternResult) throw strus::runtime_error(_TXT("pattern result assignment '<-' not allowed in aggregator section"));

				std::auto_ptr<AggregatorFunctionInstanceInterface> statfunc;
				FunctionConfig cfg = parseAggregatorFunctionConfig( src);

				const AggregatorFunctionInterface* sf = textproc->getAggregator( cfg.name());
				if (!sf) throw strus::runtime_error(_TXT( "unknown aggregator function '%s'"), cfg.name().c_str());
				
				statfunc.reset( sf->createInstance( cfg.args()));
				if (!statfunc.get()) throw strus::runtime_error(_TXT( "failed to create instance of aggregator function '%s'"), cfg.name().c_str());

				analyzer.defineAggregatedMetaData( identifier, statfunc.get());
				statfunc.release();
			}
			else if (featclass == FeatPatternMatch)
			{
				if (statementType == AssignPatternResult) throw strus::runtime_error(_TXT("pattern result assignment '<-' not allowed in pattern match section"));
				parseAnalyzerPatternMatchProgramDef( analyzer, textproc, featclassid, identifier, src, errorhnd);
			}
			else switch (statementType)
			{
				case AssignPatternResult:
					parseDocumentPatternFeatureDef( analyzer, textproc, identifier, src, featclass);
					break;
				case AssignNormalizedTerm:
					parseDocumentFeatureDef( analyzer, textproc, identifier, src, featclass);
					break;
			}
			if (!isSemiColon(*src))
			{
				throw strus::runtime_error( _TXT("semicolon ';' expected at end of feature declaration"));
			}
			(void)parse_OPERATOR(src);
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("out of memory parsing document analyzer program %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("error in document analyzer program %s: %s"), pos.c_str(), e.what());
		return false;
	}
}

DLL_PUBLIC bool strus::loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		QueryDescriptors& qdescr,
		const TextProcessorInterface* textproc,
		const std::string& source,
		bool allowIncludes,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	try
	{
		if (allowIncludes)
		{
			std::set<std::string> visited;
			std::vector<std::pair<std::string,std::string> > include_contents;

			expandIncludes( source, textproc, visited, include_contents, errorhnd);
			std::vector<std::pair<std::string,std::string> >::const_iterator
				ci = include_contents.begin(), ce = include_contents.end();
			for (; ci != ce; ++ci)
			{
				if (!strus::loadQueryAnalyzerProgram(
						analyzer, qdescr, textproc, ci->second,
						false/*!allowIncludes*/, errorhnd))
				{
					throw strus::runtime_error(_TXT("failed to load include file '%s': %s"), ci->first.c_str(), errorhnd->fetchError());
				}
			}
		}
		FeatureClass featclass = FeatSearchIndexTerm;
		std::string featclassid;

		skipSpaces(src);
		while (*src)
		{
			if (isOpenSquareBracket( *src))
			{
				featclass = parseFeatureClassDef( src, featclassid);
			}
			if (!isAlnum(*src))
			{
				throw strus::runtime_error( _TXT("feature type name (identifier) expected at start of a feature declaration"));
			}
			std::string identifier = parse_IDENTIFIER( src);
			StatementType statementType = AssignNormalizedTerm;

			if (isAssign( *src))
			{
				(void)parse_OPERATOR( src);
				statementType = AssignNormalizedTerm;
			}
			else if (isLeftArrow( src))
			{
				src += 2; skipSpaces( src); //....parse_OPERATOR
				statementType = AssignPatternResult;
			}
			else
			{
				throw strus::runtime_error( _TXT("assignment operator '=' or '<-' expected after set identifier in a feature declaration"));
			}
			if (featclass == FeatSubDocument)
			{
				throw strus::runtime_error(_TXT("sub document sections not implemented in query"));
			}
			else if (featclass == FeatAggregator)
			{
				throw strus::runtime_error(_TXT("aggregator sections not implemented in query"));
			}
			else if (featclass == FeatPatternMatch)
			{
				if (statementType == AssignPatternResult) throw strus::runtime_error(_TXT("pattern result assignment '<-' not allowed in pattern match section"));
				parseAnalyzerPatternMatchProgramDef( analyzer, textproc, featclassid, identifier, src, errorhnd);
			}
			else switch (statementType)
			{
				case AssignPatternResult:
					parseQueryPatternFeatureDef( analyzer, textproc, identifier, src, featclass);
					break;
				case AssignNormalizedTerm:
					if (qdescr.defaultFieldType.empty())
					{
						qdescr.defaultFieldType = identifier;
					}
					parseQueryFeatureDef( analyzer, qdescr, textproc, identifier, src, featclass);
					break;
			}
			if (!isSemiColon(*src))
			{
				throw strus::runtime_error( _TXT("semicolon ';' expected at end of feature declaration"));
			}
			(void)parse_OPERATOR(src);
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("out of memory parsing query analyzer program %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("error in query analyzer program %s: %s"), pos.c_str(), e.what());
		return false;
	}
}


DLL_PUBLIC bool strus::isAnalyzerMapSource(
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		char const* src = source.c_str();
		skipSpaces(src);
		if (isAlpha(*src))
		{
			std::string id = parse_IDENTIFIER( src);
			if (isEqual( id, "SCHEME") || isEqual( id, "SEGMENTER") || isEqual( id, "PROGRAM")) return true;
		}
		return false;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report(_TXT("out of memory in check for analyzer map source"));
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report(_TXT("error in check for analyzer map source: %s"), e.what());
		return false;
	}
}

static std::string parseAnalyzerMapValue( char const*& itr)
{
	std::string val;
	if (isStringQuote( *itr))
	{
		val = parse_STRING( itr);
	}
	else
	{
		for (;*itr && !isSpace(*itr) && !isColon(*itr); ++itr)
		{
			val.push_back( *itr);
		}
	}
	return val;
}

DLL_PUBLIC bool strus::loadAnalyzerMap(
		std::vector<AnalyzerMapElement>& mapdef,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	enum Mask {MSK_SCHEME=0x01, MSK_PROGRAM=0x02, MSK_SEGMENTER=0x04};
	AnalyzerMapElement elem;
	int mask = 0;
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		while (*src)
		{
			if (isSemiColon(*src))
			{
				(void)parse_OPERATOR( src);
				if ((mask & MSK_PROGRAM) == 0)
				{
					mapdef.push_back( elem);
					elem.clear();
					mask = 0;
				}
				else if (!mask)
				{
					throw strus::runtime_error( _TXT("empty declaration"));
				}
				else
				{
					throw strus::runtime_error( _TXT("PROGRAM missing in declaration"));
				}
			}
			if (isAlpha(*src))
			{
				std::string id = parse_IDENTIFIER( src);
				if (isEqual( id, "SCHEME"))
				{
					if (mask & MSK_SCHEME) throw strus::runtime_error( _TXT("duplicate definition of %s"), id.c_str());
					mask |= MSK_SCHEME;
					elem.scheme = parseAnalyzerMapValue( src);
				}
				else if (isEqual( id, "PROGRAM"))
				{
					if (mask & MSK_PROGRAM) throw strus::runtime_error( _TXT("duplicate definition of %s"), id.c_str());
					mask |= MSK_PROGRAM;
					elem.prgFilename = parseAnalyzerMapValue( src);
				}
				else if (isEqual( id, "SEGMENTER"))
				{
					if (mask & MSK_SEGMENTER) throw strus::runtime_error( _TXT("duplicate definition of %s"), id.c_str());
					mask |= MSK_SEGMENTER;
					elem.segmenter = parseAnalyzerMapValue( src);
				}
				else
				{
					throw strus::runtime_error( _TXT( "unknown identifier '%s'"), id.c_str());
				}
			}
		}
		if (mask)
		{
			throw strus::runtime_error( _TXT("unterminated definition, missing semicolon at end of source"));
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("out of memory parsing query document class to analyzer map program %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("error in query document class to analyzer map program %s: %s"), pos.c_str(), e.what());
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
			throw strus::runtime_error( _TXT("query analyze phrase type (identifier) expected after colon ':' in query"));
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

static void parseQueryExpression(
		QueryStruct& queryStruct,
		const QueryProcessorInterface* queryproc,
		const QueryDescriptors& qdescr,
		char const*& src)
{
	if (isAlpha( *src))
	{
		char const* src_bk = src;
		std::string functionName = parse_IDENTIFIER(src);
		if (isOpenOvalBracket(*src))
		{
			(void)parse_OPERATOR( src);
			std::size_t argc = 0;

			if (!isCloseOvalBracket( *src)) while (*src)
			{
				argc++;
				parseQueryExpression( queryStruct, queryproc, qdescr, src);
				if (isComma( *src))
				{
					(void)parse_OPERATOR( src);
					continue;
				}
				break;
			}
			int range = 0;
			unsigned int cardinality = 0;
			while (isOr( *src) || isExp( *src))
			{
				if (isOr( *src))
				{
					if (range != 0) throw strus::runtime_error( _TXT("range specified twice"));
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
					if (range == 0) throw strus::runtime_error( _TXT("range should be a non null number"));
				}
				else
				{
					if (cardinality != 0) throw strus::runtime_error( _TXT("cardinality specified twice"));
					(void)parse_OPERATOR( src);
					cardinality = parse_UNSIGNED1( src);
				}
			}
			if (!isCloseOvalBracket( *src))
			{
				throw strus::runtime_error( _TXT("comma ',' as query argument separator or colon ':' as range specifier or close oval bracket ')' as end of a query expression expected"));
			}
			(void)parse_OPERATOR( src);
			const PostingJoinOperatorInterface*
				function = queryproc->getPostingJoinOperator( functionName);
			if (!function)
			{
				throw strus::runtime_error( _TXT("posting join operator not defined: '%s'"),
								functionName.c_str());
			}
			std::string variableName = parseVariableRef( src);
			queryStruct.defineExpression( function, argc, range, cardinality);
			if (!variableName.empty())
			{
				queryStruct.defineVariable( variableName);
			}
			return;
		}
		else if (isCompareOperator(src))
		{
			MetaDataRestrictionInterface::CompareOperator opr = parse_CompareOperator( src);
			std::string value = parseQueryTerm( src);
			queryStruct.defineMetaDataRestriction( functionName, opr, functionName/*field type == metadata name in condition*/, value);
			return;
		}
		else
		{
			src = src_bk;
		}
	}
	bool isSelection = true;
	if (isExclamation( *src))
	{
		(void)parse_OPERATOR( src);
		isSelection = false;
	}
	if (isTextChar( *src) || isStringQuote( *src))
	{
		std::string queryField = parseQueryTerm( src);
		std::string fieldType = parseQueryFieldType( src);
		if (fieldType.empty())
		{
			fieldType = qdescr.weightingFeatureSet;
		}
		queryStruct.defineField( fieldType, queryField, isSelection);

		std::string variableName = parseVariableRef( src);
		if (!variableName.empty())
		{
			queryStruct.defineVariable( variableName);
		}
	}
	else if (isColon( *src))
	{
		std::string fieldType = parseQueryFieldType( src);
		std::string variableName = parseVariableRef( src);
		queryStruct.defineField( fieldType, std::string(), false/*isSelection*/);
		if (!variableName.empty())
		{
			queryStruct.defineVariable( variableName);
		}
	}
	else
	{
		throw strus::runtime_error( _TXT("syntax error in query, query expression or term expected"));
	}
}


DLL_PUBLIC bool strus::loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* queryproc,
		const std::string& source,
		const QueryDescriptors& qdescr,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	try
	{
		QueryStruct queryStruct( analyzer);
		bool haveSelectionFeatureDefined = false;
		skipSpaces(src);
		while (*src)
		{
			// Parse query section:
			float featureWeight = 1.0;
			std::string featureSet;
			const char* src_bk = src;

			if (isAlnum( *src))
			{
				featureSet = parse_IDENTIFIER( src);
				if (!isColon(*src))
				{
					src = src_bk;	//... step back
					featureSet = qdescr.weightingFeatureSet;
				}
				if (utils::caseInsensitiveEquals( featureSet, qdescr.selectionFeatureSet))
				{
					haveSelectionFeatureDefined = true;
				}
			}
			parseQueryExpression( queryStruct, queryproc, qdescr, src);
			if (isAsterisk(*src))
			{
				(void)parse_OPERATOR(src);
				if (isDigit(*src))
				{
					featureWeight = parse_FLOAT( src);
				}
				else
				{
					featureWeight = 1.0;
				}
			}
			queryStruct.defineFeature( featureSet, featureWeight);
		}
		if (!haveSelectionFeatureDefined)
		{
			queryStruct.defineSelectionFeatures( queryproc, qdescr);
		}
		queryStruct.translate( query, queryproc, errorhnd);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report( _TXT("out of memory parsing query source %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report( _TXT("error in query source %s: %s"), pos.c_str(), e.what());
		return false;
	}
}


DLL_PUBLIC bool strus::loadPhraseAnalyzer(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& normalizersrc,
		const std::string& tokenizersrc,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		std::auto_ptr<TokenizerFunctionInstanceInterface> tokenizer;
		std::vector<Reference<NormalizerFunctionInstanceInterface> > normalizer_ref;
		std::vector<NormalizerFunctionInstanceInterface*> normalizer;
	
		char const* nsrc = normalizersrc.c_str();
		std::vector<FunctionConfig> normalizercfg = parseNormalizerConfig( nsrc);
		if ((std::size_t)(nsrc - normalizersrc.c_str()) < normalizersrc.size())
		{
			throw strus::runtime_error( _TXT("unexpected token after end of normalizer definition: '%s'"), nsrc);
		}
		std::vector<FunctionConfig>::const_iterator ni = normalizercfg.begin(), ne = normalizercfg.end();
		for (; ni != ne; ++ni)
		{
			const NormalizerFunctionInterface* nm = textproc->getNormalizer( ni->name());
			if (!nm) throw strus::runtime_error(_TXT( "unknown normalizer function '%s'"), ni->name().c_str());
	
			Reference<NormalizerFunctionInstanceInterface> nmi( nm->createInstance( ni->args(), textproc));
			if (!nmi.get()) throw strus::runtime_error(_TXT( "failed to create instance of normalizer function '%s'"), ni->name().c_str());
	
			normalizer_ref.push_back( nmi);
			normalizer.push_back( nmi.get());
		}
		char const* tsrc = tokenizersrc.c_str();
		FunctionConfig tokenizercfg = parseTokenizerConfig( tsrc);
		if ((std::size_t)(tsrc - tokenizersrc.c_str()) < tokenizersrc.size())
		{
			throw strus::runtime_error( _TXT("unexpected token after end of tokenizer definition: '%s'"), nsrc);
		}
		const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
		if (!tk) throw strus::runtime_error(_TXT( "tokenizer function '%s' not found"), tokenizercfg.name().c_str());
	
		tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));
		if (!tokenizer.get()) throw strus::runtime_error(_TXT( "failed to create instance of tokenizer function '%s'"), tokenizercfg.name().c_str());
	
		analyzer.addSearchIndexElement( "", "", tokenizer.get(), normalizer);

		std::vector<Reference<NormalizerFunctionInstanceInterface> >::iterator
			ri = normalizer_ref.begin(), re = normalizer_ref.end();
		for (; ri != re; ++ri)
		{
			(void)ri->release();
		}
		tokenizer.release();
		return true;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report(_TXT("out of memory loading query analyzer phrase type"));
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report(_TXT("error in query analyzer phrase type: %s"), e.what());
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
		errorhnd->report( _TXT("out of memory scanning next program"));
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("error scanning next program: %s"), e.what());
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
		for (; isSpace(*itr); ++itr)
		{
			docid.push_back( *itr);
		}
		return storage.documentNumber( docid);
	}
}

static void storeMetaDataValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const NumericVariant& val)
{
	std::auto_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	if (!update.get()) throw strus::runtime_error( _TXT("failed to create document update structure"));

	update->setMetaData( name, val);
	update->done();
}

static void storeAttributeValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const std::string& val)
{
	std::auto_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	if (!update.get()) throw strus::runtime_error( _TXT("failed to create document update structure"));
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
	std::auto_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	if (!update.get()) throw strus::runtime_error( _TXT("failed to create document update structure"));
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
			throw strus::runtime_error( _TXT("unexpected token in user rigths specification"));
		}
	}
}


enum StorageValueType
{
	StorageValueMetaData,
	StorageValueAttribute,
	StorageUserRights
};

static unsigned int loadStorageValues(
		StorageClientInterface& storage,
		const std::string& elementName,
		const std::string& file,
		StorageValueType valueType,
		unsigned int commitsize)
{
	InputStream stream( file);
	if (stream.error()) throw strus::runtime_error(_TXT("failed to open storage value file '%s': %s"), file.c_str(), ::strerror(stream.error()));
	unsigned int rt = 0;
	std::auto_ptr<StorageTransactionInterface>
		transaction( storage.createTransaction());
	if (!transaction.get()) throw strus::runtime_error( _TXT("failed to create storage transaction"));
	std::size_t linecnt = 1;
	unsigned int commitcnt = 0;
	try
	{
		char line[ 2048];
		for (; stream.readLine( line, sizeof(line)); ++linecnt)
		{
			char const* itr = line;
			Index docno = parseDocno( storage, itr);

			if (!docno) continue;
			switch (valueType)
			{
				case StorageValueMetaData:
				{
					NumericVariant val( parseNumericValue( itr));
					storeMetaDataValue( *transaction, docno, elementName, val);
					rt += 1;
					break;
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
					rt += 1;
					break;
				}
				case StorageUserRights:
				{
					std::string val( itr);
					itr = std::strchr( itr, '\0');
					storeUserRights( *transaction, docno, val);
					rt += 1;
					break;
				}
			}
			if (*itr)
			{
				throw strus::runtime_error( _TXT("extra characters after value assignment"));
			}
			if (++commitcnt == commitsize)
			{
				if (!transaction->commit())
				{
					throw strus::runtime_error(_TXT("transaction commit failed"));
				}
				commitcnt = 0;
				transaction.reset( storage.createTransaction());
				if (!transaction.get()) throw strus::runtime_error( _TXT("failed to recreate storage transaction after commit"));
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
				throw strus::runtime_error(_TXT("transaction commit failed"));
			}
			commitcnt = 0;
			transaction.reset( storage.createTransaction());
			if (!transaction.get()) throw strus::runtime_error( _TXT("failed to recreate storage transaction after commit"));
		}
		return rt;
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("error on line %u: %s"), linecnt, err.what());
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentMetaDataAssignments(
		StorageClientInterface& storage,
		const std::string& metadataName,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		return loadStorageValues( storage, metadataName, file, StorageValueMetaData, commitsize);
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory loading meta data assignments"));
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("error loading meta data assignments: %s"), e.what());
		return 0;
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentAttributeAssignments(
		StorageClientInterface& storage,
		const std::string& attributeName,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		return loadStorageValues( storage, attributeName, file, StorageValueAttribute, commitsize);
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory loading attribute assignments"));
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("error loading attribute assignments: %s"), e.what());
		return 0;
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentUserRightsAssignments(
		StorageClientInterface& storage,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		return loadStorageValues( storage, std::string(), file, StorageUserRights, commitsize);
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory loading user right assignments"));
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("error loading user right assignments: %s"), e.what());
		return 0;
	}
}


DLL_PUBLIC bool strus::parseDocumentClass(
		analyzer::DocumentClass& result,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	try
	{
		std::string mimeType;
		std::string encoding;

		char const* si = source.c_str();
		char const* start = si;
		skipSpaces( si);
		if (isAlpha(*si))
		{
			std::string value = parse_PATH( si);
			if (!*si)
			{
				mimeType = value;
				encoding = "UTF-8";
			}
			else
			{
				si = start;
			}
		}
		if (!mimeType.empty())
		while (isAlpha(*si))
		{
			std::string id( parse_IDENTIFIER( si));
			std::string value;
			if (!isAssign(*si))
			{
				throw strus::runtime_error( _TXT("expected assignment operator '=' after identifier"));
			}
			(void)parse_OPERATOR( si);
			if (isStringQuote(*si))
			{
				value = parse_STRING( si);
			}
			else if (isAlpha(*si))
			{
				value = parse_PATH( si);
			}
			else
			{
				throw strus::runtime_error( _TXT("expected string or content type or encoding as value"));
			}
			if (isEqual( id, "content"))
			{
				mimeType = value;
			}
			else if (isEqual( id, "charset") || isEqual( id, "encoding"))
			{
				encoding = value;
			}
			else
			{
				throw strus::runtime_error( _TXT("unknown identifier in document class declaration: %s"), id.c_str());
			}
			if (isSemiColon(*si))
			{
				parse_OPERATOR(si);
			}
		}
		if (isEqual( mimeType,"xml") || isEqual( mimeType,"text/xml"))
		{
			mimeType = "application/xml";
		}
		else if (isEqual( mimeType,"json"))
		{
			mimeType = "application/json";
		}
		else if (isEqual( mimeType,"tsv"))
		{
			mimeType = "text/tab-separated-values";
		}
		result = analyzer::DocumentClass( mimeType, encoding);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory parsing document class"));
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("error parsing document class: %s"), e.what());
		return false;
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
		VectorStorageBuilderInterface* vsmbuilder,
		const std::string& vectorfile,
		ErrorBufferInterface* errorhnd)
{
	unsigned int linecnt = 0;
	try
	{
		InputStream infile( vectorfile);
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to open word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
		unsigned int collsize;
		unsigned int vecsize;
	
		// Read first text line, that contains two numbers, the collection size and the vector size:
		char firstline[ 256];
		std::size_t size = infile.readAhead( firstline, sizeof(firstline)-1);
		firstline[ size] = '\0';
		char const* si = firstline;
		const char* se = std::strchr( si, '\n');
		if (!se) throw strus::runtime_error(_TXT("failed to parse header line"));
		skipSpaces( si);
		if (!is_UNSIGNED(si)) throw strus::runtime_error("expected collection size as first element of the header line");
		collsize = parse_UNSIGNED1( si);
		skipSpaces( si);
		if (!is_UNSIGNED(si)) throw strus::runtime_error("expected vector size as second element of the header line");
		vecsize = parse_UNSIGNED1( si);
		if (*(si-1) != '\n')
		{
			skipToEoln( si);
			++si;
		}
		infile.read( firstline, si - firstline);

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
	
		// Parse vector by vector and add them to the builder till EOF:
		size = infile.readAhead( linebuf, linebufsize);
		while (size)
		{
			++linecnt;
			char const* si = linebuf;
			const char* se = linebuf + size;
			for (; si < se && (unsigned char)*si > 32; ++si){}
			const char* term = linebuf;
			std::size_t termsize = si - linebuf;
			++si;
			if (si+vecsize*sizeof(float) > se)
			{
				throw strus::runtime_error( _TXT("wrong file format"));
			}
#ifdef STRUS_LOWLEVEL_DEBUG
			for (std::size_t ti=0; ti<termsize; ++ti) printf("%c",term[ti]);
#endif
			std::vector<double> vec;
			vec.reserve( vecsize);
			unsigned int ii = 0;
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
			vsmbuilder->addFeature( std::string(term, termsize), vec);
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
			size = infile.readAhead( linebuf, linebufsize);
		}
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to read from word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
		if (collsize != linecnt)
		{
			throw strus::runtime_error(_TXT("collection size does not match"));
		}
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("in word2vec binary file in record %u: %s"), linecnt, err.what());
	}
}

static void loadVectorStorageVectors_word2vecText( 
		VectorStorageBuilderInterface* vsmbuilder,
		const std::string& vectorfile,
		ErrorBufferInterface* errorhnd)
{
	unsigned int linecnt = 0;
	try
	{
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
			if (se - si == LineBufSize-1) throw strus::runtime_error(_TXT("input line too long"));
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
				throw strus::runtime_error(_TXT("unexpected end of file"));
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
				throw strus::runtime_error(_TXT("expected vector of double precision floating point numbers after term definition"));
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
			vsmbuilder->addFeature( std::string(term, termsize), vec);
			if (errorhnd->hasError())
			{
				throw strus::runtime_error(_TXT("add vector failed: %s"), errorhnd->fetchError());
			}
		}
		if (infile.error())
		{
			throw strus::runtime_error(_TXT("failed to read from word2vec file '%s': %s"), vectorfile.c_str(), ::strerror(infile.error()));
		}
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("in word2vec text file on line %u: %s"), linecnt, err.what());
	}
}

DLL_PUBLIC bool strus::loadVectorStorageVectors( 
		VectorStorageBuilderInterface* vsmbuilder,
		const std::string& vectorfile,
		ErrorBufferInterface* errorhnd)
{
	char const* filetype = 0;
	try
	{
		if (isTextFile( vectorfile))
		{
			filetype = "word2vec text file";
			loadVectorStorageVectors_word2vecText( vsmbuilder, vectorfile, errorhnd);
		}
		else
		{
			filetype = "word2vec binary file";
			loadVectorStorageVectors_word2vecBin( vsmbuilder, vectorfile, errorhnd);
		}
		return vsmbuilder->done();
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory loading feature vectors from file (file format: %s)"), vectorfile.c_str(), filetype);
		return false;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("error loading feature vectors from file %s (file format: %s): %s"), vectorfile.c_str(), filetype, e.what());
		return false;
	}
}


DLL_PUBLIC PatternMatcherProgram::~PatternMatcherProgram()
{
	if (m_lexer) delete m_lexer;
	if (m_termFeeder) delete m_termFeeder;
	if (m_matcher) delete m_matcher;
}

DLL_PUBLIC void PatternMatcherProgram::init(
		PatternLexerInstanceInterface* lexer_,
		PatternTermFeederInstanceInterface* termFeeder_,
		PatternMatcherInstanceInterface* matcher_,
		const std::vector<std::string>& regexidmap_,
		const std::vector<uint32_t>& symbolRegexIdList_)
{
	m_lexer = lexer_;
	m_termFeeder = termFeeder_;
	m_matcher = matcher_;
	m_regexidmap = regexidmap_;
	m_symbolRegexIdList = symbolRegexIdList_;
}

DLL_PUBLIC PatternLexerInstanceInterface* PatternMatcherProgram::fetchLexer()
{
	PatternLexerInstanceInterface* rt = m_lexer;
	m_lexer = 0;
	return rt;
}

DLL_PUBLIC PatternTermFeederInstanceInterface* PatternMatcherProgram::fetchTermFeeder()
{
	PatternTermFeederInstanceInterface* rt = m_termFeeder;
	m_termFeeder = 0;
	return rt;
}

DLL_PUBLIC PatternMatcherInstanceInterface* PatternMatcherProgram::fetchMatcher()
{
	PatternMatcherInstanceInterface* rt = m_matcher;
	m_matcher = 0;
	return rt;
}

DLL_PUBLIC const char* PatternMatcherProgram::tokenName( unsigned int id) const
{
	if (id >= MaxRegularExpressionNameId)
	{
		id = m_symbolRegexIdList[ id - MaxRegularExpressionNameId -1];
	}
	return m_regexidmap[ id-1].c_str();
}


DLL_PUBLIC bool strus::loadPatternMatcherProgram(
		PatternMatcherProgram& result,
		const PatternLexerInterface* lexer,
		const PatternMatcherInterface* matcher,
		const std::vector<std::pair<std::string,std::string> >& sources,
		ErrorBufferInterface* errorhnd)
{
	const char* prgname = "";
	try
	{
		if (errorhnd->hasError()) throw std::runtime_error(_TXT("called load patter matcher program with error"));
		PatternMatcherProgramParser program( lexer, matcher, errorhnd);

		std::vector<std::pair<std::string,std::string> >::const_iterator
			si = sources.begin(), se = sources.end();
		for (; si != se; ++si)
		{
			prgname = si->first.c_str();
			if (!program.load( si->second)) throw std::runtime_error( errorhnd->fetchError());
		}
		if (!program.compile())
		{
			errorhnd->explain(_TXT("failed to compile pattern match program"));
			return false;
		}
		program.fetchResult( result);
		return true;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("failed to load pattern match program '%s': %s"), prgname, e.what());
		return false;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory loading pattern match program '%s'"), prgname);
		return false;
	}
}

DLL_PUBLIC bool strus::loadPatternMatcherProgramForAnalyzerOutput(
		PatternMatcherProgram& result,
		const PatternTermFeederInterface* termFeeder,
		const PatternMatcherInterface* matcher,
		const std::vector<std::pair<std::string,std::string> >& sources,
		ErrorBufferInterface* errorhnd)
{
	const char* prgname = "";
	try
	{
		if (errorhnd->hasError()) throw std::runtime_error(_TXT("called load patter matcher program with error"));
		PatternMatcherProgramParser program( termFeeder, matcher, errorhnd);

		std::vector<std::pair<std::string,std::string> >::const_iterator
			si = sources.begin(), se = sources.end();
		for (; si != se; ++si)
		{
			prgname = si->first.c_str();
			if (!program.load( si->second)) throw std::runtime_error( errorhnd->fetchError());
		}
		if (!program.compile())
		{
			errorhnd->explain(_TXT("failed to compile pattern match program for analyzer output"));
			return false;
		}
		program.fetchResult( result);
		return true;
	}
	catch (const std::runtime_error& e)
	{
		errorhnd->report( _TXT("failed to load pattern match program (for analyzer output) '%s': %s"), prgname, e.what());
		return false;
	}
	catch (const std::bad_alloc&)
	{
		errorhnd->report( _TXT("out of memory loading pattern match program (for analyzer output) '%s'"), prgname);
		return false;
	}
}

