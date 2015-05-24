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
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionStorage.hpp"
#include "private/version.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
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
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 12,
				"h,help", "S,silent", "u,user:", "n,nofranks:",
				"g,globalstats:", "t,time", "v,version", "m,module:",
				"M,moduledir:", "R,resourcedir:", "s,storage:", "r,rpc:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus storage version " << STRUS_STORAGE_VERSION_STRING << std::endl;
			std::cout << "Strus analyzer version " << STRUS_ANALYZER_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else
		{
			if (opt.nofargs() > 3)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 3)
			{
				std::cerr << "ERROR too few arguments" << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader());
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --moduledir and --rpc");
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
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --module and --rpc");
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->loadModule( *mi);
			}
		}

		if (printUsageAndExit)
		{
			std::cerr << "usage: strusQuery [options] <anprg> <qeprg> <query>" << std::endl;
			std::cerr << "<anprg>   = path of query analyzer program" << std::endl;
			std::cerr << "<qeprg>   = path of query eval program" << std::endl;
			std::cerr << "<query>   = path of query or '-' for stdin" << std::endl;
			std::cerr << "description: Executes a query or a list of queries from a file." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "    Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-s|--storage <CONFIG>" << std::endl;
			std::cerr << "    Define the storage configuration string as <CONFIG>" << std::endl;
			if (!opt("rpc"))
			{
				std::cerr << "    <CONFIG> is a semicolon ';' separated list of assignments:" << std::endl;
				printStorageConfigOptions( std::cerr, moduleLoader.get(), (opt("storage")?opt["storage"]:""));
			}
			std::cerr << "-u|--user <NAME>" << std::endl;
			std::cerr << "    Use user name <NAME> for the query" << std::endl;
			std::cerr << "-n|--nofranks <N>" << std::endl;
			std::cerr << "    Return maximum <N> ranks as query result" << std::endl;
			std::cerr << "-S|--silent" << std::endl;
			std::cerr << "    No output of results" << std::endl;
			std::cerr << "-g|--globalstats <FILE>" << std::endl;
			std::cerr << "    Load global statistics of peers from file <FILE>" << std::endl;
			std::cerr << "-t|--time" << std::endl;
			std::cerr << "    Do print duration of pure query evaluation" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			std::cerr << "-R|--resourcedir <DIR>" << std::endl;
			std::cerr << "    Search resource files for analyzer first in <DIR>" << std::endl;
			std::cerr << "-r|--rpc <ADDR>" << std::endl;
			std::cerr << "    Execute the command on the RPC server specified by <ADDR>" << std::endl;
			return rt;
		}
		bool silent = opt( "silent");
		bool measureDuration = opt( "time");
		std::string username;
		std::size_t nofRanks = 20;
		std::string storagecfg;
		if (opt("user"))
		{
			username = opt[ "user"];
		}
		if (opt("nofranks"))
		{
			nofRanks = opt.asUint( "nofranks");
		}
		if (opt("storage"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --moduledir and --rpc");
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
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"]));
			rpcClient.reset( strus::createRpcClient( messaging.get()));
			(void)messaging.release();
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
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
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << analyzerprg << " (file system error " << ec << ")" << std::endl;
			return 2;
		}
		strus::loadQueryAnalyzerProgram( *analyzer, textproc, analyzerProgramSource);

		// Load query evaluation program:
		std::string qevalProgramSource;
		ec = strus::readFile( queryprg, qevalProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load query eval program " << queryprg << " (file system error " << ec << ")" << std::endl;
			return 3;
		}
		strus::loadQueryEvalProgram( *qeval, qproc, qevalProgramSource);

		// Load global statistics from file if specified:
		if (opt("globalstats"))
		{
			std::vector<std::string> pathlist( opt.list("globalstats"));
			std::string filename = opt[ "globalstats"];
			try 
			{
				strus::loadGlobalStatistics( *storage, filename);
			}
			catch (const std::runtime_error& err)
			{
				throw std::runtime_error( std::string( "failed to read global statistics from file '") + filename + "': " + err.what());
			}
		}

		// Load query:
		std::string querystring;
		if (querypath == "-")
		{
			ec = strus::readStdin( querystring);
			if (ec)
			{
				std::cerr << "ERROR failed to read query string from stdin" << std::endl;
				return 3;
			}
		}
		else
		{
			ec = strus::readFile( querypath, querystring);
			if (ec)
			{
				std::cerr << "ERROR failed to read query string from file '" << querypath << "'" << std::endl;
				return 4;
			}
		}

		unsigned int nofQueries = 0;
		double startTime;
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
			query->setMinRank( 0);
			if (!username.empty())
			{
				query->setUserName( username);
			}
			std::vector<strus::ResultDocument>
				ranklist = query->evaluate();
	
			if (!silent) std::cout << "ranked list (maximum 20 matches):" << std::endl;
			std::vector<strus::ResultDocument>::const_iterator wi = ranklist.begin(), we = ranklist.end();
			for (int widx=1; wi != we; ++wi,++widx)
			{
				if (!silent) std::cout << "[" << widx << "] " << wi->docno() << " score " << wi->weight() << std::endl;
				std::vector<strus::ResultDocument::Attribute>::const_iterator ai = wi->attributes().begin(), ae = wi->attributes().end();
				for (; ai != ae; ++ai)
				{
					if (!silent) std::cout << "\t" << ai->name() << " (" << ai->value() << ")" << std::endl;
				}
			}
		}
		if (measureDuration)
		{
			double endTime = getTimeStamp();
			double duration = endTime - startTime;
			std::cerr << "evaluated " << nofQueries << " queries in "
					<< std::fixed << std::setw(6) << std::setprecision(3)
					<< duration << " seconds" << std::endl;
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


