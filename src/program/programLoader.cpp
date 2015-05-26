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
#include "private/dll_tags.hpp"
#include "strus/constants.hpp"
#include "strus/private/protocol.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/weightingFunctionInterface.hpp"
#include "strus/weightingFunctionInstanceInterface.hpp"
#include "strus/summarizerFunctionInterface.hpp"
#include "strus/summarizerFunctionInstanceInterface.hpp"
#include "strus/normalizerFunctionInterface.hpp"
#include "strus/normalizerFunctionInstanceInterface.hpp"
#include "strus/tokenizerFunctionInterface.hpp"
#include "strus/tokenizerFunctionInstanceInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageDocumentUpdateInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/peerStorageTransactionInterface.hpp"
#include "strus/analyzer/term.hpp"
#include "strus/reference.hpp"
#include "private/inputStream.hpp"
#include "private/utils.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>

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
		throw std::runtime_error( "query term (identifier,word,number or string) expected");
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
			throw std::runtime_error( "term value (string,identifier,number) after the feature group identifier");
		}
		std::string termvalue = parseQueryTerm( src);
		if (!isColon( *src))
		{
			throw std::runtime_error( "colon (':') expected after term value");
		}
		(void)parse_OPERATOR(src);
		if (!isAlpha( *src))
		{
			throw std::runtime_error( "term type identifier expected after colon and term value");
		}
		std::string termtype = utils::tolower( parse_IDENTIFIER( src));
		qeval.addTerm( termset, termtype, termvalue);
	}
	else
	{
		throw std::runtime_error( "feature set identifier expected as start of a term declaration in the query");
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
			throw std::runtime_error( "multiplication operator '*' expected after EVAL followed by a floating point number (weight)");
		}
		(void)parse_OPERATOR(src);
	}
	if (!isAlpha( *src))
	{
		throw std::runtime_error( "weighting function identifier expected");
	}
	std::string functionName = parse_IDENTIFIER( src);
	const WeightingFunctionInterface* wf = queryproc->getWeightingFunction( functionName);
	std::auto_ptr<WeightingFunctionInstanceInterface> function( wf->createInstance());
	typedef QueryEvalInterface::FeatureParameter FeatureParameter;
	std::vector<FeatureParameter> featureParameters;

	if (!isOpenOvalBracket( *src))
	{
		throw std::runtime_error( "open oval bracket '(' expected after weighting function identifier");
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
			throw std::runtime_error( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected");
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw std::runtime_error( "assingment operator '=' expected after weighting function parameter name");
		}
		(void)parse_OPERATOR(src);
		if (isDigit(*src) || isMinus(*src))
		{
			if (isFeatureParam)
			{
				throw std::runtime_error("feature parameter argument must be an identifier or string and not a number");
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
		throw std::runtime_error( "close oval bracket ')' expected at end of weighting function parameter list");
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
	functionName = utils::tolower( parse_IDENTIFIER( src));
	const SummarizerFunctionInterface* sf = queryproc->getSummarizerFunction( functionName);
	std::auto_ptr<SummarizerFunctionInstanceInterface> function( sf->createInstance( queryproc));

	if (!isOpenOvalBracket( *src))
	{
		throw std::runtime_error( "open oval bracket '(' expected after summarizer function identifier");
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
			throw std::runtime_error( "identifier as start of parameter declaration (assignment parameter name to parameter value) expected");
		}
		std::string parameterName = parse_IDENTIFIER( src);
		if (!isAssign( *src))
		{
			throw std::runtime_error( "assignment operator '=' expected after summarizer function parameter name");
		}
		(void)parse_OPERATOR(src);
		if (isDigit(*src) || isMinus(*src))
		{
			if (isFeatureParam)
			{
				throw std::runtime_error("feature parameter argument must be an identifier or string and not a number");
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
		throw std::runtime_error( "close oval bracket ')' expected at end of summarizer function parameter list");
	}
	(void)parse_OPERATOR(src);
	qeval.addSummarizerFunction( functionName, function.get(), featureParameters, resultAttribute);
	(void)function.release();
}


DLL_PUBLIC void strus::loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* queryproc,
		const std::string& source)
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
			+ ": " + e.what());
	}
}

enum FeatureClass
{
	FeatSearchIndexTerm,
	FeatForwardIndexTerm,
	FeatMetaData,
	FeatAttribute,
	FeatSubDocument
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
				throw std::runtime_error( std::string( "comma ',' as argument separator or close oval brakcet ')' expected at end of ") + functype + " argument list");
			}
		}
		else
		{
			arg.clear();
		}
	}
	else
	{
		throw std::runtime_error( std::string(functype) + " definition (identifier) expected");
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
					throw std::runtime_error( "assign '=' expected after open curly brackets '{' and option identifier");
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
					throw std::runtime_error( "identifier or string expected as option value");
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
						throw std::runtime_error( std::string( "'pred' or 'succ' expected as 'position' option value instead of '") + optval + "'");
					}
				}
				else
				{
					throw std::runtime_error( std::string( "unknown option '") + optname + "'");
				}
			}
		}
		while (isComma( *src));

		if (!isCloseCurlyBracket( *src))
		{
			throw std::runtime_error( "close curly bracket '}' expected at end of option list");
		}
		(void)parse_OPERATOR( src);
	}
	return rt;
}

static void parseFeatureDef(
	DocumentAnalyzerInterface& analyzer,
	const TextProcessorInterface* textproc,
	const std::string& featurename,
	char const*& src,
	FeatureClass featureClass)
{
	std::string xpathexpr;
	std::auto_ptr<TokenizerFunctionInstanceInterface> tokenizer;
	std::vector<Reference<NormalizerFunctionInstanceInterface> > normalizer_ref;
	std::vector<NormalizerFunctionInstanceInterface*> normalizer;
	
	if (featureClass != FeatSubDocument)
	{
		std::vector<FunctionConfig> normalizercfg = parseNormalizerConfig( src);
		std::vector<FunctionConfig>::const_iterator ni = normalizercfg.begin(), ne = normalizercfg.end();
		for (; ni != ne; ++ni)
		{
			const NormalizerFunctionInterface* nm = textproc->getNormalizer( ni->name());
			normalizer_ref.push_back( nm->createInstance( ni->args(), textproc));
			normalizer.push_back( normalizer_ref.back().get());
		}
		FunctionConfig tokenizercfg = parseTokenizerConfig( src);
		const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
		tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));
	}
	if (isStringQuote(*src))
	{
		xpathexpr = parse_STRING( src);
	}
	else
	{
		char const* start = src;
		while (*src && !isSpace(*src) && *src != ';' && *src != '{')
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
		xpathexpr.append( start, src-start);
		skipSpaces( src);
	}

	switch (featureClass)
	{
		case FeatSearchIndexTerm:
			analyzer.addSearchIndexFeature(
				featurename, xpathexpr,
				tokenizer.get(), normalizer,
				parseFeatureOptions( src));
			break;

		case FeatForwardIndexTerm:
			analyzer.addForwardIndexFeature(
				featurename, xpathexpr,
				tokenizer.get(), normalizer,
				parseFeatureOptions( src));
			break;

		case FeatMetaData:
			analyzer.defineMetaData(
				featurename, xpathexpr,
				tokenizer.get(), normalizer);
			break;

		case FeatAttribute:
			analyzer.defineAttribute(
				featurename, xpathexpr,
				tokenizer.get(), normalizer);
			break;
		case FeatSubDocument:
			analyzer.defineSubDocument( featurename, xpathexpr);
			break;
	}
	std::vector<Reference<NormalizerFunctionInstanceInterface> >::iterator
		ri = normalizer_ref.begin(), re = normalizer_ref.end();
	for (; ri != re; ++ri)
	{
		(void)ri->release();
	}
	(void)tokenizer.release();
}


DLL_PUBLIC void strus::loadDocumentAnalyzerProgram(
		DocumentAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source)
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
					throw std::runtime_error( "feature class identifier expected after open square bracket '['");
				}
				featclass = featureClassFromName( parse_IDENTIFIER( src));
				if (!isCloseSquareBracket( *src))
				{
					throw std::runtime_error( "close square bracket ']' expected to close feature class section definition");
				}
				(void)parse_OPERATOR(src);
			}
			if (!isAlnum(*src))
			{
				throw std::runtime_error( "feature type name (identifier) expected at start of a feature declaration");
			}
			std::string featuretype = parse_IDENTIFIER( src);
			if (isAssign( *src))
			{
				(void)parse_OPERATOR(src);
				parseFeatureDef( analyzer, textproc, featuretype, src, featclass);
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
			+ ": " + e.what());
	}
}

DLL_PUBLIC void strus::loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source)
{
	char const* src = source.c_str();
	skipSpaces(src);
	try
	{
		while (*src)
		{
			if (!isAlpha(*src))
			{
				throw std::runtime_error( "identifier (feature type name) expected after assign '=' in a query phrase type declaration");
			}
			std::string featureType = parse_IDENTIFIER( src);
			std::string phraseType = featureType;
			if (isSlash( *src))
			{
				(void)parse_OPERATOR(src);

				if (!isAlnum(*src))
				{
					throw std::runtime_error( "alphanumeric identifier (phrase type) after feature type name and slash '/' ");
				}
				phraseType = parse_IDENTIFIER( src);
			}
			if (!isAssign( *src))
			{
				throw std::runtime_error( "assignment operator '=' expected after feature type identifier in a query phrase type declaration");
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
				normalizer_ref.push_back( nm->createInstance( ni->args(), textproc));
				normalizer.push_back( normalizer_ref.back().get());
			}
			FunctionConfig tokenizercfg = parseTokenizerConfig( src);
			const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
			tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));

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
				throw std::runtime_error( "semicolon ';' expected at end of query phrase type declaration");
			}
			(void)parse_OPERATOR(src);
		}
	}
	catch (const std::runtime_error& e)
	{
		throw std::runtime_error(
			std::string( "error in query analyzer program ")
			+ errorPosition( source.c_str(), src)
			+ ": " + e.what());
	}
}

DLL_PUBLIC void strus::loadQueryAnalyzerPhraseType(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& phraseType,
		const std::string& featureType,
		const std::string& normalizersrc,
		const std::string& tokenizersrc)
{
	std::auto_ptr<TokenizerFunctionInstanceInterface> tokenizer;
	std::vector<Reference<NormalizerFunctionInstanceInterface> > normalizer_ref;
	std::vector<NormalizerFunctionInstanceInterface*> normalizer;

	char const* nsrc = normalizersrc.c_str();
	std::vector<FunctionConfig> normalizercfg = parseNormalizerConfig( nsrc);
	if ((std::size_t)(nsrc - normalizersrc.c_str()) < normalizersrc.size())
	{
		throw std::runtime_error( std::string( "unexpected token after end of normalizer definition: '") + nsrc + "'");
	}
	std::vector<FunctionConfig>::const_iterator ni = normalizercfg.begin(), ne = normalizercfg.end();
	for (; ni != ne; ++ni)
	{
		const NormalizerFunctionInterface* nm = textproc->getNormalizer( ni->name());
		normalizer_ref.push_back( nm->createInstance( ni->args(), textproc));
		normalizer.push_back( normalizer_ref.back().get());
	}
	char const* tsrc = tokenizersrc.c_str();
	FunctionConfig tokenizercfg = parseTokenizerConfig( tsrc);
	if ((std::size_t)(tsrc - tokenizersrc.c_str()) < tokenizersrc.size())
	{
		throw std::runtime_error( std::string( "unexpected token after end of tokenizer definition: '") + nsrc + "'");
	}
	const TokenizerFunctionInterface* tk = textproc->getTokenizer( tokenizercfg.name());
	tokenizer.reset( tk->createInstance( tokenizercfg.args(), textproc));

	analyzer.definePhraseType(
		phraseType, featureType, tokenizer.get(), normalizer);

	std::vector<Reference<NormalizerFunctionInstanceInterface> >::iterator
		ri = normalizer_ref.begin(), re = normalizer_ref.end();
	for (; ri != re; ++ri)
	{
		(void)ri->release();
	}
	tokenizer.release();
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
			throw std::runtime_error( "query analyze phrase type (identifier) expected after colon ':' in query");
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

static void pushQueryPhrase(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* queryproc,
		const std::string& phraseType,
		const std::string& content)
{
	std::vector<analyzer::Term>
		queryTerms = analyzer->analyzePhrase( phraseType, content);
	std::vector<analyzer::Term>::const_iterator
		ti = queryTerms.begin(), te = queryTerms.end();
	if (ti == te)
	{
		throw std::runtime_error( std::string( "query analyzer returned empty list of terms for query phrase '") + content + "'");
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
				
				query.pushExpression( join, join_argc, 0);
			}
		}
	}
	if (seq_argc > 1)
	{
		const PostingJoinOperatorInterface* seq
			= queryproc->getPostingJoinOperator(
				Constants::operator_query_phrase_sequence());
		query.pushExpression( seq, seq_argc, pos);
	}
}


static void parseQueryExpression(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
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
				parseQueryExpression( query, analyzer, queryproc, defaultPhraseType, src);
				if (isComma( *src))
				{
					(void)parse_OPERATOR( src);
					continue;
				}
				break;
			}
			int range = 0;
			if (isOr( *src))
			{
				(void)parse_OPERATOR( src);
				range = parse_INTEGER( src);
			}
			if (!isCloseOvalBracket( *src))
			{
				throw std::runtime_error( "comma ',' as query argument separator or colon ':' as range specifier or close oval bracket ')' as end of a query expression expected");
			}
			(void)parse_OPERATOR( src);
			const PostingJoinOperatorInterface*
				function = queryproc->getPostingJoinOperator( functionName);
			query.pushExpression( function, argc, range);
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
		pushQueryPhrase( query, analyzer, queryproc, phraseType, queryPhrase);
		std::string variableName = parseVariableRef( src);
		if (!variableName.empty())
		{
			query.attachVariable( variableName);
		}
	}
	else
	{
		throw std::runtime_error( "syntax error in query, query expression or term expected");
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
		throw std::runtime_error( std::string(err.what()) + " parsing meta data restriction operand");
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
			cmpop = invertedOperator(
					parseMetaDataComparionsOperator( src));

		if (!isAlpha( *src))
		{
			throw std::runtime_error( "expected at least one meta data field identifier in query restriction expression");
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


DLL_PUBLIC void strus::loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* queryproc,
		const std::string& source)
{
	char const* src = source.c_str();
	try
	{
		skipSpaces(src);
		while (*src)
		{
			// Parse query section:
			if (!isOpenSquareBracket( *src))
			{
				throw std::runtime_error( "expected open square bracket to start query section declaration");
			}
			(void)parse_OPERATOR( src);
			if (!isAlnum(*src))
			{
				throw std::runtime_error("query section identifier expected after open square bracket '['");
			}
			std::string name = parse_IDENTIFIER( src);
			if (isEqual( name, "Feature"))
			{
				float featureWeight = 1.0;

				if (!isAlnum( *src))
				{
					throw std::runtime_error( "feature set identifier expected after keyword 'Feature' in query section definition");
				}
				std::string featureSet = parse_IDENTIFIER( src);

				if (!isColon(*src))
				{
					throw std::runtime_error( "colon ':' expected after feature set name in query section definition");
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
					throw std::runtime_error( "close square bracket ']' expected to terminate query section declaration");
				}
				(void)parse_OPERATOR( src);
				while (*src && !isOpenSquareBracket( *src))
				{
					parseQueryExpression( query, analyzer, queryproc, defaultPhraseType, src);
					query.defineFeature( featureSet, featureWeight);
				}
			}
			else if (isEqual( name, "Condition"))
			{
				if (!isCloseSquareBracket( *src))
				{
					throw std::runtime_error( "close square bracket ']' expected to terminate query section declaration");
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
						throw std::runtime_error( "semicolon ';' as separator of meta data restrictions");
					}
				}
			}
			else
			{
				throw std::runtime_error( std::string( "unknown query section identifier '") + name + "'");
			}
		}
	}
	catch (const std::runtime_error& e)
	{
		throw std::runtime_error(
			std::string( "error in query ")
			+ errorPosition( source.c_str(), src)
			+ ": " + e.what());
	}
}


DLL_PUBLIC bool strus::scanNextProgram(
		std::string& segment,
		std::string::const_iterator& si,
		const std::string::const_iterator& se)
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


static std::string readToken( std::string::const_iterator& si, const std::string::const_iterator& se)
{
	std::string rt;
	for (;*si && !isSpace( *si); ++si)
	{
		rt.push_back( *si);
	}
	for (; *si && isSpace( *si); ++si){}
	return rt;
}

static int readInteger( std::string::const_iterator& si, const std::string::const_iterator& se)
{
	int rt = 0, prev_rt = 0;
	bool sign = false;
	if (*si == '-')
	{
		sign = true;
		++si;
	}
	for (;*si && isDigit( *si); ++si)
	{
		rt = rt * 10 + *si - '0';
		if (prev_rt > rt)
		{
			throw std::runtime_error( "integer value out of range");
		}
		prev_rt = rt;
	}
	for (; isSpace( *si); ++si){}
	return (sign)?-rt:rt;
}


DLL_PUBLIC void strus::loadGlobalStatistics(
		StorageClientInterface& storage,
		const std::string& file)
{
	std::auto_ptr<PeerStorageTransactionInterface>
		transaction( storage.createPeerStorageTransaction());
	std::size_t linecnt = 1;
	InputStream stream( file);
	try
	{
		char line[ 2048];
		for (; stream.readline( line, sizeof(line)); ++linecnt)
		{
			std::string linebuf( line);
			std::string::const_iterator li = linebuf.begin(), le = linebuf.end();
			if (li != le)
			{
				std::string tok = readToken( li, le);
				if (tok == Constants::storage_statistics_document_frequency())
				{
					int df = readInteger( li, le);
					std::string termtype = readToken( li, le);
					std::string termvalue = Protocol::decodeString( readToken( li, le));

					transaction->updateDocumentFrequencyChange(
							termtype.c_str(), termvalue.c_str(), df);
				}
				else if (tok == Constants::storage_statistics_number_of_documents())
				{
					int nofDocs = readInteger( li, le);
					transaction->updateNofDocumentsInsertedChange( nofDocs);
				}
				else
				{
					throw std::runtime_error( "unexpected token at start of line");
				}
			}
			if (li != le)
			{
				throw std::runtime_error( "unconsumed characters at end of line");
			}
		}
		transaction->commit();
	}
	catch (const std::runtime_error& err)
	{
		throw std::runtime_error( std::string( "error on line ")
			+ utils::tostring( linecnt)
			+ ": " + err.what());
	}
}

static Index parseDocno( StorageClientInterface& storage, char const*& itr)
{
	/*[-]*/std::cout << "parseDocno " << itr << ' ' << (int)__LINE__ << std::endl;
	if (isDigit(*itr) && is_INTEGER(itr))
	{
		/*[-]*/std::cout << "parseDocno " << itr << ' ' << (int)__LINE__ << std::endl;
		return parse_UNSIGNED1( itr);
	}
	else if (isStringQuote(*itr))
	{
		/*[-]*/std::cout << "parseDocno " << itr << ' ' << (int)__LINE__ << std::endl;
		std::string docid = parse_STRING(itr);
		return storage.documentNumber( docid);
	}
	else
	{
		/*[-]*/std::cout << "parseDocno " << itr << ' ' << (int)__LINE__ << std::endl;
		std::string docid;
		for (; isSpace(*itr); ++itr)
		{
			docid.push_back( *itr);
		}
		/*[-]*/std::cout << "parseDocno " << docid << ' ' << (int)__LINE__ << std::endl;
		return storage.documentNumber( docid);
	}
}

static void storeMetaDataValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const ArithmeticVariant& val)
{
	std::auto_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
	update->setMetaData( name, val);
	update->done();
}

static void storeAttributeValue( StorageTransactionInterface& transaction, const Index& docno, const std::string& name, const std::string& val)
{
	std::auto_ptr<StorageDocumentUpdateInterface> update( transaction.createDocumentUpdate( docno));
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
			throw std::runtime_error("unexpected token in user rigths specification");
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
	std::size_t linecnt = 1;
	unsigned int commitcnt = 0;
	try
	{
		char line[ 2048];
		for (; stream.readline( line, sizeof(line)); ++linecnt)
		{
			/*[-]*/std::cout << "read line " << "(" << line << ")" << ' ' << (int)__LINE__ << std::endl;
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
				throw std::runtime_error("extra characters after value assignment");
			}
			if (++commitcnt == commitsize)
			{
				transaction->commit();
				commitcnt = 0;
				transaction.reset( storage.createTransaction());
			}
		}
		if (commitcnt)
		{
			transaction->commit();
			commitcnt = 0;
			transaction.reset( storage.createTransaction());
		}
		return rt;
	}
	catch (const std::runtime_error& err)
	{
		throw std::runtime_error( std::string( "error on line ")
			+ utils::tostring( linecnt)
			+ ": " + err.what());
	}
}


DLL_PUBLIC unsigned int strus::loadDocumentMetaDataAssignments(
		StorageClientInterface& storage,
		const std::string& metadataName,
		const std::string& file,
		unsigned int commitsize)
{
	return loadStorageValues( storage, metadataName, file, StorageValueMetaData, commitsize);
}


DLL_PUBLIC unsigned int strus::loadDocumentAttributeAssignments(
		StorageClientInterface& storage,
		const std::string& attributeName,
		const std::string& file,
		unsigned int commitsize)
{
	return loadStorageValues( storage, attributeName, file, StorageValueAttribute, commitsize);
}


DLL_PUBLIC unsigned int strus::loadDocumentUserRightsAssignments(
		StorageClientInterface& storage,
		const std::string& file,
		unsigned int commitsize)
{
	return loadStorageValues( storage, std::string(), file, StorageUserRights, commitsize);
}



