/*
* Copyright (c) 2017 Patrick P. Frey
*
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/
/// \brief Program to execute tests defined as scripts
/// \file execTest.cpp
#include "strus/base/fileio.hpp"
#include "strus/base/exec.hpp"
#include "private/internationalization.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <unistd.h>

#if defined _WIN32
#error Executing test programs with execTest is not implemented
#endif

static std::string g_testname;
static std::string g_maindir;
static std::string g_bindir;
static std::string g_testdir;
static std::string g_execdir;
static std::map<std::string,std::string> g_env;

struct ProgramPath
{
	const char* name;
	const char* path;
};

static ProgramPath g_prgpathmap[] =
{
	{"StrusDumpStorage", "src/strusDumpStorage/strusDumpStorage"},
	{"StrusAnalyze", "src/strusAnalyze/strusAnalyze"},
	{"StrusDeleteDocument", "src/strusDeleteDocument/strusDeleteDocument"},
	{"StrusPatternSerialize", "src/strusPatternSerialize/strusPatternSerialize"},
	{"StrusDestroy", "src/strusDestroy/strusDestroy"},
	{"StrusBuildVectorStorage", "src/strusBuildVectorStorage/strusBuildVectorStorage"},
	{"StrusAnalyzeQuery", "src/strusAnalyzeQuery/strusAnalyzeQuery"},
	{"StrusDumpStatistics", "src/strusDumpStatistics/strusDumpStatistics"},
	{"StrusPatternMatcher", "src/strusPatternMatcher/strusPatternMatcher"},
	{"StrusQuery", "src/strusQuery/strusQuery"},
	{"StrusCreate", "src/strusCreate/strusCreate"},
	{"StrusCheckStorage", "src/strusCheckStorage/strusCheckStorage"},
	{"StrusHelp", "src/strusHelp/strusHelp"},
	{"StrusUpdateStorageCalcStatistics", "src/strusUpdateStorageCalcStatistics/strusUpdateStorageCalcStatistics"},
	{"StrusInspectVectorStorage", "src/strusInspectVectorStorage/strusInspectVectorStorage"},
	{"StrusAnalyzePhrase", "src/strusAnalyzePhrase/strusAnalyzePhrase"},
	{"StrusAlterMetaData", "src/strusAlterMetaData/strusAlterMetaData"},
	{"StrusUpdateStorage", "src/strusUpdateStorage/strusUpdateStorage"},
	{"StrusCreateVectorStorage", "src/strusCreateVectorStorage/strusCreateVectorStorage"},
	{"StrusCheckInsert", "src/strusInsert/strusCheckInsert"},
	{"StrusGenerateKeyMap", "src/strusInsert/strusGenerateKeyMap"},
	{"StrusInsert", "src/strusInsert/strusInsert"},
	{"StrusInspect", "src/strusInspect/strusInspect"},
	{"StrusSegment", "src/strusSegment/strusSegment"},
	{0,0}
};

static bool isSpace( char ch)
{
	return ch && (unsigned char)ch <= 32;
}

static bool isAlpha( char ch)
{
	if ((ch|32) >= 'a' && (ch|32) <= 'z') return true;
	if (ch == '_') return true;
	return false;
}

static bool isDigit( char ch)
{
	return (ch >= '0' && ch <= '9');
}

static bool isAlphaNum( char ch)
{
	return isAlpha(ch) || isDigit(ch);
}

static std::string parseLine( char const*& si)
{
	std::string rt;
	for (; *si && *si != '\n'; ++si)
	{
		rt.push_back( *si);
	}
	if (*si) ++si;
	return rt;
}

static void skipSpacesAndComments( char const*& si)
{
	while (*si)
	{
		while (isSpace( *si)) ++si;
		if (!*si)
		{
			return;
		}
		else if (*si == '#')
		{
			while (*si && *si != '\n') ++si;
			if ((*si) == '\n')
			{
				++si;
			}
		}
		else
		{
			return;
		}
	}
}

static int countLines( char const* si, char const* se)
{
	int rt = 0;
	for (; si != se; ++si)
	{
		if (*si == '\n')
		{
			++rt;
		}
	}
	return rt;
}

static std::string parseProgramName( char const*& si)
{
	std::string rt;
	skipSpacesAndComments( si);
	if (!isAlpha(*si))
	{
		throw std::runtime_error(_TXT("program name expected at start of a line"));
	}
	for (; isAlphaNum(*si); ++si)
	{
		rt.push_back( *si);
	}
	return rt;
}

static std::string parseString( char const*& si)
{
	std::string rt;
	char eb = *si++;
	for (; *si && *si != eb; ++si)
	{
		if (*si == '\\')
		{
			++si;
			if (!*si) throw std::runtime_error(_TXT("backslash at end of line"));
		}
		rt.push_back( *si);
	}
	if (!*si) throw std::runtime_error(_TXT("string not terminated"));
	++si;
	return rt;
}

static std::string parseToken( char const*& si)
{
	std::string rt;
	for (; *si && !isSpace( *si); ++si)
	{
		rt.push_back( *si);
	}
	return rt;
}

static std::string parseArgument( char const*& si)
{
	std::string tok;
	while (isSpace( *si)) ++si;
	if (!*si) return std::string();
	if (*si == '"' || *si == '\'')
	{
		tok = parseString( si);
	}
	else
	{
		tok = parseToken( si);
	}
	std::string rt;
	char const* ti = tok.c_str();
	char const* te = strchr( ti, '$');
	for (; te; te = strchr( ti, '$'))
	{
		rt.append( ti, te-ti);
		if (te[1] == 'T')
		{
			rt.append( g_testdir);
			ti = te + 2;
		}
		else if (te[1] == 'E')
		{
			rt.append( g_execdir);
			ti = te + 2;
		}
		else
		{
			throw std::runtime_error(_TXT("unknown substitution char in arguments"));
		}
	}
	rt.append( ti);
	return rt;
}

struct TestCommand
{
	enum {MaxNofArguments=30};
	int lineno;
	std::string prg;
	std::vector<std::string> argstrings;

	explicit TestCommand( int lineno_, const std::string& line)
		:lineno(lineno_)
	{
		char const* si = line.c_str();
		std::string pnam = parseProgramName( si);
		ProgramPath const* pi = g_prgpathmap;
		for (; pi->name; ++pi)
		{
			if (std::strcmp( pi->name, pnam.c_str()) == 0)
			{
				prg = g_bindir + strus::dirSeparator() + pi->path;
				break;
			}
		}
		if (!pi->name)
		{
			throw std::runtime_error(_TXT("program not defined"));
		}
		std::string arg = parseArgument( si);
		while (!arg.empty())
		{
			argstrings.push_back( arg);
			arg = parseArgument( si);
		}
	}
	TestCommand( const TestCommand& o)
		:lineno(o.lineno),prg(o.prg),argstrings(o.argstrings)
	{}

	std::string tostring() const
	{
		std::ostringstream out;
		out << prg;
		std::vector<std::string>::const_iterator ai = argstrings.begin(), ae = argstrings.end();
		for (; ai < ae; ++ai)
		{
			out << " \"" << *ai << "\"";
		}
		return out.str();
	}

	std::string exec() const
	{
		std::string rt;
		const char* argv[ MaxNofArguments+2];
		int argc = argstrings.size()+1;

		if (argc > MaxNofArguments) throw std::runtime_error(_TXT("too many arguments"));
		argv[0] = prg.c_str();
		for (int ai=1; ai<argc; ++ai)
		{
			argv[ ai] = argstrings[ai-1].c_str();
		}
		argv[ argc] = 0;

		int ec;
		if (g_env.empty())
		{
			ec = strus::execv_tostring( prg.c_str(), argv, rt);
		}
		else
		{
			ec = strus::execve_tostring( prg.c_str(), argv, g_env, rt);
		}
		if (ec)
		{
			char buf[ 2048];
			snprintf( buf, sizeof(buf), _TXT("error on line %d of test: %s"), lineno, ::strerror(ec));
			throw std::runtime_error( buf);
		}
		return rt;
	}
};

static std::string runTestCommands( const std::vector<TestCommand>& cmds)
{
	std::string rt;
	std::vector<TestCommand>::const_iterator ci = cmds.begin(), ce = cmds.end();
	for (; ci != ce; ++ci)
	{
		rt.append( ci->exec());
	}
	return rt;
}

static bool diffTestOutput( std::string& output, std::string& expected)
{
	std::string::const_iterator oi = output.begin(), oe = output.end();
	std::string::const_iterator ei = expected.begin(), ee = expected.end();
	for (; oi != oe && ei != ee; ++oi,++ei)
	{
		while (*oi == '\r') ++oi;
		while (*ei == '\r') ++ei;
		if (*oi != *ei) return false;
	}
	if (oi != oe || ei != ee) return false;
	return true;
}

int main( int argc, const char* argv[])
{
	int rt = 0;
	if (argc <= 1)
	{
		std::cerr << _TXT("no arguments passed to ") << argv[0] << std::endl;
		return 0;
	}
	if (argc <= 2)
	{
		std::cerr << _TXT("missing project directory (2nd argument) of ") << argv[0] << std::endl;
		return -1;
	}
	if (argc <= 3)
	{
		std::cerr << _TXT("missing project binary directory (3nd argument) of ") << argv[0] << std::endl;
		return -1;
	}
	try
	{
		g_testname = argv[1];
		g_maindir = argv[2];
		g_testdir = g_maindir + strus::dirSeparator() + "tests" + strus::dirSeparator() + "scripts" + strus::dirSeparator() + g_testname;
		g_bindir = argv[3];

		std::string mainexecdir = g_bindir + strus::dirSeparator() + "tests" + strus::dirSeparator() + "scripts" + strus::dirSeparator() + "exec";
		g_execdir = mainexecdir + strus::dirSeparator() + g_testname;

		std::cerr << _TXT("test name: ") << g_testname << std::endl;
		std::cerr << _TXT("test directory: ") << g_testdir << std::endl;
		std::cerr << _TXT("binary directory: ") << g_bindir << std::endl;
		std::cerr << _TXT("main execution directory: ") << mainexecdir << std::endl;
		std::cerr << _TXT("execution directory: ") << g_execdir << std::endl;
		std::cerr << _TXT("project directory: ") << g_maindir << std::endl;
		int argi=4;
		for (; argi < argc; ++argi)
		{
			const char* sep = std::strchr( argv[argi], '=');
			if (!sep) throw std::runtime_error( _TXT("expected list of environment variable assignments for the rest of arguments"));
			std::string envkey( argv[argi], sep - argv[argi]);
			std::string envval( sep+1);
			g_env[ envkey] = envval;
			std::cerr << _TXT("environment variable ") << envkey << "='" << envval << "'" << std::endl;
		}
		std::cerr << std::endl;

		std::string prgsrc;
		std::string prgfilename( g_testdir + strus::dirSeparator() + "RUN");
		unsigned int ec = strus::readFile( prgfilename, prgsrc);
		if (ec)
		{
			char msgbuf[ 2024];
			snprintf( msgbuf, sizeof(msgbuf), _TXT("error reading program file '%s': %s"), prgfilename.c_str(), strerror( ec));
			throw std::runtime_error( msgbuf);
		}
		std::vector<TestCommand> cmds;

		char const* si = prgsrc.c_str();
		skipSpacesAndComments( si);
		while (*si)
		{
			int lineno = countLines( prgsrc.c_str(), si)+1;
			std::string line( parseLine( si));
			if (!line.empty())
			{
				try
				{
					cmds.push_back( TestCommand( lineno, line));
				}
				catch (const std::runtime_error& err)
				{
					char msgbuf[ 2024];
					snprintf( msgbuf, sizeof(msgbuf), _TXT("error on line %u of program file '%s': %s"), lineno, prgfilename.c_str(), err.what());
					throw std::runtime_error( msgbuf);
				}
				std::cerr << cmds.back().tostring() << std::endl;
			}
			skipSpacesAndComments( si);
		}
		rt = strus::removeDirRecursive( g_execdir);
		if (rt)
		{
			std::cerr << _TXT("failed to remove old test execution directory: ") << ::strerror(rt) << std::endl;
			return rt;
		}
		rt = strus::createDir( mainexecdir, false);
		if (rt)
		{
			std::cerr << _TXT("failed to create main test execution directory: ") << ::strerror(rt) << std::endl;
			return rt;
		}
		rt = strus::createDir( g_execdir);
		if (rt)
		{
			std::cerr << _TXT("failed to create test execution directory: ") << ::strerror(rt) << std::endl;
			return rt;
		}
		rt = strus::changeDir( g_execdir);
		if (rt)
		{
			std::cerr << _TXT("failed to change to test execution directory: ") << ::strerror(rt) << std::endl;
			return rt;
		}
		std::string output = runTestCommands( cmds);
		rt = strus::changeDir( "..");
		if (rt)
		{
			std::cerr << _TXT("failed to change back from test execution directory: ") << ::strerror(rt) << std::endl;
			return rt;
		}
		std::string expected;
		rt = strus::readFile( g_testdir + strus::dirSeparator() + "EXP", expected);
		if (rt)
		{
			std::cerr << _TXT("failed to read EXP file of test: ") << ::strerror(rt) << std::endl;
		}
		else if (!diffTestOutput( output, expected))
		{
			rt = strus::writeFile( g_execdir + strus::dirSeparator() + "OUT", output);
			if (rt)
			{
				std::cerr << _TXT("failed to write OUT file of test: ") << ::strerror(rt) << std::endl;
			}
			rt = strus::writeFile( g_execdir + strus::dirSeparator() + "EXP", expected);
			if (rt)
			{
				std::cerr << _TXT("failed to write EXP file of test: ") << ::strerror(rt) << std::endl;
			}
			throw std::runtime_error("output differs from expected");
		}
		else
		{
			std::cerr << _TXT("done.") << std::endl;
		}
	}
	catch (const std::bad_alloc& err)
	{
		std::cerr << "out of memory executing test" << std::endl;
		return -2;
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << "error executing test: " << err.what() << std::endl;
		return -1;
	}
	return rt;
}


