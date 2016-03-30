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
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/private/fileio.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionStorage.hpp"
#include "private/version.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <stdexcept>

#undef STRUS_LOWLEVEL_DEBUG

static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& dbcfg, strus::ErrorBufferInterface* errorhnd)
{
	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbcfg);
	if (!dbi) throw strus::runtime_error(_TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (!sti) throw strus::runtime_error(_TXT("failed to get storage interface"));

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


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	std::auto_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 12,
				"h,help", "Q,quiet", "u,user:", "N,nofranks:", "I,firstrank:",
				"T,time", "v,version", "m,module:", "M,moduledir:", "R,resourcedir:",
				"s,storage:", "r,rpc:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
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
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));
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

		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusQuery [options] <anprg> <qeprg> <query>" << std::endl;
			std::cout << "<anprg>   = " << _TXT("path of query analyzer program") << std::endl;
			std::cout << "<qeprg>   = " << _TXT("path of query eval program") << std::endl;
			std::cout << "<query>   = " << _TXT("path of query or '-' for stdin") << std::endl;
			std::cout << _TXT("description: Executes a query or a list of queries from a file.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << _TXT("    <CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
			std::cout << "-u|--user <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use user name <NAME> for the query") << std::endl;
			std::cout << "-N|--nofranks <N>" << std::endl;
			std::cout << "    " << _TXT("Return maximum <N> ranks as query result") << std::endl;
			std::cout << "-I|--firstrank <N>" << std::endl;
			std::cout << "    " << _TXT("Return the result starting with rank <N> as first rank") << std::endl;
			std::cout << "-Q|--quiet" << std::endl;
			std::cout << "    " << _TXT("No output of results") << std::endl;
			std::cout << "-T|--time" << std::endl;
			std::cout << "    " << _TXT("Do print duration of pure query evaluation") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			return rt;
		}
		bool quiet = opt( "quiet");
		bool measureDuration = opt( "time");
		std::string username;
		std::size_t nofRanks = 20;
		std::size_t firstRank = 0;
		std::string storagecfg;
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
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
			storagecfg = opt["storage"];
		}
		std::string analyzerprg = opt[0];
		std::string queryprg = opt[1];
		std::string querypath = opt[2];

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
		std::string resourcepath;
		if (0!=strus::getParentPath( analyzerprg, resourcepath))
		{
			throw strus::runtime_error( _TXT("failed to evaluate resource path"));
		}
		if (!resourcepath.empty())
		{
			moduleLoader->addResourcePath( resourcepath);
		}

		// Create objects for query evaluation:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error(_TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error(_TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create rpc analyzer object builder"));
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create rpc storage object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create analyzer object builder"));
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));
		}
		strus::utils::ScopedPtr<strus::StorageClientInterface>
			storage( storageBuilder->createStorageClient( storagecfg));
		if (!storage.get()) throw strus::runtime_error(_TXT("failed to create storage client"));

		strus::utils::ScopedPtr<strus::QueryAnalyzerInterface>
			analyzer( analyzerBuilder->createQueryAnalyzer());
		if (!analyzer.get()) throw strus::runtime_error(_TXT("failed to create query analyzer"));

		strus::utils::ScopedPtr<strus::QueryEvalInterface>
			qeval( storageBuilder->createQueryEval());
		if (!qeval.get()) throw strus::runtime_error(_TXT("failed to create query evaluation interface"));

		const strus::QueryProcessorInterface* qproc = storageBuilder->getQueryProcessor();
		if (!qproc) throw strus::runtime_error(_TXT("failed to get query processor"));
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor"));

		// Load query analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec) throw strus::runtime_error(_TXT("failed to load analyzer program %s (errno %u)"), analyzerprg.c_str(), ec);

		if (!strus::loadQueryAnalyzerProgram( *analyzer, textproc, analyzerProgramSource, errorBuffer.get()))
		{
			throw strus::runtime_error(_TXT("failed to load query analyzer program"));
		}
		// Load query evaluation program:
		std::string qevalProgramSource;
		ec = strus::readFile( queryprg, qevalProgramSource);
		if (ec) throw strus::runtime_error(_TXT("failed to load query eval program %s (errno %u)"), queryprg.c_str(), ec);

		if (!strus::loadQueryEvalProgram( *qeval, qproc, qevalProgramSource, errorBuffer.get()))
		{
			throw strus::runtime_error(_TXT("failed to load query evaluation program"));
		}

		// Load query:
		std::string querystring;
		if (querypath == "-")
		{
			ec = strus::readStdin( querystring);
			if (ec) throw strus::runtime_error( _TXT("failed to read query string from stdin (errno %u)"), ec);
		}
		else
		{
			ec = strus::readFile( querypath, querystring);
			if (ec) throw strus::runtime_error(_TXT("failed to read query string from file %s (errno %u)"), querypath.c_str(), ec);
		}

		unsigned int nofQueries = 0;
		double startTime = 0.0;
		if (measureDuration)
		{
			startTime = getTimeStamp();
		}
		std::string::const_iterator si = querystring.begin(), se = querystring.end();
		std::string qs;
		while (strus::scanNextProgram( qs, si, se, errorBuffer.get()))
		{
			++nofQueries;
			std::auto_ptr<strus::QueryInterface> query(
				qeval->createQuery( storage.get()));
			if (!query.get()) throw strus::runtime_error(_TXT("failed to create query object"));

			if (!strus::loadQuery( *query, analyzer.get(), qproc, qs, errorBuffer.get()))
			{
				throw strus::runtime_error(_TXT("failed to load query from source"));
			}

			query->setMaxNofRanks( nofRanks);
			query->setMinRank( firstRank);
			if (!username.empty())
			{
				query->addUserName( username);
			}
			strus::QueryResult result = query->evaluate();

			if (!quiet)
			{
				std::cout << strus::utils::string_sprintf( _TXT("evaluated till pass %u, got %u ranks (%u without restrictions applied):"), result.evaluationPass(), result.nofDocumentsRanked(), result.nofDocumentsVisited()) << std::endl;
				std::cout << strus::utils::string_sprintf( _TXT("ranked list (starting with rank %u, maximum %u results):"), firstRank, nofRanks) << std::endl;
			}
			std::vector<strus::ResultDocument>::const_iterator wi = result.ranks().begin(), we = result.ranks().end();
			if (!quiet)
			{
				for (int widx=1; wi != we; ++wi,++widx)
				{
					std::cout << strus::utils::string_sprintf( _TXT( "[%u] %u score %f"), widx, wi->docno(), wi->weight()) << std::endl;
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
		if (measureDuration)
		{
			double endTime = getTimeStamp();
			double duration = endTime - startTime;
			std::cerr << strus::utils::string_sprintf( _TXT("evaluated %u queries in %.4f seconds"), nofQueries, duration) << std::endl;
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in command line query"));
		}
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


