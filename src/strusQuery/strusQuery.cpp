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

static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& dbcfg)
{
	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbcfg);
	const strus::StorageInterface* sti = storageBuilder->getStorage();

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient));
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient));
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
	strus::ErrorBufferInterface* errorBuffer = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		errorBuffer = strus::createErrorBuffer_standard( stderr, 2);
		if (!errorBuffer) throw strus::runtime_error( _TXT("failed to create error buffer"));

		opt = strus::ProgramOptions(
				argc_, argv_, 13,
				"h,help", "Q,quiet", "u,user:", "n,nofranks:", "i,firstrank:",
				"g,globalstats:", "t,time", "v,version", "m,module:",
				"M,moduledir:", "R,resourcedir:", "s,storage:", "r,rpc:");
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
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer));
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
				moduleLoader->loadModule( *mi);
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
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""));
			}
			std::cout << "-u|--user <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use user name <NAME> for the query") << std::endl;
			std::cout << "-n|--nofranks <N>" << std::endl;
			std::cout << "    " << _TXT("Return maximum <N> ranks as query result") << std::endl;
			std::cout << "-i|--firstrank <N>" << std::endl;
			std::cout << "    " << _TXT("Return the result starting with rank <N> as first rank") << std::endl;
			std::cout << "-Q|--quiet" << std::endl;
			std::cout << "    " << _TXT("No output of results") << std::endl;
			std::cout << "-g|--globalstats <FILE>" << std::endl;
			std::cout << "    " << _TXT("Load global statistics of peers from file <FILE>") << std::endl;
			std::cout << "-t|--time" << std::endl;
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
		moduleLoader->addResourcePath( strus::getParentPath( analyzerprg));

		// Create objects for query evaluation:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer));
			(void)messaging.release();
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
		}
		else if (opt("storage"))
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
		}
		else
		{
			throw strus::runtime_error( _TXT("neither storage (option --storage) nor rpc proxy (option --rpc) specified"));
		}
		strus::utils::ScopedPtr<strus::StorageClientInterface>
			storage( storageBuilder->createStorageClient( storagecfg));

		strus::utils::ScopedPtr<strus::QueryAnalyzerInterface>
			analyzer( analyzerBuilder->createQueryAnalyzer());

		strus::utils::ScopedPtr<strus::QueryEvalInterface>
			qeval( storageBuilder->createQueryEval());

		const strus::QueryProcessorInterface* qproc = storageBuilder->getQueryProcessor();
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();

		// Load query analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec) throw strus::runtime_error(_TXT("failed to load analyzer program %s (errno %u)"), analyzerprg.c_str(), ec);

		strus::loadQueryAnalyzerProgram( *analyzer, textproc, analyzerProgramSource);

		// Load query evaluation program:
		std::string qevalProgramSource;
		ec = strus::readFile( queryprg, qevalProgramSource);
		if (ec) throw strus::runtime_error(_TXT("failed to load query eval program %s (errno %u)"), queryprg.c_str(), ec);

		strus::loadQueryEvalProgram( *qeval, qproc, qevalProgramSource);

		// Load global statistics from file if specified:
		if (opt("globalstats"))
		{
			std::vector<std::string> pathlist( opt.list("globalstats"));
			std::string filename = opt[ "globalstats"];
			try 
			{
				std::string content;
				unsigned int ec = strus::readFile( filename, content);
				if (ec) throw strus::runtime_error(_TXT("error reading global statistics file %s (errno %u)"), filename.c_str(), ec);

				storage->pushPeerMessage( content.c_str(), content.size());
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error(_TXT("failed to read global statistics file %s: %s"), filename.c_str(), err.what());
			}
		}

		// Load query:
		std::string querystring;
		if (querypath == "-")
		{
			ec = strus::readStdin( querystring);
			if (ec)
			{
				std::cerr << _TXT("failed to read query string from stdin") << std::endl;
				return 3;
			}
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
		while (strus::scanNextProgram( qs, si, se))
		{
			++nofQueries;
			std::auto_ptr<strus::QueryInterface> query(
				qeval->createQuery( storage.get()));

			strus::loadQuery( *query, analyzer.get(), qproc, qs);

			query->setMaxNofRanks( nofRanks);
			query->setMinRank( firstRank);
			if (!username.empty())
			{
				query->addUserName( username);
			}
			std::vector<strus::ResultDocument> ranklist = query->evaluate();

			if (!quiet) std::cout << strus::utils::string_sprintf( _TXT("ranked list (starting with rank %u, maximum %u results):"), firstRank, nofRanks) << std::endl;
			std::vector<strus::ResultDocument>::const_iterator wi = ranklist.begin(), we = ranklist.end();
			for (int widx=1; wi != we; ++wi,++widx)
			{
				if (!quiet) std::cout << strus::utils::string_sprintf( _TXT( "[%u] %u score %f"), widx, wi->docno(), wi->weight()) << std::endl;
				std::vector<strus::ResultDocument::Attribute>::const_iterator ai = wi->attributes().begin(), ae = wi->attributes().end();
				for (; ai != ae; ++ai)
				{
					if (!quiet) std::cout << "\t" << ai->name() << " (" << ai->value() << ")" << std::endl;
				}
			}
		}
		if (measureDuration)
		{
			double endTime = getTimeStamp();
			double duration = endTime - startTime;
			std::cerr << strus::utils::string_sprintf( _TXT("evaluated %u queries in %.4f seconds"), nofQueries, duration) << std::endl;
		}
		delete errorBuffer;
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer?errorBuffer->fetchError():0;
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << errormsg << ": " << e.what() << std::endl;
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
	delete errorBuffer;
	return -1;
}


