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
#include "strus/lib/module.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/objectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/reference.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "strus/programLoader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>

struct TermOrder
{
	bool operator()( const strus::analyzer::Term& aa, const strus::analyzer::Term& bb)
	{
		if (aa.pos() != bb.pos()) return (aa.pos() < bb.pos());
		int cmp;
		cmp = aa.type().compare( bb.type());
		if (cmp != 0) return (cmp < 0);
		cmp = aa.value().compare( bb.value());
		if (cmp != 0) return (cmp < 0);
		return false;
	}
};

int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 9,
				"h,help", "v,version", "t,tokenizer:", "n,normalizer:",
				"m,module:", "M,moduledir:", "q,quot:", "p,plain",
				"R,resourcedir:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus analyzer version " << STRUS_ANALYZER_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 1)
			{
				std::cerr << "ERROR too few arguments" << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader());
		if (opt("moduledir"))
		{
			std::vector<std::string> modirlist( opt.list("moduledir"));
			std::vector<std::string>::const_iterator mi = modirlist.begin(), me = modirlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->addModulePath( *mi);
			}
			moduleLoader->addSystemModulePath();
		}
		if (opt("module"))
		{
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->loadModule( *mi);
			}
		}
		const strus::ObjectBuilderInterface& builder = moduleLoader->builder();

		if (printUsageAndExit)
		{
			std::cerr << "usage: strusAnalyze [options] <phrasepath>" << std::endl;
			std::cerr << "<phrasepath> = path to phrase to analyze ('-' for stdin)" << std::endl;
			std::cerr << "description: tokenizes and normalizes a text segment" << std::endl;
			std::cerr << "             and prints the result to stdout." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "   Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			std::cerr << "-R|--resourcedir <DIR>" << std::endl;
			std::cerr << "    Search resource files for analyzer first in <DIR>" << std::endl;
			std::cerr << "-t|--tokenizer <CALL>" << std::endl;
			std::cerr << "    Use the tokenizer <CALL> (default 'content')" << std::endl;
			std::cerr << "-n|--normalizer <CALL>" << std::endl;
			std::cerr << "    Use the normalizer <CALL> (default 'orig')" << std::endl;
			std::cerr << "-q|--quot <STR>" << std::endl;
			std::cerr << "    Use the string <STR> as quote for the result (default \"\'\")" << std::endl;
			std::cerr << "-p|--plain" << std::endl;
			std::cerr << "    Do not print position and define default quotes as empty" << std::endl;
			return rt;
		}
		std::string resultQuot = "'";
		bool resultPlain = false;
		if (opt( "plain"))
		{
			resultPlain = true;
			resultQuot.clear();
		}
		if (opt( "quot"))
		{
			resultQuot = opt[ "quot"];
		}
		std::string docpath = opt[0];
		std::string tokenizer( "content");
		if (opt( "tokenizer"))
		{
			tokenizer = opt[ "tokenizer"];
		}
		std::string normalizer( "orig");
		if (opt( "normalizer"))
		{
			normalizer = opt[ "normalizer"];
		}
		// Set paths for locating resources:
		if (opt("resourcedir"))
		{
			std::vector<std::string> pathlist( opt.list("resourcedir"));
			std::vector<std::string>::const_iterator
				pi = pathlist.begin(), pe = pathlist.end();
			for (; pi != pe; ++pi)
			{
				moduleLoader->addResourcePath( *pi);
			}
		}

		// Create objects for analyzer:
		std::auto_ptr<strus::QueryAnalyzerInterface>
			analyzer( builder.createQueryAnalyzer());

		// Create phrase type (tokenizer and normalizer):
		std::vector<strus::NormalizerConfig> normalizerConfig(
			strus::parseNormalizerConfig( normalizer));

		strus::TokenizerConfig tokenizerConfig(
			strus::parseTokenizerConfig( tokenizer));

		std::string phraseType;
		analyzer->definePhraseType( phraseType, "",  tokenizerConfig, normalizerConfig);

		// Load the phrase:
		std::string phrase;
		if (docpath == "-")
		{
			unsigned int ec = strus::readStdin( phrase);
			if (ec)
			{
				std::ostringstream msg;
				msg << "errno " << ec;
				throw std::runtime_error( std::string( "error reading input from stdin: ") + msg.str() + "'");
			}
		}
		else
		{
			unsigned int ec = strus::readFile( docpath, phrase);
			if (ec)
			{
				std::ostringstream msg;
				msg << "errno " << ec;
				throw std::runtime_error( std::string( "error reading input file '") + docpath + "': " + msg.str() + "'");
			}
		}

		// Analyze the phrase and print the result:
		std::vector<strus::analyzer::Term> terms
			= analyzer->analyzePhrase( phraseType, phrase);

		std::sort( terms.begin(), terms.end(), TermOrder());

		std::vector<strus::analyzer::Term>::const_iterator
			ti = terms.begin(), te = terms.end();

		for (; ti != te; ++ti)
		{
			if (!resultPlain)
			{
				std::cout << ti->pos() << " ";
			}
			std::cout << resultQuot << ti->value() << resultQuot << std::endl;
		}
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		std::cerr << "ERROR " << e.what() << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cerr << "EXCEPTION " << e.what() << std::endl;
	}
	return -1;
}

