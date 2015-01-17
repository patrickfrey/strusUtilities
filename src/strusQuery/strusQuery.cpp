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
#include "strus/tokenMinerFactory.hpp"
#include "strus/tokenMinerLib.hpp"
#include "strus/analyzerInterface.hpp"
#include "strus/analyzerLib.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageLib.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryEvalLib.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/queryProcessorLib.hpp"
#include "strus/utils/fileio.hpp"
#include "strus/utils/cmdLineOpt.hpp"
#include "strus/statCounterValue.hpp"
#include "programOptions.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <boost/scoped_ptr.hpp>

#undef STRUS_LOWLEVEL_DEBUG
namespace {
class TermPosComparator
{
public:
	typedef strus::analyzer::Term Term;
	bool operator() (Term const& aa, Term const& bb) const
	{
		return (aa.pos() < bb.pos());
	}
};
}//anonymous namespace

static bool processQuery( 
	const strus::StorageInterface* storage,
	const strus::AnalyzerInterface* analyzer,
	const strus::QueryProcessorInterface* qproc,
	const strus::QueryEvalInterface* qeval,
	const std::string& username,
	const std::string& querystring,
	bool silent)
{
	try
	{
		boost::scoped_ptr<strus::QueryInterface> query( qeval->createQuery());
		typedef strus::analyzer::Term Term;

		strus::analyzer::Document doc = analyzer->analyze( querystring);

		if (doc.metadata().size())
		{
			std::cerr << "unexpected meta data definitions in the query (ignored)" << std::endl;
		}
		std::vector<Term> termar = doc.searchIndexTerms();
		if (termar.empty())
		{
			std::cerr << "query got empty after analyze (did you use the right analyzer program ?)" << std::endl;
		}
		std::sort( termar.begin(), termar.end(), TermPosComparator());

#ifdef STRUS_LOWLEVEL_DEBUG
		if (!silent) std::cout << "analyzed query:" << std::endl;
		std::vector<Term>::const_iterator ati = termar.begin(), ate = termar.end();
		for (; ati!=ate; ++ati)
		{
			if (!silent) std::cout << ati->pos()
				  << " " << ati->type()
				  << " '" << ati->value() << "'"
				  << std::endl;
		}
#endif
		
		std::vector<Term>::const_iterator ti = termar.begin(), tv = termar.begin(), te = termar.end();
		for (; ti!=te; tv=ti,++ti)
		{
			query->pushTerm( ti->type(), ti->value());
			query->defineFeature( ti->type()/*set*/);
		}
		query->setMaxNofRanks( 20);
		query->setMinRank( 0);
		query->setUserName( username);

		std::vector<strus::queryeval::ResultDocument>
			ranklist = query->evaluate( storage);

		if (!silent) std::cout << "ranked list (maximum 20 matches):" << std::endl;
		std::vector<strus::queryeval::ResultDocument>::const_iterator wi = ranklist.begin(), we = ranklist.end();
		for (int widx=1; wi != we; ++wi,++widx)
		{
			if (!silent) std::cout << "[" << widx << "] " << wi->docno() << " score " << wi->weight() << std::endl;
			std::vector<strus::queryeval::ResultDocument::Attribute>::const_iterator ai = wi->attributes().begin(), ae = wi->attributes().end();
			for (; ai != ae; ++ai)
			{
				if (!silent) std::cout << "\t" << ai->name() << " (" << ai->value() << ")" << std::endl;
			}
		}
		return true;
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << "ERROR failed to evaluate query: " << err.what() << std::endl;
		return false;
	}
}

static bool getNextQuery( std::string& qs, std::string::const_iterator& si, const std::string::const_iterator& se)
{
	for (; si != se; ++si)
	{
		if ((unsigned char)*si > 32) break;
	}
	if (si == se)
	{
		return false;
	}
	if (*si != '<' || *(si+1) != '?')
	{
		throw std::runtime_error("query string is not an XML with header");
	}
	std::string::const_iterator start = si;
	for (++si; si != se; ++si)
	{
		if (*si == '<' && *(si+1) == '?')
		{
			break;
		}
	}
	qs = std::string( start, si);
	return true;
}

int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 4,
				"h,help", "t,stats", "s,silent", "u,user:");
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
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getStorageConfigDescription(
						strus::CmdCreateStorageClient));
		std::cerr << "<anprg>   = path of query analyzer program" << std::endl;
		std::cerr << "<qeprg>   = path of query eval program" << std::endl;
		std::cerr << "<query>   = path of query or '-' for stdin" << std::endl;
		std::cerr << "option -h|--help   :Print this usage and do nothing else" << std::endl;
		std::cerr << "option -u|--user   :User name for the query" << std::endl;
		std::cerr << "option -s|--silent :No output of results" << std::endl;
		std::cerr << "option -t|--stats  :Print some statistics available" << std::endl;
		return rt;
	}
	try
	{
		bool silent = opt( "silent");
		bool statistics = opt( "stats");
		std::string username;
		if (opt("user"))
		{
			username = opt[ "user"];
		}
		std::string storagecfg = opt[0];
		std::string analyzerprg = opt[1];
		std::string queryprg = opt[2];
		std::string querypath = opt[3];

		boost::scoped_ptr<strus::StorageInterface> storage(
			strus::createStorageClient( storagecfg.c_str()));

		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << analyzerprg << " (file system error " << ec << ")" << std::endl;
			return 2;
		}
		std::string tokenMinerSource;
		boost::scoped_ptr<strus::TokenMinerFactory> minerfac(
			strus::createTokenMinerFactory( tokenMinerSource));

		boost::scoped_ptr<strus::AnalyzerInterface> analyzer(
			strus::createAnalyzer( *minerfac, analyzerProgramSource));

		boost::scoped_ptr<strus::QueryProcessorInterface> qproc(
			strus::createQueryProcessorInterface( storage.get()));

		std::string qevalProgramSource;
		ec = strus::readFile( queryprg, qevalProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load query eval program " << queryprg << " (file system error " << ec << ")" << std::endl;
			return 3;
		}
		boost::scoped_ptr<strus::QueryEvalInterface> qeval(
			strus::createQueryEval( qproc.get(), qevalProgramSource));

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
		std::string::const_iterator si = querystring.begin(), se = querystring.end();
		std::string qs;
		while (getNextQuery( qs, si, se))
		{
			if (!processQuery( storage.get(), analyzer.get(), qproc.get(), qeval.get(), username, qs, silent))
			{
				std::cerr << "ERROR query evaluation failed" << std::endl;
				return 5;
			}
		}
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


