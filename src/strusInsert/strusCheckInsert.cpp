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
#include "strus/databaseLib.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/storageLib.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/databaseLib.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "programOptions.hpp"
#include "fileCrawler.hpp"
#include "commitQueue.hpp"
#include "docnoAllocator.hpp"
#include "checkInsertProcessor.hpp"
#include "thread.hpp"
#include "programOptions.hpp"
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
				"h,help", "t,threads:", "l,logfile:", "n,notify:");
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
		std::cerr << "usage: strusCheckInsert [options] <config> <program> <docpath>" << std::endl;
		std::cerr << "<config>  = storage configuration string" << std::endl;
		std::cerr << "            semicolon ';' separated list of assignments:" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getDatabaseConfigDescription(
						strus::CmdCreateDatabaseClient));
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getStorageConfigDescription(
						strus::CmdCreateStorageClient));
		std::cerr << "<program> = path of analyzer program" << std::endl;
		std::cerr << "<docpath> = path of document or directory to check" << std::endl;
		std::cerr << "options:" << std::endl;
		std::cerr << "-h,--help    : Print this usage info" << std::endl;
		std::cerr << "-t,--threads : Number of check insert threads to use"  << std::endl;
		std::cerr << "-l,--logfile : File to use for output (default stdout)"  << std::endl;
		std::cerr << "-n,--notify  : Notification interval (number of documents)" << std::endl;
		return rt;
	}
	try
	{
		unsigned int nofThreads = opt.as<unsigned int>( "threads");
		std::string logfile = "-";
		if (opt("logfile"))
		{
			logfile = opt[ "logfile"];
		}
		unsigned int notificationInterval = 1000;
		if (opt("notify"))
		{
			notificationInterval = opt.as<unsigned int>( "notify");
		}
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( opt[1], analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << opt[1] << " (file system error " << ec << ")" << std::endl;
			return 4;
		}
		std::string database_cfg( opt[0]);
		strus::removeKeysFromConfigString(
				database_cfg,
				strus::getStorageConfigParameters( strus::CmdCreateStorageClient));
		//... In database_cfg is now the pure database configuration without the storage settings

		std::string storage_cfg( opt[0]);
		strus::removeKeysFromConfigString(
				storage_cfg,
				strus::getDatabaseConfigParameters( strus::CmdCreateDatabaseClient));
		//... In storage_cfg is now the pure storage configuration without the database settings

		boost::scoped_ptr<strus::DatabaseInterface>
			database( strus::createDatabaseClient(
				database_cfg.c_str()));

		boost::scoped_ptr<strus::StorageInterface>
			storage( strus::createStorageClient(
				storage_cfg.c_str(), database.get()));

		std::string tokenMinerSource;
		boost::scoped_ptr<strus::TokenMinerFactory>
			minerfac( strus::createTokenMinerFactory( tokenMinerSource));

		boost::scoped_ptr<strus::AnalyzerInterface>
			analyzer( strus::createAnalyzer( *minerfac, analyzerProgramSource));

		strus::FileCrawler* fileCrawler
			= new strus::FileCrawler(
				opt[2], 0, notificationInterval, nofThreads*5+5);

		boost::scoped_ptr< strus::Thread< strus::FileCrawler> >
			fileCrawlerThread(
				new strus::Thread< strus::FileCrawler >( fileCrawler,
					"filecrawler"));
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
			boost::scoped_ptr< strus::ThreadGroup< strus::CheckInsertProcessor > >
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


