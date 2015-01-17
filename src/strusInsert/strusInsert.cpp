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
#include "strus/index.hpp"
#include "strus/analyzerInterface.hpp"
#include "strus/analyzerLib.hpp"
#include "strus/tokenMiner.hpp"
#include "strus/tokenMinerFactory.hpp"
#include "strus/tokenMinerLib.hpp"
#include "strus/storageLib.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/fileio.hpp"
#include "strus/cmdLineOpt.hpp"
#include "programOptions.hpp"
#include "fileCrawler.hpp"
#include "commitQueue.hpp"
#include "docnoAllocator.hpp"
#include "insertProcessor.hpp"
#include "thread.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <boost/scoped_ptr.hpp>


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 4,
				"h,help", "t,threads:", "c,commit:", "n,new");
		if (opt( "help")) printUsageAndExit = true;

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
	catch (const std::runtime_error& err)
	{
		std::cerr << "ERROR in arguments: " << err.what() << std::endl;
		printUsageAndExit = true;
		rt = 3;
	}
	if (printUsageAndExit)
	{
		std::cerr << "usage: strusInsert [options] <config> <program> <docpath>" << std::endl;
		std::cerr << "<config>  = storage configuration string" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getStorageConfigDescription(
						strus::CmdCreateStorageClient));
		std::cerr << "<program> = path of analyzer program" << std::endl;
		std::cerr << "<docpath> = path of document or directory to insert" << std::endl;
		std::cerr << "options:" << std::endl;
		std::cerr << "-h,--help    : Print this usage info" << std::endl;
		std::cerr << "-t,--threads : Number of inserter threads to use"  << std::endl;
		std::cerr << "-c,--commit  : Number of files inserted per transaction (default 1000)" << std::endl;
		std::cerr << "-n,--new     : All inserts are new; use preallocated document numbers" << std::endl;
		return rt;
	}
	try
	{
		bool allInsertsNew = opt( "new");
		unsigned int nofThreads = opt.as<unsigned int>( "threads");
		unsigned int transactionSize = opt.as<unsigned int>( "commit");
		if (!transactionSize) transactionSize = 1000;

		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( opt[1], analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << opt[1] << " (file system error " << ec << ")" << std::endl;
			return 4;
		}
		boost::scoped_ptr<strus::StorageInterface>
			storage( strus::createStorageClient( opt[0]));

		std::string tokenMinerSource;
		boost::scoped_ptr<strus::TokenMinerFactory>
			minerfac( strus::createTokenMinerFactory( tokenMinerSource));

		boost::scoped_ptr<strus::AnalyzerInterface>
			analyzer( strus::createAnalyzer( *minerfac, analyzerProgramSource));

		boost::scoped_ptr<strus::CommitQueue>
			commitQue( new strus::CommitQueue( storage.get()));

		boost::scoped_ptr<strus::DocnoAllocator> docnoAllocator;
		if (allInsertsNew)
		{
			docnoAllocator.reset( 
				new strus::DocnoAllocator( storage.get()));
		}
		strus::FileCrawler* fileCrawler
			= new strus::FileCrawler(
					opt[2], docnoAllocator.get(),
					transactionSize, nofThreads*5+5);

		boost::scoped_ptr< strus::Thread< strus::FileCrawler> >
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
			boost::scoped_ptr< strus::ThreadGroup< strus::InsertProcessor > >
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


