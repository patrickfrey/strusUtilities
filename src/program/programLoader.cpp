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
#include "strus/constants.hpp"
#include "strus/arithmeticVariant.hpp"
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
#include "strus/queryProcessorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/storageDocumentUpdateInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/term.hpp"
#include "strus/reference.hpp"
#include "strus/private/snprintf.h"
#include "private/inputStream.hpp"
#include "private/utils.hpp"
#include "private/dll_tags.hpp"
#include "private/internationalization.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>

using namespace strus;
using namespace strus::parser;

class ErrorPosition
{
public:
	ErrorPosition( const char* base, const char* itr)
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
		strus_snprintf( m_buf, sizeof(m_buf), "at line %u column %u", line, col);
	}

	const char* c_str() const
	{
		return m_buf;
	}
private:
	char m_buf[ 128];
};


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
		char const*& src)
{
	if (isAlpha(*src))
	{
		std::string termset = utils::tolower( parse_IDENTIFIER( src));
		if (!isStringQuote( *src) && !isTextChar( *src))
		{
			throw strus::runtime_error(_TXT( "term value (string,identifier,number) after the feature group identifier"));
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
			while (*src == '0') ++src;
			if (*src >= '1' && *src <= '9')
			{
				return ArithmeticVariant( parse_UNSIGNED( src));
			}
			else
			{
				return ArithmeticVariant( 0);
			}
		}
	}
	else
	{
		return ArithmeticVariant( parse_FLOAT( src));
	}
}

static void parseWeightingConfig(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* queryproc,
		char const*& src)
{
	float weight = 1.0;
	if (is_FLOAT( src))
	{
		weight = parse_FLOAT( src);
		if (!isAsterisk(*src))
		{
			throw strus::runtime_error(_TXT( "multiplication operator '*' expected after EVAL followed by a floating point number (weight)"));
		}
		(void)parse_OPERATOR(src);
	}
	if (!isAlpha( *src))
	{
		throw strus::runtime_error(_TXT( "weighting function identifier expected"));
	}
	std::string functionName = parse_IDENTIFIER( src);

	const WeightingFunctionInterface* wf = queryproc->getWeightingFunction( functionName);
	if (!wf) throw strus::runtime_error(_TXT( "weighting function '%s' not defined"), functionName.c_str());

	std::auto_ptr<WeightingFunctionInstanceInterface> function( wf->createInstance());
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
		if (isDigit(*src) || isMinus(*src))
		{
			if (isFeatureParam)
			{
				throw strus::runtime_error(_TXT( "feature parameter argument must be an identifier or string and not a number"));
			}
			ArithmeticVariant parameterValue = parseNumericValue( src);
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
		throw strus::runtime_error(_TXT( "close oval bracket ')' expected at end of weighting function parameter list"));
	}
	(void)parse_OPERATOR(src);
	qeval.addWeightingFunction( functionName, function.get(), featureParameters, weight); 
	(void)function.release();
}


static void parseSummarizerConfig(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* queryproc,
		char const*& src)
{
	std::string functionName;
	std::string resultAttribute;
	typedef QueryEvalInterface::FeatureParameter FeatureParameter;
	std::vector<FeatureParameter> featureParameters;

	if (!isAlpha( *src))
	{
		throw strus::runtime_error(_TXT( "name of result attribute expected after SUMMARIZE"));
	}
	resultAttribute = parse_IDENTIFIER( src);
	if (!isAssign(*src))
	{
		throw strus::runtime_error(_TXT( "assignment operator '=' expected after the name of result attribute in summarizer definition"));
	}
	(void)parse_OPERATOR( src);
	if (!isAlpha( *src))
	{
		throw strus::runtime_error(_TXT( "name of summarizer function expected after assignment in summarizer definition"));
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
		if (isDigit(*src) || isMinus(*src))
		{
			if (isFeatureParam)
			{
				throw strus::runtime_error(_TXT( "feature parameter argument must be an identifier or string and not a number"));
			}
			ArithmeticVariant parameterValue = parseNumericValue( src);
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
	qeval.addSummarizerFunction( functionName, function.get(), featureParameters, resultAttribute);
	(void)function.release();
}


DLL_PUBLIC bool strus::loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* queryproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	enum StatementKeyword {e_EVAL, e_SELECTION, e_RESTRICTION, e_TERM, e_SUMMARIZE};
	std::string id;

	skipSpaces( src);
	try
	{
		while (*src)
		{
			switch ((StatementKeyword)parse_KEYWORD( src, 5, "EVAL", "SELECT", "RESTRICT", "TERM", "SUMMARIZE"))
			{
				case e_TERM:
					parseTermConfig( qeval, src);
					break;
				case e_SELECTION:
					while (*src && isAlnum( *src))
					{
						qeval.addSelectionFeature( parse_IDENTIFIER(src));
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
					parseWeightingConfig( qeval, queryproc, src);
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


static DocumentAnalyzerInterface::FeatureOptions
	parseFeatureOptions( char const*& src)
{
	DocumentAnalyzerInterface::FeatureOptions rt;
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
						rt.definePositionBind( DocumentAnalyzerInterface::FeatureOptions::BindSuccessor);
					}
					else if (utils::caseInsensitiveEquals( optval, "pred"))
					{
						rt.definePositionBind( DocumentAnalyzerInterface::FeatureOptions::BindPredecessor);
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
		while (*src && *src != ';' && *src != '{')
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

static void parseFeatureDef(
	DocumentAnalyzerInterface& analyzer,
	const TextProcessorInterface* textproc,
	const std::string& featurename,
	char const*& src,
	FeatureClass featureClass)
{
	std::auto_ptr<TokenizerFunctionInstanceInterface> tokenizer;
	std::vector<Reference<NormalizerFunctionInstanceInterface> > normalizer_ref;
	std::vector<NormalizerFunctionInstanceInterface*> normalizer;

	// [1] Parse normalizer:
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
	// [2] Parse tokenizer:
	FunctionConfig tokenizercfg = parseTokenizerConfig( src);
	const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
	if (!tk) throw strus::runtime_error(_TXT( "tokenizer function '%s' not found"), tokenizercfg.name().c_str());

	tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));
	if (!tokenizer.get()) throw strus::runtime_error(_TXT( "failed to create instance of tokenizer function '%s'"), tokenizercfg.name().c_str());

	// [3] Parse feature options, if defined:
	DocumentAnalyzerInterface::FeatureOptions featopt( parseFeatureOptions( src));

	// [4] Parse selection expression:
	std::string xpathexpr( parseSelectorExpression( src));

	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.addSearchIndexFeature(
				featurename, xpathexpr,
				tokenizer.get(), normalizer,
				featopt);
			break;

		case FeatForwardIndexTerm:
			analyzer.addForwardIndexFeature(
				featurename, xpathexpr,
				tokenizer.get(), normalizer,
				featopt);
			break;

		case FeatMetaData:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for meta data feature"));
			}
			analyzer.defineMetaData(
				featurename, xpathexpr,
				tokenizer.get(), normalizer);
			break;

		case FeatAttribute:
			if (featopt.opt())
			{
				throw strus::runtime_error( _TXT("no feature options expected for attribute feature"));
			}
			analyzer.defineAttribute(
				featurename, xpathexpr,
				tokenizer.get(), normalizer);
			break;
		case FeatSubDocument:
			throw std::logic_error("illegal call of parse feature definition for sub document");
		case FeatAggregator:
			throw std::logic_error("illegal call of parse feature definition for aggregator");
	}
	std::vector<Reference<NormalizerFunctionInstanceInterface> >::iterator
		ri = normalizer_ref.begin(), re = normalizer_ref.end();
	for (; ri != re; ++ri)
	{
		(void)ri->release();
	}
	(void)tokenizer.release();
}


DLL_PUBLIC bool strus::loadDocumentAnalyzerProgram(
		DocumentAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		FeatureClass featclass = FeatSearchIndexTerm;
		
		while (*src)
		{
			if (isOpenSquareBracket( *src))
			{
				(void)parse_OPERATOR(src);
				if (!isAlnum(*src))
				{
					throw strus::runtime_error( _TXT("feature class identifier expected after open square bracket '['"));
				}
				featclass = featureClassFromName( parse_IDENTIFIER( src));
				if (!isCloseSquareBracket( *src))
				{
					throw strus::runtime_error( _TXT("close square bracket ']' expected to close feature class section definition"));
				}
				(void)parse_OPERATOR(src);
			}
			if (!isAlnum(*src))
			{
				throw strus::runtime_error( _TXT("feature type name (identifier) expected at start of a feature declaration"));
			}
			std::string identifier = parse_IDENTIFIER( src);
			if (!isAssign( *src))
			{
				throw strus::runtime_error( _TXT("assignment operator '=' expected after set identifier in a feature declaration"));
			}
			(void)parse_OPERATOR(src);
			if (featclass == FeatSubDocument)
			{
				std::string xpathexpr( parseSelectorExpression( src));
				analyzer.defineSubDocument( identifier, xpathexpr);
			}
			else if (featclass == FeatAggregator)
			{
				std::auto_ptr<AggregatorFunctionInstanceInterface> statfunc;
				FunctionConfig cfg = parseAggregatorFunctionConfig( src);

				const AggregatorFunctionInterface* sf = textproc->getAggregator( cfg.name());
				if (!sf) throw strus::runtime_error(_TXT( "unknown aggregator function '%s'"), cfg.name().c_str());
				
				statfunc.reset( sf->createInstance( cfg.args()));
				if (!statfunc.get()) throw strus::runtime_error(_TXT( "failed to create instance of aggregator function '%s'"), cfg.name().c_str());

				analyzer.defineAggregatedMetaData( identifier, statfunc.get());
				statfunc.release();
			}
			else
			{
				parseFeatureDef( analyzer, textproc, identifier, src, featclass);
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


DLL_PUBLIC bool strus::loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		while (*src)
		{
			if (!isAlpha(*src))
			{
				throw strus::runtime_error( _TXT("identifier (feature type name) expected after assign '=' in a query phrase type declaration"));
			}
			std::string featureType = parse_IDENTIFIER( src);
			std::string phraseType = featureType;
			if (isSlash( *src))
			{
				(void)parse_OPERATOR(src);

				if (!isAlnum(*src))
				{
					throw strus::runtime_error( _TXT("alphanumeric identifier (phrase type) after feature type name and slash '/' "));
				}
				phraseType = parse_IDENTIFIER( src);
			}
			if (!isAssign( *src))
			{
				throw strus::runtime_error( _TXT("assignment operator '=' expected after feature type identifier in a query phrase type declaration"));
			}
			(void)parse_OPERATOR(src);
	
			std::auto_ptr<TokenizerFunctionInstanceInterface> tokenizer;
			std::vector<Reference<NormalizerFunctionInstanceInterface> > normalizer_ref;
			std::vector<NormalizerFunctionInstanceInterface*> normalizer;

			std::vector<FunctionConfig> normalizercfg = parseNormalizerConfig( src);
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
			FunctionConfig tokenizercfg = parseTokenizerConfig( src);
			const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
			if (!tk) throw strus::runtime_error(_TXT( "tokenizer function '%s' not found"), tokenizercfg.name().c_str());

			tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));
			if (!tokenizer.get()) throw strus::runtime_error(_TXT( "failed to create instance of tokenizer function '%s'"), tokenizercfg.name().c_str());

			analyzer.definePhraseType(
				phraseType, featureType, tokenizer.get(), normalizer);

			std::vector<Reference<NormalizerFunctionInstanceInterface> >::iterator
				ri = normalizer_ref.begin(), re = normalizer_ref.end();
			for (; ri != re; ++ri)
			{
				(void)ri->release();
			}
			(void)tokenizer.release();

			if (!isSemiColon(*src))
			{
				throw strus::runtime_error( _TXT("semicolon ';' expected at end of query phrase type declaration"));
			}
			(void)parse_OPERATOR(src);
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("out of memory loading query analyzer program %s"), pos.c_str());
		return false;
	}
	catch (const std::runtime_error& e)
	{
		ErrorPosition pos( source.c_str(), src);
		errorhnd->report(_TXT("error in query analyzer program %s: %s"), pos.c_str(), e.what());
		return false;
	}
}


DLL_PUBLIC bool strus::loadQueryAnalyzerPhraseType(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& phraseType,
		const std::string& featureType,
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
	
		analyzer.definePhraseType(
			phraseType, featureType, tokenizer.get(), normalizer);
	
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


static std::string parseQueryPhraseType( char const*& src)
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


struct QueryStackElement
{
	QueryStackElement( const PostingJoinOperatorInterface* function_, int arg_, int range_, unsigned int cardinality_, float weight_)
		:function(function_),arg(arg_),range(range_),cardinality(cardinality_),weight(weight_){}
	QueryStackElement( const QueryStackElement& o)
		:function(o.function),arg(o.arg),range(o.range),cardinality(o.cardinality),name(o.name),weight(o.weight){}
	QueryStackElement()
		:function(0),arg(-1),range(0),cardinality(0),weight(0.0f){}

	const PostingJoinOperatorInterface* function;
	int arg;
	int range;
	unsigned int cardinality;
	std::string name;
	float weight;
};

struct QueryStack
{
	QueryStack( const QueryStack& o)
		:ar(o.ar),phraseBulk(o.phraseBulk){}
	QueryStack(){}

	std::vector<QueryStackElement> ar;
	std::vector<QueryAnalyzerInterface::Phrase> phraseBulk;

	void defineFeature( const std::string& featureSet, float weight)
	{
		ar.push_back( QueryStackElement( 0, -1/*arg*/, 0/*range*/, 0/*cardinality*/, weight));
		ar.back().name = featureSet;
	}

	void pushPhrase( const std::string& phraseType, const std::string& phraseContent, const std::string& variableName)
	{
		ar.push_back( QueryStackElement( 0, phraseBulk.size(), 0/*range*/, 0/*cardinality*/, 0.0/*weight*/));
		phraseBulk.push_back( QueryAnalyzerInterface::Phrase( phraseType, phraseContent));
		if (!variableName.empty())
		{
			ar.back().name = variableName;
		}
	}

	void pushExpression( const PostingJoinOperatorInterface* function, int arg, int range, unsigned int cardinality, const std::string& variableName)
	{
		ar.push_back( QueryStackElement( function, arg, range, cardinality, 0.0));
		if (!variableName.empty())
		{
			ar.back().name = variableName;
		}
	}
};

static void translateQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* queryproc,
		const QueryStack& stk)
{
	std::vector<analyzer::TermVector> analyzerResult = analyzer->analyzePhraseBulk( stk.phraseBulk);
	std::vector<QueryStackElement>::const_iterator si = stk.ar.begin(), se = stk.ar.end();
	for (; si != se; ++si)
	{
		if (si->function)
		{
			// Expression function definition:
			query.pushExpression( si->function, si->arg, si->range, si->cardinality);
			if (!si->name.empty())
			{
				query.attachVariable( si->name);
			}
		}
		else if (si->arg < 0)
		{
			// Feature definition:
			query.defineFeature( si->name, si->weight);
		}
		else
		{
			// Term definition:
			std::vector<analyzer::Term>::const_iterator
				ti = analyzerResult[ si->arg].begin(), te = analyzerResult[si->arg].end();
			if (ti == te)
			{
				throw strus::runtime_error( _TXT("query analyzer returned empty list of terms for query phrase %s: '%s'"),
								stk.phraseBulk[ si->arg].type().c_str(), stk.phraseBulk[ si->arg].content().c_str());
			}
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
						query.pushTerm( ti->type(), ti->value());
					}
					if (join_argc > 1)
					{
						const PostingJoinOperatorInterface* join 
							= queryproc->getPostingJoinOperator(
								Constants::operator_query_phrase_same_position());
						if (!join)
						{
							throw strus::runtime_error( _TXT("posting join operator not defined: '%s'"),
											Constants::operator_query_phrase_same_position());
						}
						query.pushExpression( join, join_argc, 0, 0);
					}
				}
			}
			if (seq_argc > 1)
			{
				const PostingJoinOperatorInterface* seq
					= queryproc->getPostingJoinOperator(
						Constants::operator_query_phrase_sequence());
				if (!seq)
				{
					throw strus::runtime_error( _TXT("posting join operator not defined: '%s'"),
									Constants::operator_query_phrase_sequence());
				}
				query.pushExpression( seq, seq_argc, pos, 0);
			}
			if (!si->name.empty())
			{
				query.attachVariable( si->name);
			}
		}
	}
}

static void parseQueryExpression(
		QueryStack& querystack,
		const QueryProcessorInterface* queryproc,
		const std::string& defaultPhraseType,
		char const*& src)
{
	std::string functionName;
	if (isAlpha( *src))
	{
		char const* src_bk = src;
		functionName = parse_IDENTIFIER(src);
		if (isOpenOvalBracket(*src))
		{
			(void)parse_OPERATOR( src);
			std::size_t argc = 0;

			if (!isCloseOvalBracket( *src)) while (*src)
			{
				argc++;
				parseQueryExpression( querystack, queryproc, defaultPhraseType, src);
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
					range = parse_INTEGER( src);
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

			querystack.pushExpression( function, argc, range, cardinality, variableName);
			return;
		}
		else
		{
			src = src_bk;
		}
	}
	if (isTextChar( *src) || isStringQuote( *src))
	{
		std::string queryPhrase = parseQueryTerm( src);
		std::string phraseType = parseQueryPhraseType( src);
		if (phraseType.empty())
		{
			phraseType = defaultPhraseType;
		}
		std::string variableName = parseVariableRef( src);

		querystack.pushPhrase( phraseType, queryPhrase, variableName);
	}
	else
	{
		throw strus::runtime_error( _TXT("syntax error in query, query expression or term expected"));
	}
}

static ArithmeticVariant parseMetaDataOperand( char const*& src)
{
	try
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
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("error parsing meta data restriction operand: %s"), err.what());
	}
}

static std::vector<ArithmeticVariant> parseMetaDataOperands( char const*& src)
{
	std::vector<ArithmeticVariant> rt;
	for (;;)
	{
		if (isStringQuote( *src))
		{
			std::string value = parse_STRING( src);
			char const* vv = value.c_str();
			rt.push_back( parseMetaDataOperand( vv));
		}
		else
		{
			rt.push_back( parseMetaDataOperand( src));
		}
		if (isComma( *src))
		{
			(void)parse_OPERATOR( src);
			continue;
		}
		break;
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
	throw strus::runtime_error( _TXT("bad query meta data operator"));
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
			throw strus::runtime_error( _TXT("unknown meta data comparison operator"));
		}
	}
	else
	{
		throw strus::runtime_error( _TXT("expected meta data comparison operator"));
	}
	skipSpaces( src);
	if (*src && !isAlnum( *src) && !isStringQuote( *src))
	{
		throw strus::runtime_error( _TXT("unexpected character after meta data comparison operator"));
	}
	return rt;
}

static void parseMetaDataRestriction(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		char const*& src)
{
	if (isAlpha( *src))
	{
		std::string fieldname = parse_IDENTIFIER( src);

		QueryInterface::CompareOperator
			cmpop = parseMetaDataComparionsOperator( src);

		std::vector<ArithmeticVariant>
			operands = parseMetaDataOperands( src);

		std::vector<ArithmeticVariant>::const_iterator
			oi = operands.begin(), oe = operands.end();
		query.defineMetaDataRestriction( cmpop, fieldname, *oi, true);
		for (++oi; oi != oe; ++oi)
		{
			query.defineMetaDataRestriction( cmpop, fieldname, *oi, false);
		}
	}
	else if (isStringQuote( *src) || isDigit( *src) || isMinus( *src))
	{
		std::vector<ArithmeticVariant>
			operands = parseMetaDataOperands( src);

		QueryInterface::CompareOperator
			cmpop = invertedOperator( parseMetaDataComparionsOperator( src));

		if (!isAlpha( *src))
		{
			throw strus::runtime_error( _TXT("expected at least one meta data field identifier in query restriction expression"));
		}
		std::string fieldname = parse_IDENTIFIER( src);

		std::vector<ArithmeticVariant>::const_iterator
			oi = operands.begin(), oe = operands.end();
		query.defineMetaDataRestriction( cmpop, fieldname, *oi, true);
		for (++oi; oi != oe; ++oi)
		{
			query.defineMetaDataRestriction( cmpop, fieldname, *oi, false);
		}
	}
}


DLL_PUBLIC bool strus::loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* queryproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd)
{
	char const* src = source.c_str();
	try
	{
		QueryStack querystack;
		skipSpaces(src);
		while (*src)
		{
			// Parse query section:
			if (!isOpenSquareBracket( *src))
			{
				throw strus::runtime_error( _TXT("expected open square bracket to start query section declaration"));
			}
			(void)parse_OPERATOR( src);
			if (!isAlnum(*src))
			{
				throw strus::runtime_error( _TXT("query section identifier expected after open square bracket '['"));
			}
			std::string name = parse_IDENTIFIER( src);
			if (isEqual( name, "Feature"))
			{
				float featureWeight = 1.0;

				if (!isAlnum( *src))
				{
					throw strus::runtime_error( _TXT("feature set identifier expected after keyword 'Feature' in query section definition"));
				}
				std::string featureSet = parse_IDENTIFIER( src);

				if (!isColon(*src))
				{
					throw strus::runtime_error( _TXT("colon ':' expected after feature set name in query section definition"));
				}
				(void)parse_OPERATOR(src);
				std::string defaultPhraseType = parse_IDENTIFIER( src);

				if (isDigit(*src))
				{
					featureWeight = parse_FLOAT( src);
				}
				else
				{
					featureWeight = 1.0;
				}
				if (!isCloseSquareBracket( *src))
				{
					throw strus::runtime_error( _TXT("close square bracket ']' expected to terminate query section declaration"));
				}
				(void)parse_OPERATOR( src);
				while (*src && !isOpenSquareBracket( *src))
				{
					parseQueryExpression( querystack, queryproc, defaultPhraseType, src);
					querystack.defineFeature( featureSet, featureWeight);
				}
			}
			else if (isEqual( name, "Condition"))
			{
				if (!isCloseSquareBracket( *src))
				{
					throw strus::runtime_error( _TXT("close square bracket ']' expected to terminate query section declaration"));
				}
				while (*src && !isOpenSquareBracket( *src))
				{
					parseMetaDataRestriction( query, analyzer, src);
					if (isSemiColon(*src))
					{
						(void)parse_OPERATOR( src);
					}
					else if (*src && !isOpenSquareBracket( *src))
					{
						throw strus::runtime_error( _TXT("semicolon ';' as separator of meta data restrictions"));
					}
				}
			}
			else
			{
				throw strus::runtime_error( _TXT("unknown query section identifier '%s'"),name.c_str());
			}
		}
		translateQuery( query, analyzer, queryproc, querystack);
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

static void storeMetaDataValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const ArithmeticVariant& val)
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
	unsigned int rt = 0;
	std::auto_ptr<StorageTransactionInterface>
		transaction( storage.createTransaction());
	if (!transaction.get()) throw strus::runtime_error( _TXT("failed to create storage transaction"));
	std::size_t linecnt = 1;
	unsigned int commitcnt = 0;
	try
	{
		char line[ 2048];
		for (; stream.readline( line, sizeof(line)); ++linecnt)
		{
			char const* itr = line;
			Index docno = parseDocno( storage, itr);

			if (!docno) continue;
			switch (valueType)
			{
				case StorageValueMetaData:
				{
					ArithmeticVariant val( parseNumericValue( itr));
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



