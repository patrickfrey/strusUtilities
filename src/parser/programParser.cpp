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
#include "parser.hpp"
#include "lexems.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/weightingConfigInterface.hpp"
#include "strus/summarizerConfigInterface.hpp"
#include "strus/summarizerFunctionInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
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
		parse_OPERATOR(src);
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
	parse_OPERATOR(src);

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
		parse_OPERATOR(src);
		wcfg->defineNumericParameter( parameterName, parseNumericValue( src));

		if (!isComma( *src))
		{
			break;
		}
		parse_OPERATOR(src);
	}
	if (!isCloseOvalBracket( *src))
	{
		throw std::runtime_error( "close oval bracket ')' expected at end of weighting function parameter list");
	}
	parse_OPERATOR(src);
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
						parse_OPERATOR( src);
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
						parse_OPERATOR( src);
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
	parse_OPERATOR( src);
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
	parse_OPERATOR(src);

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
		parse_OPERATOR(src);
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
		parse_OPERATOR(src);
	}
	if (!isCloseOvalBracket( *src))
	{
		throw std::runtime_error( "close oval bracket ')' expected at end of summarizer function parameter list");
	}
	parse_OPERATOR(src);
	summarizer->done();
}


void strus::loadQueryEvalProgram(
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
				parse_OPERATOR( src);
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

