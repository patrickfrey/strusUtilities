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
#include "strus/arithmeticVariant.hpp"
#include "strus/analyzerInterface.hpp"
#include "strus/analyzerLib.hpp"
#include "strus/tokenMiner.hpp"
#include "strus/tokenMinerFactory.hpp"
#include "strus/tokenMinerLib.hpp"
#include "strus/storageLib.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/constants.hpp"
#include "strus/utils/fileio.hpp"
#include "strus/utils/cmdLineOpt.hpp"
#include "programOptions.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <boost/scoped_ptr.hpp>

static int failedOperations = 0;
static int succeededOperations = 0;
static int loopCount = 0;
static int notifyProgress = 0;

static bool processDocument( 
	strus::StorageTransactionInterface* transaction,
	const strus::AnalyzerInterface* analyzer,
	const std::string& path,
	bool hasDoclenAttribute)
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

		// Call the analyzer and create the document:
		strus::analyzer::Document doc
			= analyzer->analyze( documentContent);

		boost::scoped_ptr<strus::StorageDocumentInterface>
			storagedoc( transaction->createDocument( path));

		strus::Index lastPos = (doc.terms().empty())?0:doc.terms()[ doc.terms().size()-1].pos();

		// Define hardcoded document attributes:
		storagedoc->setAttribute(
			strus::Constants::attribute_docid(), path);

		// Define hardcoded document metadata, if known:
		if (hasDoclenAttribute)
		{
			storagedoc->setMetaData(
				strus::Constants::metadata_doclen(),
				strus::ArithmeticVariant( lastPos));
		}
		// Define all term occurrencies:
		std::vector<strus::analyzer::Term>::const_iterator
			ti = doc.terms().begin(), te = doc.terms().end();
		for (; ti != te; ++ti)
		{
			storagedoc->addTermOccurrence(
				ti->type(), ti->value(), ti->pos(), 0.0/*weight*/);
		}

		// Define all attributes extracted from the document analysis:
		std::vector<strus::analyzer::Attribute>::const_iterator
			ai = doc.attributes().begin(), ae = doc.attributes().end();
		for (; ai != ae; ++ai)
		{
			storagedoc->setAttribute( ai->name(), ai->value());
		}

		// Define all metadata elements extracted from the document analysis:
		std::vector<strus::analyzer::MetaData>::const_iterator
			mi = doc.metadata().begin(), me = doc.metadata().end();
		for (; mi != me; ++mi)
		{
			strus::ArithmeticVariant value( mi->value());
			storagedoc->setMetaData( mi->name(), value);
		}

		storagedoc->done();

		// Notify progress:
		if (notifyProgress && ++loopCount == notifyProgress)
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
	strus::StorageTransactionInterface* transaction,
	const strus::AnalyzerInterface* analyzer,
	const std::string& path,
	bool hasDoclenAttribute)
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
		if (processDocument(
			transaction, analyzer, file, hasDoclenAttribute))
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
			if (!processDirectory( transaction, analyzer, subdir, hasDoclenAttribute))
			{
				std::cerr << "ERROR failed to process subdirectory '" << subdir << "'" << std::endl;
				return false;
			}
		}
	}
	return true;
}


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions( argc_, argv_, 2, "h,help", "p,notify-progress");
		if (opt("h","help")) printUsageAndExit = true;

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
		std::cerr << "usage: strusInsert <config> <program> <docpath>" << std::endl;
		std::cerr << "<config>  = storage configuration string" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getStorageConfigDescription(
						strus::CmdCreateStorageClient));
		std::cerr << "<program> = path of analyzer program" << std::endl;
		std::cerr << "<docpath> = path of document or directory to insert" << std::endl;
		return rt;
	}
	try
	{
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( opt[1], analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << opt[1] << " (file system error " << ec << ")" << std::endl;
			return 2;
		}
		boost::scoped_ptr<strus::StorageInterface>
			storage( strus::createStorageClient( opt[0]));

		boost::scoped_ptr<strus::MetaDataReaderInterface>
			metadata( storage->createMetaDataReader());
		bool hasDoclenAttribute
			= metadata->hasElement( strus::Constants::metadata_doclen());

		std::string tokenMinerSource;
		boost::scoped_ptr<strus::TokenMinerFactory>
			minerfac( strus::createTokenMinerFactory( tokenMinerSource));

		boost::scoped_ptr<strus::AnalyzerInterface>
			analyzer( strus::createAnalyzer( *minerfac, analyzerProgramSource));

		boost::scoped_ptr<strus::StorageTransactionInterface>
			transaction( storage->createTransaction());

		std::string path( opt[2]);
		if (strus::isDir( path))
		{
			if (!processDirectory( transaction.get(), analyzer.get(), path, hasDoclenAttribute))
			{
				std::cerr << "ERROR failed processing of directory '" << path << "'" << std::endl;
				return 3;
			}
		}
		else if (strus::isFile( path))
		{
			if (processDocument( transaction.get(), analyzer.get(), path, hasDoclenAttribute))
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
			return 5;
		}
		transaction->commit();

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
		return 6;
	}
	catch (const std::exception& e)
	{
		std::cerr << "EXCEPTION " << e.what() << std::endl;
		return 7;
	}
	return 0;
}


