/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryAnalyzerInstanceInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/reference.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/programOptions.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "strus/programLoader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <cerrno>
#include <cstdio>
#include <memory>


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::DebugTraceInterface* dbgtrace = strus::createDebugTrace_standard( 2);
	if (!dbgtrace)
	{
		std::cerr << _TXT("failed to create debug trace") << std::endl;
		return -1;
	}
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2, dbgtrace/*passed with ownership*/));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	try
	{
		bool printUsageAndExit = false;
		strus::ProgramOptions opt(
				errorBuffer.get(), argc, argv, 13,
				"h,help", "v,version", "license", "G,debug:", "t,tokenizer:", "n,normalizer:",
				"m,module:", "M,moduledir:", "q,quot:", "P,plain", "F,fileinput",
				"R,resourcedir:", "T,trace:");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}
		if (opt( "help")) printUsageAndExit = true;

		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw std::runtime_error( _TXT("failed to create module loader"));

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
				if (!moduleLoader->loadModule( *mi))
				{
					throw strus::runtime_error(_TXT("error failed to load module %s"), mi->c_str());
				}
			}
		}
		if (opt("license"))
		{
			std::vector<std::string> licenses_3rdParty = moduleLoader->get3rdPartyLicenseTexts();
			std::vector<std::string>::const_iterator ti = licenses_3rdParty.begin(), te = licenses_3rdParty.end();
			if (ti != te) std::cout << _TXT("3rd party licenses:") << std::endl;
			for (; ti != te; ++ti)
			{
				std::cout << *ti << std::endl;
			}
			std::cout << std::endl;
			if (!printUsageAndExit) return 0;
		}
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
			std::vector<std::string> versions_3rdParty = moduleLoader->get3rdPartyVersionTexts();
			std::vector<std::string>::const_iterator vi = versions_3rdParty.begin(), ve = versions_3rdParty.end();
			if (vi != ve) std::cout << _TXT("3rd party versions:") << std::endl;
			for (; vi != ve; ++vi)
			{
				std::cout << *vi << std::endl;
			}
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusAnalyze [options] <phrase>" << std::endl;
			std::cout << "<phrase> =   " << _TXT("path to phrase to analyze") << std::endl;
			std::cout << "             " << _TXT("file or '-' for stdin if option -F is specified)") << std::endl;
			std::cout << "description: " << _TXT("tokenizes and normalizes a text segment") << std::endl;
			std::cout << "             " << _TXT("and prints the result to stdout.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "   " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Issue debug messages for component <COMP> to stderr") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-t|--tokenizer <CALL>" << std::endl;
			std::cout << "    " << _TXT("Use the tokenizer <CALL> (default 'content')") << std::endl;
			std::cout << "-n|--normalizer <CALL>" << std::endl;
			std::cout << "    " << _TXT("Use the normalizer <CALL> (default 'orig')") << std::endl;
			std::cout << "-q|--quot <STR>" << std::endl;
			std::cout << "    " << _TXT("Use the string <STR> as quote for the result (default \"\'\")") << std::endl;
			std::cout << "-P|--plain" << std::endl;
			std::cout << "    " << _TXT("Print results without quotes and without an end of line for each result") << std::endl;
			std::cout << "-F|--fileinput" << std::endl;
			std::cout << "    " << _TXT("Interpret phrase argument as a file name containing the input") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			return rt;
		}
		// Declare trace proxy objects:
		typedef strus::Reference<strus::TraceProxy> TraceReference;
		std::vector<TraceReference> trace;
		if (opt("trace"))
		{
			std::vector<std::string> tracecfglist( opt.list("trace"));
			std::vector<std::string>::const_iterator ti = tracecfglist.begin(), te = tracecfglist.end();
			for (; ti != te; ++ti)
			{
				trace.push_back( new strus::TraceProxy( moduleLoader.get(), *ti, errorBuffer.get()));
			}
		}
		// Enable debugging selected with option 'debug':
		{
			std::vector<std::string> dbglist = opt.list( "debug");
			std::vector<std::string>::const_iterator gi = dbglist.begin(), ge = dbglist.end();
			for (; gi != ge; ++gi)
			{
				if (!dbgtrace->enable( *gi))
				{
					throw strus::runtime_error(_TXT("failed to enable debug '%s'"), gi->c_str());
				}
			}
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
		std::string phrasestring = opt[0];
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

		// Create root object for analyzer:
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface>
			analyzerBuilder( moduleLoader->createAnalyzerObjectBuilder());
		if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create analyzer object builder"));

		// Create proxy objects if tracing enabled:
		{
			std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
			for (; ti != te; ++ti)
			{
				strus::AnalyzerObjectBuilderInterface* proxy = (*ti)->createProxy( analyzerBuilder.get());
				analyzerBuilder.release();
				analyzerBuilder.reset( proxy);
			}
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}

		// Create objects for analyzer:
		strus::local_ptr<strus::QueryAnalyzerInstanceInterface>
			analyzer( analyzerBuilder->createQueryAnalyzer());
		if (!analyzer.get()) throw std::runtime_error( _TXT("failed to create analyzer"));
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw std::runtime_error( _TXT("failed to get text processor"));

		// Create phrase type (tokenizer and normalizer):
		if (!loadPhraseAnalyzer( *analyzer, textproc, normalizer, tokenizer, errorBuffer.get()))
		{
			throw std::runtime_error( _TXT("failed to load analyze phrase analyzer"));
		}

		// Load the phrase:
		bool queryIsFile = opt("fileinput");
		if (queryIsFile)
		{
			std::string ps;
			if (phrasestring == "-")
			{
				unsigned int ec = strus::readStdin( ps);
				if (ec) throw strus::runtime_error( _TXT("failed to read query from stdin (errno %u)"), ec);
			}
			else
			{
				unsigned int ec = strus::readFile( phrasestring, ps);
				if (ec) throw strus::runtime_error(_TXT("failed to read query from file %s (errno %u)"), phrasestring.c_str(), ec);
			}
			phrasestring = ps;
		}

		// Analyze the phrase and print the result:
		strus::local_ptr<strus::QueryAnalyzerContextInterface> qryanactx( analyzer->createContext());
		if (!qryanactx.get()) throw std::runtime_error( _TXT("failed to create query analyzer context"));
	
		qryanactx->putField( 1, "", phrasestring);
		strus::analyzer::QueryTermExpression qry = qryanactx->analyze();
		if (errorBuffer->hasError()) throw std::runtime_error( _TXT("query analysis failed"));
		std::vector<strus::analyzer::QueryTerm> terms;
		std::vector<strus::analyzer::QueryTermExpression::Instruction>::const_iterator
			ii = qry.instructions().begin(), ie = qry.instructions().end();
		for (int iidx=0; ii != ie; ++ii,++iidx)
		{
			if (ii->opCode() == strus::analyzer::QueryTermExpression::Instruction::Term)
			{
				const strus::analyzer::QueryTerm& term = qry.term( ii->idx());
				if (resultPlain)
				{
					if (iidx) std::cout << " ";
					std::cout << term.value();
				}
				else
				{
					std::cout << resultQuot << term.value() << resultQuot << std::endl;
				}
			}
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in analyze phrase"));
		}
		if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
		{
			std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
		}
		std::cerr << _TXT("done.") << std::endl;
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer->fetchError();
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << e.what() << ": " << errormsg << std::endl;
		}
		else
		{
			std::cerr << _TXT("ERROR ") << e.what() << std::endl;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << _TXT("EXCEPTION ") << e.what() << std::endl;
	}
	return -1;
}


