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
#include "strus/lib/database_leveldb.hpp"
#include "strus/lib/storage.hpp"
#include "strus/lib/analyzer.hpp"
#include "strus/lib/textprocessor.hpp"
#include "strus/lib/queryproc.hpp"
#include "strus/lib/queryeval.hpp"
#include "strus/lib/segmenter_textwolf.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/statCounterValue.hpp"
#include "strus/programLoader.hpp"
#include "programOptions.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#undef STRUS_LOWLEVEL_DEBUG
#define STRUS_MEASURE_TIME

int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 6,
				"h,help", "t,stats", "s,silent", "u,user:", "n,nofranks:", "g,globalstats:");
		if (opt( "help")) printUsageAndExit = true;

		if (opt.nofargs() > 4)
		{
			std::cerr << "ERROR too many arguments" << std::endl;
			printUsageAndExit = true;
			rt = 1;
		}
		if (opt.nofargs() < 4)
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
		std::cerr << "usage: strusQuery [options] <config> <anprg> <qeprg> <query>" << std::endl;
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
		std::cerr << "<anprg>   = path of query analyzer program" << std::endl;
		std::cerr << "<qeprg>   = path of query eval program" << std::endl;
		std::cerr << "<query>   = path of query or '-' for stdin" << std::endl;
		std::cerr << "option -h|--help       :Print this usage and do nothing else" << std::endl;
		std::cerr << "option -u|--user       :User name for the query" << std::endl;
		std::cerr << "option -n|--nofranks   :Number of result ranks to return" << std::endl;
		std::cerr << "option -s|--silent     :No output of results" << std::endl;
		std::cerr << "option -t|--stats      :Print some statistics available" << std::endl;
		std::cerr << "option -g|--globalstats:Load global statistics of peers from this file" << std::endl;
		return rt;
	}
	try
	{
		bool silent = opt( "silent");
		bool statistics = opt( "stats");
		std::string username;
		std::size_t nofRanks = 20;
		if (opt("user"))
		{
			username = opt[ "user"];
		}
		if (opt("nofranks"))
		{
			nofRanks = opt.as<std::size_t>( "nofranks");
		}
		std::string databasecfg( opt[0]);
		std::string storagecfg( opt[0]);
		std::string analyzerprg = opt[1];
		std::string queryprg = opt[2];
		std::string querypath = opt[3];

		strus::removeKeysFromConfigString(
				databasecfg,
				strus::getStorageConfigParameters( strus::CmdCreateStorageClient));
		//... In database_cfg is now the pure database configuration without the storage settings

		strus::removeKeysFromConfigString(
				storagecfg,
				strus::getDatabaseConfigParameters( strus::CmdCreateDatabaseClient));
		//... In storage_cfg is now the pure storage configuration without the database settings

		// Create objects for query evaluation:
		boost::scoped_ptr<strus::DatabaseInterface>
			database( strus::createDatabaseClient(
				databasecfg.c_str()));

		boost::scoped_ptr<strus::StorageInterface>
			storage( strus::createStorageClient(
				storagecfg.c_str(), database.get()));

		boost::scoped_ptr<strus::TextProcessorInterface> textproc(
			strus::createTextProcessor());

		boost::scoped_ptr<strus::QueryAnalyzerInterface> analyzer(
			strus::createQueryAnalyzer( textproc.get()));

		boost::scoped_ptr<strus::QueryProcessorInterface> qproc(
			strus::createQueryProcessorInterface( storage.get()));

		boost::scoped_ptr<strus::QueryEvalInterface> qeval(
			strus::createQueryEval( qproc.get()));

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
		strus::loadQueryAnalyzerProgram( *analyzer, analyzerProgramSource);

		// Load query evaluation program:
		std::string qevalProgramSource;
		ec = strus::readFile( queryprg, qevalProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load query eval program " << queryprg << " (file system error " << ec << ")" << std::endl;
			return 3;
		}
		strus::loadQueryEvalProgram( *qeval, *qproc, qevalProgramSource);

		// Load global statistics from file if specified:
		if (opt("globalstats"))
		{
			std::string filename = opt[ "globalstats"];
			std::ifstream file;
			file.exceptions( std::ifstream::failbit | std::ifstream::badbit);
			try 
			{
				file.open( filename.c_str(), std::fstream::in);
				strus::loadGlobalStatistics( *storage, file);
			}
			catch (const std::ifstream::failure& err)
			{
				throw std::runtime_error( std::string( "failed to read global statistics from file '") + filename + "': " + err.what());
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

#ifdef STRUS_MEASURE_TIME
		std::clock_t start;
		unsigned int nofQueries = 0;
		start = std::clock();
#endif
		std::string::const_iterator si = querystring.begin(), se = querystring.end();
		std::string qs;
		while (strus::scanNextProgram( qs, si, se))
		{
			++nofQueries;
			boost::scoped_ptr<strus::QueryInterface> query(
				qeval->createQuery( storage.get()));

			strus::loadQuery( *query, *analyzer, qs);

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
#ifdef STRUS_MEASURE_TIME
		float duration = (std::clock() - start) / (double) CLOCKS_PER_SEC;
		std::cerr << "evaluated " << nofQueries << " queries in "
				<< std::fixed << std::setw(6) << std::setprecision(3)
				<< duration << " seconds" << std::endl;
#endif
		if (statistics)
		{
			std::vector<strus::StatCounterValue> stats = storage->getStatistics();
			std::vector<strus::StatCounterValue>::const_iterator
				si = stats.begin(), se = stats.end();
			std::cerr << "Statistics:" << std::endl;
			for (; si != se; ++si)
			{
				std::cerr << "\t" << si->name() << " = " << si->value() << std::endl;
			}
		}
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


