/*
 * Copyright (c) 2016 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Program running pattern matching with a rule file on an input file
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/markup_std.hpp"
#include "strus/lib/pattern_serialize.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/constants.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/segmenterInstanceInterface.hpp"
#include "strus/patternMatcherInstanceInterface.hpp"
#include "strus/patternMatcherInterface.hpp"
#include "strus/tokenMarkupInstanceInterface.hpp"
#include "strus/patternLexerInstanceInterface.hpp"
#include "strus/patternLexerInterface.hpp"
#include "strus/patternTermFeederInterface.hpp"
#include "strus/patternTermFeederInstanceInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/reference.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/programOptions.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <memory>

#undef STRUS_LOWLEVEL_DEBUG

static strus::ErrorBufferInterface* g_errorBuffer = 0;


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	g_errorBuffer = errorBuffer.get();

	bool printUsageAndExit = false;
	try
	{
		strus::ProgramOptions opt(
				errorBuffer.get(), argc, argv, 10,
				"h,help", "v,version", "license",
				"o,output:", "F,feeder",
				"M,moduledir:", "m,module:", "r,rpc:",
				"R,resourcedir:", "T,trace:");

		if (opt( "help")) printUsageAndExit = true;
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("error too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("error too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		strus::local_ptr<strus::ModuleLoaderInterface>
				moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error( "%s", _TXT("failed to create module loader"));

		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
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
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--module", "--rpc");
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
#if STRUS_PATTERN_STD_ENABLED
		if (!moduleLoader->loadModule( strus::Constants::standard_pattern_matcher_module()))
		{
			std::cerr << _TXT("failed to load module ") << "'" << strus::Constants::standard_pattern_matcher_module() << "': " << g_errorBuffer->fetchError() << std::endl;
		}
#endif
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
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
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
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusPatternSerialize [options] <program>" << std::endl;
			std::cout << _TXT("description: Loads a pattern matcher program source in file <program>") << std::endl;
			std::cout << _TXT("    and outputs its serialization.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "    " << _TXT("The module modstrus_analyzer_pattern is implicitely defined") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-F|--feeder" << std::endl;
			std::cout << "    " << _TXT("Assume program with feeder (post analyzer processing)") << std::endl;
			std::cout << "-o|--output <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write output to file <FILE>.") << std::endl;
			std::cout << "    " << _TXT("Do text output to stdout if not specified.") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string programfile = opt[ 0];
		std::string outputfile;
		bool use_feeder = opt( "feeder");
		if (opt( "output"))
		{
			outputfile = opt[ "output"];
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

		// Set paths for locating resources:
		if (opt("resourcedir"))
		{
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--resourcedir", "--rpc");
			std::vector<std::string> pathlist( opt.list("resourcedir"));
			std::vector<std::string>::const_iterator
				pi = pathlist.begin(), pe = pathlist.end();
			for (; pi != pe; ++pi)
			{
				moduleLoader->addResourcePath( *pi);
			}
		}
		// Create objects for analyzer:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;

		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc analyzer object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create analyzer object builder"));
		}

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* proxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( proxy);
		}
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("error in initialization"));
		}
		// Create objects:
		strus::PatternSerializerType serializerType = use_feeder
							? strus::PatternMatcherWithFeeder
							: strus::PatternMatcherWithLexer;
		strus::local_ptr<strus::PatternSerializer> serializer;
		if (outputfile.empty())
		{
			serializer.reset( strus::createPatternSerializerText( std::cout, serializerType, g_errorBuffer));
		}
		else
		{
			serializer.reset( strus::createPatternSerializer( outputfile, serializerType, g_errorBuffer));
		}
		if (!serializer.get()) throw strus::runtime_error( "%s", _TXT("failed to create serializer"));

		std::cerr << "serialize program ..." << std::endl;
		std::string programsrc;
		unsigned int ec = strus::readFile( programfile, programsrc);
		if (ec) throw strus::runtime_error(_TXT("error (%u) reading rule file %s: %s"), ec, programfile.c_str(), ::strerror(ec));
		std::vector<std::string> warnings;
		bool result = (use_feeder)
			? strus::loadPatternMatcherProgramWithFeeder( serializer->feeder(), serializer->matcher(), programsrc, g_errorBuffer, warnings)
			: strus::loadPatternMatcherProgramWithLexer( serializer->lexer(), serializer->matcher(), programsrc, g_errorBuffer, warnings);
		if (!result) throw strus::runtime_error( "%s", _TXT("failed to load program"));

		std::vector<std::string>::const_iterator wi = warnings.begin(), we = warnings.end();
		for (; wi != we; ++wi)
		{
			std::cerr << "warning: " << *we << std::endl;
		}
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("uncaught error in pattern serialize"));
		}
		serializer->close();
		std::cerr << _TXT("OK done") << std::endl;
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


