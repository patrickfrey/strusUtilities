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
#include "strus/constants.hpp"
#include "strus/analyzerInterface.hpp"
#include "strus/analyzerLib.hpp"
#include "strus/tokenMiner.hpp"
#include "strus/tokenMinerFactory.hpp"
#include "strus/tokenMinerLib.hpp"
#include "strus/storageLib.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageInserterInterface.hpp"
#include "strus/constants.hpp"
#include "strus/utils/fileio.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <boost/scoped_ptr.hpp>

static int failedOperations = 0;
static int succeededOperations = 0;
static int loopCount = 0;

static bool processDocument( 
	strus::StorageInterface* storage,
	const strus::AnalyzerInterface* analyzer,
	const std::string& path)
{
	try
	{
		// Read the input file to analyze:
		std::string documentContent;
		unsigned int ec = strus::readFile( path, documentContent);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load document to analyze " << path << " (file system error " << ec << ")" << std::endl;
			return false;
		}

		// Call the analyzer and open the inserter:
		strus::analyzer::Document doc
			= analyzer->analyze( documentContent);

		boost::scoped_ptr<strus::StorageInserterInterface>
			inserter( storage->createInserter( path));

		strus::Index lastPos = (doc.terms().empty())?0:doc.terms()[ doc.terms().size()-1].pos();

		// Define hardcoded document meta data:
		inserter->setAttribute(
			strus::Constants::attribute_docid(), path);
		inserter->setMetaData(
			strus::Constants::metadata_doclen(), lastPos);

		// Define all term occurrencies:
		std::vector<strus::analyzer::Term>::const_iterator
			ti = doc.terms().begin(), te = doc.terms().end();
		for (; ti != te; ++ti)
		{
			inserter->addTermOccurrence(
				ti->type(), ti->value(), ti->pos(), 0.0/*weight*/);
		}

		// Define all attributes extracted from the document analysis:
		std::vector<strus::analyzer::Attribute>::const_iterator
			ai = doc.attributes().begin(), ae = doc.attributes().end();
		for (; ai != ae; ++ai)
		{
			inserter->setAttribute( ai->name(), ai->value());
		}

		// Define all metadata elements extracted from the document analysis:
		std::vector<strus::analyzer::MetaData>::const_iterator
			mi = doc.metadata().begin(), me = doc.metadata().end();
		for (; mi != me; ++mi)
		{
			inserter->setMetaData( mi->name(), mi->value());
		}

		inserter->done();

		// Notify progress:
		if (++loopCount == 1000)
		{
			loopCount = 0;
			std::cerr << "inserted " << (succeededOperations+1) << " documents" << std::endl;
		}
		return true;
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << "ERROR failed to insert document '" << path << "': " << err.what() << std::endl;
		return false;
	}
}

static bool processDirectory( 
	strus::StorageInterface* storage,
	const strus::AnalyzerInterface* analyzer,
	const std::string& path)
{
	std::vector<std::string> filesToProcess;
	unsigned int ec = strus::readDir( path, ".xml", filesToProcess);
	if (ec)
	{
		std::cerr << "ERROR could not read directory to process '" << path << "' (file system error '" << ec << ")" << std::endl;
		return false;
	}
	std::vector<std::string>::const_iterator pi = filesToProcess.begin(), pe = filesToProcess.end();
	for (; pi != pe; ++pi)
	{
		std::string file( path);
		if (file.size() && file[ file.size()-1] != strus::dirSeparator())
		{
			file.push_back( strus::dirSeparator());
		}
		file.append( *pi);
		if (processDocument( storage, analyzer, file))
		{
			++succeededOperations;
		}
		else
		{
			++failedOperations;
		}
	}
	std::vector<std::string> subdirsToProcess;
	ec = strus::readDir( path, "", subdirsToProcess);
	if (ec)
	{
		std::cerr << "ERROR could not read subdirectories to process '" << path << "' (file system error " << ec << ")" << std::endl;
		return false;
	}
	std::vector<std::string>::const_iterator di = subdirsToProcess.begin(), de = subdirsToProcess.end();
	for (; di != de; ++di)
	{
		std::string subdir( path + strus::dirSeparator() + *di);
		if (strus::isDir( subdir))
		{
			if (!processDirectory( storage, analyzer, subdir))
			{
				std::cerr << "ERROR failed to process subdirectory '" << subdir << "'" << std::endl;
				return false;
			}
		}
	}
	return true;
}


int main( int argc, const char* argv[])
{

	if (argc > 4)
	{
		std::cerr << "ERROR too many arguments" << std::endl;
	}
	if (argc < 4)
	{
		std::cerr << "ERROR too few arguments" << std::endl;
	}
	if (argc != 4 || std::strcmp( argv[1], "-h") == 0 || std::strcmp( argv[1], "--help") == 0)
	{
		std::cerr << "usage: strusInsert <config> <program> <docpath>" << std::endl;
		std::cerr << "<config>      = storage configuration string as used for strusCreate" << std::endl;
		std::cerr << "<program>     = path of analyzer program" << std::endl;
		std::cerr << "<docpath>     = path of document or directory to insert" << std::endl;
		return 0;
	}
	try
	{
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( argv[2], analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << argv[1] << " (file system error " << ec << ")" << std::endl;
			return 2;
		}
		boost::scoped_ptr<strus::StorageInterface>
			storage( strus::createStorageClient( argv[1]));

		std::string tokenMinerSource;
		boost::scoped_ptr<strus::TokenMinerFactory>
			minerfac( strus::createTokenMinerFactory( tokenMinerSource));

		boost::scoped_ptr<strus::AnalyzerInterface>
			analyzer( strus::createAnalyzer( *minerfac, analyzerProgramSource));

		std::string path( argv[3]);
		if (strus::isDir( path))
		{
			if (!processDirectory( storage.get(), analyzer.get(), path))
			{
				std::cerr << "ERROR failed processing of directory '" << path << "'" << std::endl;
				return 3;
			}
		}
		else if (strus::isFile( path))
		{
			if (processDocument( storage.get(), analyzer.get(), path))
			{
				++succeededOperations;
			}
			else
			{
				++failedOperations;
			}
		}
		else
		{
			std::cerr << "ERROR item '" << path << "' to process is neither a file nor a directory" << std::endl;
			return 4;
		}
		if (failedOperations > 0)
		{
			std::cerr << "total " << failedOperations << " inserts failed out of " << (succeededOperations + failedOperations) << std::endl;
		}
		else
		{
			std::cerr << "successfully inserted " << (succeededOperations + failedOperations) << " documents" << std::endl;
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


