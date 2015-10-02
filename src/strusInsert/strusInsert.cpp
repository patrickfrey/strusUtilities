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
#include "strus/index.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/docnoRangeAllocatorInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionStorage.hpp"
#include "private/version.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "fileCrawler.hpp"
#include "commitQueue.hpp"
#include "insertProcessor.hpp"
#include "thread.hpp"
#include <iostream>
#include <sstream>
#include <memory>
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
	FILE* logfile = 0;
	strus::ErrorBufferInterface* errorBuffer = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 14,
				"h,help", "t,threads:", "c,commit:", "f,fetch:",
				"n,new", "v,version", "g,segmenter:", "m,module:",
				"M,moduledir:", "R,resourcedir:", "r,rpc:", "L,logerror:",
				"x,extension:", "s,storage:");

		unsigned int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
		}
		errorBuffer = strus::createErrorBuffer_standard( stderr, nofThreads+2);
		if (!errorBuffer) throw strus::runtime_error( _TXT("failed to create error buffer"));

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
			if (opt.nofargs() > 2)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir" ,"--rpc");
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
			std::cout << _TXT("usage:") << " strusInsert [options] <program> <docpath>" << std::endl;
			std::cout << "<program> = " << _TXT("path of analyzer program or analyzer map program") << std::endl;
			std::cout << "<docpath> = " << _TXT("path of document or directory to insert") << std::endl;
			std::cout << _TXT("description: Insert a document or a set of documents into a storage.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""));
			}
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME> (default textwolf)") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("Grab only the files with extension <EXT> (default all files)") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of inserter threads to use") << std::endl;
			std::cout << "-c|--commit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of documents inserted per transaction (default 1000)") << std::endl;
			std::cout << "-f|--fetch <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files fetched in each inserter iteration") << std::endl;
			std::cout << "    " << _TXT("Default is the value of option '--commit' (one document/file)") << std::endl;
			std::cout << "-n|--new" << std::endl;
			std::cout << "    " << _TXT("All inserts are new; use preallocated document numbers") << std::endl;
			std::cout << "-L|--logerror <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write the last error occurred to <FILE> in case of an exception")  << std::endl;
			return rt;
		}
		bool allInsertsNew = opt( "new");
		std::string storagecfg;
		unsigned int transactionSize = 1000;
		if (opt("logerror"))
		{
			std::string filename( opt["logerror"]);
			logfile = fopen( filename.c_str(), "a+");
			if (!logfile) throw strus::runtime_error(_TXT("error loading log file '%s' for appending (errno %u)"), filename.c_str(), errno);
			errorBuffer->setLogFile( logfile);
		}
		if (opt("commit"))
		{
			transactionSize = opt.asUint( "commit");
		}
		unsigned int fetchSize = transactionSize;
		if (opt("fetch"))
		{
			fetchSize = opt.asUint( "fetch");
		}
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
			storagecfg = opt["storage"];
		}
		std::string analyzerprg = opt[0];
		std::string datapath = opt[1];
		std::string fileext = "";
		std::string segmenter;
		if (opt( "segmenter"))
		{
			segmenter = opt[ "segmenter"];
		}
		if (opt( "extension"))
		{
			fileext = opt[ "extension"];
			if (fileext.size() && fileext[0] != '.')
			{
				fileext = std::string(".") + fileext;
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
		moduleLoader->addResourcePath( strus::getParentPath( analyzerprg));

		// Create objects for inserter:
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
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
		}
		strus::utils::ScopedPtr<strus::StorageClientInterface>
			storage( storageBuilder->createStorageClient( storagecfg));

		strus::utils::ScopedPtr<strus::DocumentAnalyzerInterface>
			analyzer( analyzerBuilder->createDocumentAnalyzer( segmenter));
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();

		// Load analyzer program(s):
		strus::AnalyzerMap analyzerMap( analyzerBuilder.get());
		analyzerMap.defineProgram( ""/*scheme*/, segmenter, analyzerprg);

		// Start inserter process:
		strus::utils::ScopedPtr<strus::CommitQueue>
			commitQue( new strus::CommitQueue( storage.get()));

		strus::utils::ScopedPtr<strus::DocnoRangeAllocatorInterface> docnoAllocator;
		if (allInsertsNew)
		{
			docnoAllocator.reset( storage->createDocnoRangeAllocator());
		}
		strus::FileCrawler* fileCrawler
			= new strus::FileCrawler( datapath, fetchSize, nofThreads*5+5, fileext);

		strus::utils::ScopedPtr< strus::Thread< strus::FileCrawler> >
			fileCrawlerThread(
				new strus::Thread< strus::FileCrawler >(
					fileCrawler, "filecrawler"));
		std::cout.flush();
		fileCrawlerThread->start();

		if (nofThreads == 0)
		{
			strus::InsertProcessor inserter(
				storage.get(), textproc, analyzerMap, docnoAllocator.get(),
				commitQue.get(), fileCrawler, transactionSize);
			inserter.run();
		}
		else
		{
			std::auto_ptr< strus::ThreadGroup< strus::InsertProcessor > >
				inserterThreads(
					new strus::ThreadGroup<strus::InsertProcessor>( "inserter"));

			for (unsigned int ti = 0; ti<nofThreads; ++ti)
			{
				inserterThreads->start(
					new strus::InsertProcessor(
						storage.get(), textproc, analyzerMap, docnoAllocator.get(),
						commitQue.get(), fileCrawler, transactionSize));
			}
			inserterThreads->wait_termination();
		}
		fileCrawlerThread->wait_termination();
		std::cerr << std::endl << "done" << std::endl;
		delete errorBuffer;
		if (logfile) fclose( logfile);
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
	if (logfile) fclose( logfile);
	return -1;
}


