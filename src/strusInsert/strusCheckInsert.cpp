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
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/index.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionStorage.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
#include "fileCrawler.hpp"
#include "commitQueue.hpp"
#include "checkInsertProcessor.hpp"
#include "thread.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>

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


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 11,
				"h,help", "t,threads:", "l,logfile:",
				"n,notify:", "v,version", "R,resourcedir:",
				"M,moduledir:", "m,module:",
				"r,rpc:", "g,segmenter:", "s,storage:");
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
			if (opt.nofargs() > 2)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
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
			std::cerr << "usage: strusCheckInsert [options] <program> <docpath>" << std::endl;
			std::cerr << "<program> = path of analyzer program" << std::endl;
			std::cerr << "<docpath> = path of document or directory to check" << std::endl;
			std::cerr << "description: Checks if a storage contains all data of a document set." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "   Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-s|--storage <CONFIG>" << std::endl;
			std::cerr << "    Define the storage configuration string as <CONFIG>" << std::endl;
			if (!opt("rpc"))
			{
				std::cerr << "    <CONFIG> is a semicolon ';' separated list of assignments:" << std::endl;
				printStorageConfigOptions( std::cerr, moduleLoader.get(), (opt("storage")?opt["storage"]:""));
			}
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			std::cerr << "-R|--resourcedir <DIR>" << std::endl;
			std::cerr << "    Search resource files for analyzer first in <DIR>" << std::endl;
			std::cerr << "-r|--rpc <ADDR>" << std::endl;
			std::cerr << "    Execute the command on the RPC server specified by <ADDR>" << std::endl;
			std::cerr << "-g|--segmenter <NAME>" << std::endl;
			std::cerr << "    Use the document segmenter with name <NAME> (default textwolf XML)" << std::endl;
			std::cerr << "-t|--threads <N>" << std::endl;
			std::cerr << "    Set <N> as number of inserter threads to use"  << std::endl;
			std::cerr << "-l|--logfile <FILE>" << std::endl;
			std::cerr << "    Set <FILE> as output file (default stdout)"  << std::endl;
			std::cerr << "-n|--notify <N>" << std::endl;
			std::cerr << "    Set <N> as notification interval (number of documents)" << std::endl;
			return rt;
		}
		unsigned int nofThreads = opt.asUint( "threads");
		std::string logfile = "-";
		std::string storagecfg;
		if (opt("logfile"))
		{
			logfile = opt[ "logfile"];
		}
		unsigned int notificationInterval = 1000;
		if (opt("notify"))
		{
			notificationInterval = opt.asUint( "notify");
		}
		if (opt("storage"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --moduledir and --rpc");
			storagecfg = opt["storage"];
		}
		std::string analyzerprg = opt[0];
		std::string datapath = opt[1];
		std::string segmenter;
		if (opt( "segmenter"))
		{
			segmenter = opt[ "segmenter"];
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
		moduleLoader->addResourcePath( strus::getParentPath( analyzerprg));

		// Create objects for insert checker:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"]));
			rpcClient.reset( strus::createRpcClient( messaging.get()));
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

		strus::utils::ScopedPtr<strus::DocumentAnalyzerInterface>
			analyzer( analyzerBuilder->createDocumentAnalyzer( segmenter));

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program '" << analyzerprg << "' (file system error " << ec << ")" << std::endl;
			return 4;
		}
		strus::loadDocumentAnalyzerProgram( *analyzer, analyzerProgramSource);

		strus::FileCrawler* fileCrawler
			= new strus::FileCrawler(
				datapath, notificationInterval, nofThreads*5+5);

		std::auto_ptr< strus::Thread< strus::FileCrawler> >
			fileCrawlerThread(
				new strus::Thread< strus::FileCrawler >( fileCrawler, "filecrawler"));
		std::cout.flush();
		fileCrawlerThread->start();

		if (nofThreads == 0)
		{
			strus::CheckInsertProcessor checker(
				storage.get(), analyzer.get(), fileCrawler, logfile);
			checker.run();
		}
		else
		{
			std::auto_ptr< strus::ThreadGroup< strus::CheckInsertProcessor > >
				checkInsertThreads(
					new strus::ThreadGroup<strus::CheckInsertProcessor>(
					"checker"));

			for (unsigned int ti = 0; ti<nofThreads; ++ti)
			{
				checkInsertThreads->start(
					new strus::CheckInsertProcessor(
						storage.get(), analyzer.get(),
						fileCrawler, logfile));
			}
			checkInsertThreads->wait_termination();
		}
		fileCrawlerThread->wait_termination();
		std::cerr << "done" << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		std::cerr << "ERROR " << e.what() << std::endl;
		return 6;
	}
	catch (const std::exception& e)
	{
		std::cerr << "EXCEPTION " << e.what() << std::endl;
		return 7;
	}
	return 0;
}


