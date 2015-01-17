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
#include "strus/analyzerInterface.hpp"
#include "strus/analyzerLib.hpp"
#include "strus/tokenMiner.hpp"
#include "strus/tokenMinerFactory.hpp"
#include "strus/tokenMinerLib.hpp"
#include "strus/fileio.hpp"
#include "strus/cmdLineOpt.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <boost/scoped_ptr.hpp>

int main( int argc, const char* argv[])
{
	int rt = 0;
	if (argc > 3)
	{
		std::cerr << "ERROR too many arguments" << std::endl;
		rt = 1;
	}
	if (argc < 3)
	{
		std::cerr << "ERROR too few arguments" << std::endl;
		rt = 2;
	}
	if (rt || std::strcmp( argv[1], "-h") == 0 || std::strcmp( argv[1], "--help") == 0)
	{
		std::cerr << "usage: strusAnalyze <program> <document>" << std::endl;
		std::cerr << "<program>     = path of analyzer program" << std::endl;
		std::cerr << "<document>    = path of document to analyze" << std::endl;
		return rt;
	}
	try
	{
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( argv[1], analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			msg << "failed to load analyzer program " << argv[1] << " (file system error '" << ec << ")";
			throw std::runtime_error( msg.str());
		}
		std::string documentContent;
		ec = strus::readFile( argv[2], documentContent);
		if (ec)
		{
			std::ostringstream msg;
			msg << "failed to load document to analyze " << argv[2] << " (file system error '" << ec << ")";
			throw std::runtime_error( msg.str());
		}
		std::string tokenMinerSource;
		boost::scoped_ptr<strus::TokenMinerFactory> minerfac(
			strus::createTokenMinerFactory( tokenMinerSource));

		boost::scoped_ptr<strus::AnalyzerInterface> analyzer(
			strus::createAnalyzer( *minerfac, analyzerProgramSource));

		strus::analyzer::Document doc
			= analyzer->analyze( documentContent);

		std::vector<strus::analyzer::Term>::const_iterator
			ti = doc.searchIndexTerms().begin(), te = doc.searchIndexTerms().end();

		std::cout << "search index terms:" << std::endl;
		for (; ti != te; ++ti)
		{
			std::cout << ti->pos()
				  << " " << ti->type()
				  << " '" << ti->value() << "'"
				  << std::endl;
		}

		std::vector<strus::analyzer::Term>::const_iterator
			fi = doc.forwardIndexTerms().begin(), fe = doc.forwardIndexTerms().end();

		std::cout << "forward index terms:" << std::endl;
		for (; fi != fe; ++fi)
		{
			std::cout << fi->pos()
				  << " " << fi->type()
				  << " '" << fi->value() << "'"
				  << std::endl;
		}

		std::vector<strus::analyzer::MetaData>::const_iterator
			mi = doc.metadata().begin(), me = doc.metadata().end();

		std::cout << std::endl << "metadata:" << std::endl;
		for (; mi != me; ++mi)
		{
			std::cout << mi->name()
				  << " '" << mi->value() << "'"
				  << std::endl;
		}

		std::vector<strus::analyzer::Attribute>::const_iterator
			ai = doc.attributes().begin(), ae = doc.attributes().end();

		std::cout << std::endl << "attributes:" << std::endl;
		for (; ai != ae; ++ai)
		{
			std::cout << ai->name()
				  << " '" << ai->value() << "'"
				  << std::endl;
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


