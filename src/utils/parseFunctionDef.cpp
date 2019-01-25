/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Some utility functions for parsing function definitions in program arguments
/// \file parseFunctionDef.cpp
#include "private/parseFunctionDef.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/programLexer.hpp"
#include "private/internationalization.hpp"

/// \brief strus toplevel namespace
using namespace strus;
using namespace strus::utils;

enum Tokens {
	TokIdentifier,
	TokOpenOvalBracket,
	TokCloseOvalBracket,
	TokComma,
	TokColon
};
static const char* g_tokens[] = {
	"[a-z0-9A-Z_][a-zA-Z0-9_.]*",
	"\\(",
	"\\)",
	",",
	":",
	NULL
};

static std::vector<std::string> parseArgumentList( strus::ProgramLexer& lexer)
{
	std::vector<std::string> rt;
	for (;;)
	{
		strus::ProgramLexem cur = lexer.current();
		if (cur.isToken( TokIdentifier))
		{
			rt.push_back( cur.value());
		}
		else if (cur.isString())
		{
			rt.push_back( cur.value());
		}
		else
		{
			throw strus::runtime_error( _TXT("unexpected token in argument list"));
		}
		cur = lexer.next();
		if (cur.isToken( TokComma))
		{
			cur = lexer.next();
			continue;
		}
		break;
	}
	return rt;
}

static FunctionDef parseFunctionDef( strus::ProgramLexer& lexer)
{
	strus::ProgramLexem cur = lexer.current();
	if (cur.isToken( TokIdentifier))
	{
		FunctionDef rt;
		rt.first = cur.value();
		cur = lexer.next();
		if (cur.isToken( TokOpenOvalBracket))
		{
			cur = lexer.next();
			if (cur.isToken( TokCloseOvalBracket))
			{
				lexer.next();
			}
			else
			{
				rt.second = parseArgumentList( lexer);
			}
			if (!lexer.current().isToken( TokCloseOvalBracket))
			{
				throw strus::runtime_error( _TXT("comma ',' as argument separator or close oval bracket ')' at end of %s argument list"), "function");
			}
			lexer.next();
		}
		return rt;
	}
	else
	{
		throw strus::runtime_error( _TXT("function name expected"));
	}
}

std::vector<FunctionDef> utils::parseFunctionDefs( const std::string& parameter, ErrorBufferInterface* errorhnd)
{
	try
	{
		strus::ProgramLexer lexer( parameter.c_str(), NULL/*eolncomment*/, g_tokens, NULL/*errtokens*/, errorhnd);
		lexer.next();
		std::vector<FunctionDef> rt;
		for(;;)
		{
			rt.insert( rt.begin(), parseFunctionDef( lexer));

			if (!lexer.current().isToken( TokColon)) break;
			lexer.next();
		}
		return rt;
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error(_TXT("error parsing function definition '%s': %s"), parameter.c_str(), err.what());
	}
	if (errorhnd->hasError())
	{
		throw strus::runtime_error(_TXT("error parsing function definition '%s': %s"), parameter.c_str(), errorhnd->fetchError());
	}
}



