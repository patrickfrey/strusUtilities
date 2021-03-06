/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/storage_objbuild.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/lib/storage_prgload_std.hpp"
#include "strus/lib/filelocator.hpp"
#include "strus/fileLocatorInterface.hpp"
#include "strus/reference.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryAnalyzerInstanceInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/local_ptr.hpp"
#include "private/programLoader.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <sys/time.h>

static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& config, strus::ErrorBufferInterface* errorhnd)
{
	std::string configstr( config);
	std::string dbname;
	(void)strus::extractStringFromConfigString( dbname, configstr, "database", errorhnd);
	if (errorhnd->hasError()) throw strus::runtime_error(_TXT("cannot evaluate database: %s"), errorhnd->fetchError());

	strus::local_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
	if (!dbi) throw std::runtime_error( _TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (!sti) throw std::runtime_error( _TXT("failed to get storage interface"));

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient), errorhnd);
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient), errorhnd);
}

static double getTimeStamp()
{
	struct timeval now;
	gettimeofday( &now, NULL);
	return (double)now.tv_usec / 1000000.0 + now.tv_sec;
}

static std::string getFileArg( const std::string& filearg, strus::ModuleLoaderInterface* moduleLoader)
{
	std::string programFileName = filearg;
	std::string programDir;
	int ec;

	if (strus::isExplicitPath( programFileName))
	{
		ec = strus::getParentPath( programFileName, programDir);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file directory from explicit path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		moduleLoader->addResourcePath( programDir);
	}
	else
	{
		std::string filedir;
		std::string filenam;
		ec = strus::getFileName( programFileName, filenam);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file name from absolute path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		ec = strus::getParentPath( programFileName, filedir);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file directory from absolute path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		programDir = filedir;
		programFileName = filenam;
		moduleLoader->addResourcePath( programDir);
	}
	return programFileName;
}

int main( int argc_, const char* argv_[])
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
		delete dbgtrace;
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	try
	{
		bool printUsageAndExit = false;

		strus::ProgramOptions opt(
				errorBuffer.get(), argc_, argv_, 18,
				"h,help", "v,version", "license",
				"G,debug:", "Q,quiet", "u,user:", "N,nofranks:", "I,firstrank:", "F,fileinput",
				"D,time", "m,module:", "M,moduledir:", "R,resourcedir:",
				"s,storage:", "S,configfile:", "r,rpc:", "T,trace:", "V,verbose");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}
		if (opt( "help")) printUsageAndExit = true;

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

		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw std::runtime_error( _TXT("failed to create module loader"));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
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
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--module", "--rpc");
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
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
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
			if (opt.nofargs() > 3)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 3)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusQuery [options] <anprg> <qeprg> <query>" << std::endl;
			std::cout << "<anprg>   = " << _TXT("path of query analyzer program") << std::endl;
			std::cout << "<qeprg>   = " << _TXT("path of query eval program") << std::endl;
			std::cout << "<query>    = " << _TXT("query string") << std::endl;
			std::cout << "             " << _TXT("file or '-' for stdin if option -F is specified)") << std::endl;
			std::cout << _TXT("description: Executes a query or a list of queries from a file.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << _TXT("    <CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration file as <FILENAME>") << std::endl;
			std::cout << "    " << _TXT("<FILENAME> is a file containing the configuration string") << std::endl;
			std::cout << "-u|--user <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use user name <NAME> for the query") << std::endl;
			std::cout << "-N|--nofranks <N>" << std::endl;
			std::cout << "    " << _TXT("Return maximum <N> ranks as query result") << std::endl;
			std::cout << "-I|--firstrank <N>" << std::endl;
			std::cout << "    " << _TXT("Return the result starting with rank <N> as first rank") << std::endl;
			std::cout << "-Q|--quiet" << std::endl;
			std::cout << "    " << _TXT("No output of results") << std::endl;
			std::cout << "-D|--time" << std::endl;
			std::cout << "    " << _TXT("Do print duration of pure query evaluation") << std::endl;
			std::cout << "-F|--fileinput" << std::endl;
			std::cout << "    " << _TXT("Interpret query argument as a file name containing the input") << std::endl;
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Print debug messages for component <COMP> to stderr") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			std::cout << "-V|--verbose" << std::endl;
			std::cout << "    " << _TXT("Verbose mode: Print some info like query analysis") << std::endl;
			return rt;
		}
		// Parse arguments:
		bool quiet = opt( "quiet");
		bool doMeasureDuration = opt( "time");
		bool verbose = opt("verbose");
		std::string username;
		std::size_t nofRanks = 20;
		std::size_t firstRank = 0;
		std::string storagecfg;
		bool queryIsFile = opt("fileinput");

		if (opt("user"))
		{
			username = opt[ "user"];
		}
		if (opt("nofranks"))
		{
			nofRanks = opt.asUint( "nofranks");
		}
		if (opt("firstrank"))
		{
			firstRank = opt.asUint( "firstrank");
		}
		if (opt("configfile"))
		{
			if (opt("storage")) throw strus::runtime_error(_TXT("conflicting configuration options specified: '%s' and '%s'"), "--storage", "--configfile");
			std::string configfile = opt[ "configfile"];
			int ec = strus::readFile( configfile, storagecfg);
			if (ec) throw strus::runtime_error(_TXT("failed to read configuration file %s (errno %u)"), configfile.c_str(), ec);

			std::string::iterator di = storagecfg.begin(), de = storagecfg.end();
			for (; di != de; ++di)
			{
				if ((unsigned char)*di < 32) *di = ' ';
			}
		}
		if (opt("storage"))
		{
			if (opt("configfile")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--storage", "--configfile");
			storagecfg = opt["storage"];
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
		std::string analyzerprg = getFileArg( opt[0], moduleLoader.get());
		std::string queryprg = getFileArg( opt[1], moduleLoader.get());
		std::string querystring = opt[2];

		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}

		// Create objects for query evaluation:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		strus::local_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw std::runtime_error( _TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw std::runtime_error( _TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create rpc analyzer object builder"));
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create rpc storage object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create analyzer object builder"));
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create storage object builder"));
		}

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* aproxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( aproxy);
			strus::StorageObjectBuilderInterface* sproxy = (*ti)->createProxy( storageBuilder.get());
			storageBuilder.release();
			storageBuilder.reset( sproxy);
		}

		// Create objects:
		strus::local_ptr<strus::StorageClientInterface>
			storage( strus::createStorageClient( storageBuilder.get(), errorBuffer.get(), storagecfg));
		if (!storage.get()) throw strus::runtime_error(_TXT("failed to create storage client: %s"), errorBuffer->fetchError());

		strus::local_ptr<strus::QueryAnalyzerInstanceInterface> analyzer( analyzerBuilder->createQueryAnalyzer());
		if (!analyzer.get()) throw strus::runtime_error(_TXT("failed to create query analyzer: %s"), errorBuffer->fetchError());

		strus::local_ptr<strus::QueryEvalInterface> qeval( storageBuilder->createQueryEval());
		if (!qeval.get()) throw strus::runtime_error(_TXT("failed to create query evaluation interface: %s"), errorBuffer->fetchError());

		const strus::QueryProcessorInterface* qproc = storageBuilder->getQueryProcessor();
		if (!qproc) throw strus::runtime_error(_TXT("failed to get query processor: %s"), errorBuffer->fetchError());
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor: %s"), errorBuffer->fetchError());
		if (errorBuffer->hasError()) throw strus::runtime_error(_TXT("error in initialization: %s"), errorBuffer->fetchError());

		// Load query analyzer program:
		if (!strus::load_QueryAnalyzer_programfile_std( analyzer.get(), textproc, analyzerprg, errorBuffer.get()))
		{
			throw strus::runtime_error(_TXT("failed to load query analyzer program: %s"), errorBuffer->fetchError());
		}
		// Load query evaluation program:
		std::string qevalProgramSource;
		std::string queryprgpath = strus::isExplicitPath( queryprg) ? queryprg : textproc->getResourceFilePath( queryprg);

		int ec = strus::readFile( queryprgpath, qevalProgramSource);
		if (ec) throw strus::runtime_error(_TXT("failed to load query eval program %s (errno %u)"), queryprg.c_str(), ec);

		if (!strus::load_queryeval_program( *qeval, analyzer->queryTermTypes(), qproc, qevalProgramSource, errorBuffer.get()))
		{
			throw strus::runtime_error(_TXT("failed to load query evaluation program: %s"), errorBuffer->fetchError());
		}

		// Load query:
		if (queryIsFile)
		{
			std::string qs;
			if (querystring == "-")
			{
				ec = strus::readStdin( qs);
				if (ec) throw strus::runtime_error( _TXT("failed to read query from stdin (errno %u)"), ec);
			}
			else
			{
				ec = strus::readFile( querystring, qs);
				if (ec) throw strus::runtime_error(_TXT("failed to read query from file %s (errno %u)"), querystring.c_str(), ec);
			}
			querystring = qs;
		}
		unsigned int nofQueries = 0;
		double startTime = 0.0;
		if (doMeasureDuration)
		{
			startTime = getTimeStamp();
		}
		std::string::const_iterator si = querystring.begin(), se = querystring.end();
		std::string qs;
		while (strus::scanNextProgram( qs, si, se, errorBuffer.get()))
		{
			++nofQueries;
			strus::local_ptr<strus::QueryInterface> query( qeval->createQuery( storage.get()));
			if (!query.get()) throw strus::runtime_error(_TXT("failed to create query object: %s"), errorBuffer->fetchError());

			std::string selectionFeatureSet;
			std::string weightingFeatureSet;
			std::vector<std::string> selfset = qeval->getSelectionFeatureSets();
			if (!selfset.empty()) selectionFeatureSet = selfset[0];
			std::vector<std::string> weightset = qeval->getWeightingFeatureSets();
			if (!weightset.empty()) weightingFeatureSet = weightset[0];

			if (!strus::loadQuery( *query, analyzer.get(), selectionFeatureSet, weightingFeatureSet, qproc, qs, errorBuffer.get()))
			{
				throw strus::runtime_error(_TXT("failed to load query from source: %s"), errorBuffer->fetchError());
			}
			if (!username.empty())
			{
				query->addAccess( username);
			}
			if (verbose)
			{
				std::cerr << "Query:" << std::endl;
				std::cerr << query->view().tostring() << std::endl;
			}
			strus::QueryResult result = query->evaluate( firstRank, nofRanks);
			if (!quiet)
			{
				std::cout << strus::string_format( _TXT("evaluated till pass %u, got %u ranks (%u without restrictions applied):"), result.evaluationPass(), result.nofRanked(), result.nofVisited()) << std::endl;
				std::cout << strus::string_format( _TXT("ranked list (starting with rank %u, maximum %u results):"), (unsigned int)firstRank, (unsigned int)nofRanks) << std::endl;
			}
			std::vector<strus::ResultDocument>::const_iterator wi = result.ranks().begin(), we = result.ranks().end();
			if (!quiet)
			{
				for (int widx=1; wi != we; ++wi,++widx)
				{
					if (wi->field().defined())
					{
						std::cout << strus::string_format( _TXT( "[%u] field [%d,%d] score %f"), widx, (int)wi->field().start(), (int)wi->field().end(), wi->weight()) << std::endl;
					}
					else
					{
						std::cout << strus::string_format( _TXT( "[%u] score %f"), widx, wi->weight()) << std::endl;
					}
					std::vector<strus::SummaryElement>::const_iterator
						ai = wi->summaryElements().begin(),
						ae = wi->summaryElements().end();
					for (; ai != ae; ++ai)
					{
						std::cout << "\t" << ai->name();
						if (ai->index() >= 0)
						{
							std::cout << "[" << ai->index() << "]";
						}
						std::cout << " = '" << ai->value() << "'";
						std::cout << " " << ai->weight() << std::endl;
					}
				}
			}
		}
		if (doMeasureDuration)
		{
			double endTime = getTimeStamp();
			double duration = endTime - startTime;
			std::cerr << strus::string_format( _TXT("evaluated %u queries in %.4f seconds"), nofQueries, duration) << std::endl;
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in command line query: %s"), errorBuffer->fetchError());
		}
		std::cerr << _TXT("done.") << std::endl;
		if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
		{
			std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
		}
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
		return -2;
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
	if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
	{
		std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
	}
	return -1;
}


