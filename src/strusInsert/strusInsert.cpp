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
#include "private/version.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "fileCrawler.hpp"
#include "commitQueue.hpp"
#include "docnoAllocator.hpp"
#include "insertProcessor.hpp"
#include "thread.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <cstring>
#include <stdexcept>


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 8,
				"h,help", "t,threads:", "c,commit:",
				"n,new", "v,version", "m,module:",
				"s,segmenter:", "M,moduledir:");
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
	
		const strus::DatabaseInterface* dbi = builder.getDatabase( (opt.nofargs()>=1?opt[0]:""));
		const strus::StorageInterface* sti = builder.getStorage();

		if (printUsageAndExit)
		{
			std::cerr << "usage: strusInsert [options] <config> <program> <docpath>" << std::endl;
			std::cerr << "<config>  = storage configuration string" << std::endl;
			std::cerr << "            semicolon ';' separated list of assignments:" << std::endl;
			strus::printIndentMultilineString(
						std::cerr,
						12, dbi->getConfigDescription(
							strus::DatabaseInterface::CmdCreateClient));
			strus::printIndentMultilineString(
						std::cerr,
						12, sti->getConfigDescription(
							strus::StorageInterface::CmdCreateClient));
			std::cerr << "<program> = path of analyzer program" << std::endl;
			std::cerr << "<docpath> = path of document or directory to insert" << std::endl;
			std::cerr << "description: Insert a document or a set of documents into a storage." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "   Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			std::cerr << "-s|--segmenter <NAME>" << std::endl;
			std::cerr << "    Use the document segmenter with name <NAME> (default textwolf)" << std::endl;
			std::cerr << "-t|--threads <N>" << std::endl;
			std::cerr << "    Set <N> as number of inserter threads to use"  << std::endl;
			std::cerr << "-c|--commit <N>" << std::endl;
			std::cerr << "    Set <N> as number of files inserted per transaction (default 1000)" << std::endl;
			std::cerr << "-n|--new" << std::endl;
			std::cerr << "    All inserts are new; use preallocated document numbers" << std::endl;
			return rt;
		}
		bool allInsertsNew = opt( "new");
		unsigned int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
		}
		unsigned int transactionSize = 1000;
		if (opt("commit"))
		{
			transactionSize = opt.asUint( "commit");
		}
		std::string storagecfg( opt[0]);
		std::string analyzerprg = opt[1];
		std::string datapath = opt[2];
		std::string segmenter;
		if (opt( "segmenter"))
		{
			segmenter = opt[ "segmenter"];
		}

		// Create objects for inserter:
		strus::utils::ScopedPtr<strus::StorageClientInterface>
			storage( builder.createStorageClient( storagecfg));

		strus::utils::ScopedPtr<strus::DocumentAnalyzerInterface>
			analyzer( builder.createDocumentAnalyzer( segmenter));

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( opt[1], analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << opt[1] << " (file system error " << ec << ")" << std::endl;
			return 4;
		}
		strus::loadDocumentAnalyzerProgram( *analyzer, analyzerProgramSource);

		// Start inserter process:
		strus::utils::ScopedPtr<strus::CommitQueue>
			commitQue( new strus::CommitQueue( storage.get()));

		strus::utils::ScopedPtr<strus::DocnoAllocator> docnoAllocator;
		if (allInsertsNew)
		{
			docnoAllocator.reset( 
				new strus::DocnoAllocator( storage.get()));
		}
		strus::FileCrawler* fileCrawler
			= new strus::FileCrawler(
					datapath, docnoAllocator.get(),
					transactionSize, nofThreads*5+5);

		strus::utils::ScopedPtr< strus::Thread< strus::FileCrawler> >
			fileCrawlerThread(
				new strus::Thread< strus::FileCrawler >( fileCrawler,
					"filecrawler"));
		std::cout.flush();
		fileCrawlerThread->start();

		if (nofThreads == 0)
		{
			strus::InsertProcessor inserter(
				storage.get(), analyzer.get(),
				commitQue.get(), fileCrawler);
			inserter.run();
		}
		else
		{
			std::auto_ptr< strus::ThreadGroup< strus::InsertProcessor > >
				inserterThreads(
					new strus::ThreadGroup<strus::InsertProcessor>(
					"inserter"));

			for (unsigned int ti = 0; ti<nofThreads; ++ti)
			{
				inserterThreads->start(
					new strus::InsertProcessor(
						storage.get(), analyzer.get(), 
						commitQue.get(), fileCrawler));
			}
			inserterThreads->wait_termination();
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


