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
#include "strus/textProcessorInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/reference.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>

int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 4,
				"h,help", "v,version", "m,module:", "s,segmenter:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
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

		if (printUsageAndExit)
		{
			std::cerr << "usage: strusAnalyze [options] <program> <document>" << std::endl;
			std::cerr << "<program>   = path of analyzer program" << std::endl;
			std::cerr << "<document>  = path of document to analyze" << std::endl;
			std::cerr << "description: Analyzes a document and dumps the result to stdout." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "   Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-s|--segmenter <NAME>" << std::endl;
			std::cerr << "    Use the document segmenter with name <NAME> (default textwolf XML)" << std::endl;
			return rt;
		}
		std::string analyzerprg = opt[0];
		std::string docpath = opt[1];
		std::string segmenter;
		if (opt( "segmenter"))
		{
			segmenter = opt[ "segmenter"];
		}
		// Create objects for analyzer:
		std::auto_ptr<strus::DocumentAnalyzerInterface>
			analyzer( builder.createDocumentAnalyzer( segmenter));

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << opt[1] << " (file system error " << ec << ")" << std::endl;
			return 4;
		}
		strus::loadDocumentAnalyzerProgram( *analyzer, analyzerProgramSource);

		std::auto_ptr<strus::DocumentAnalyzerInstanceInterface> analyzerInstance;
		if (docpath == "-")
		{
			analyzerInstance.reset( analyzer->createDocumentAnalyzerInstance( std::cin));
		}
		else
		{
			std::ifstream documentFile;
			try 
			{
				documentFile.open( docpath.c_str(), std::fstream::in);
			}
			catch (const std::ifstream::failure& err)
			{
				throw std::runtime_error( std::string( "failed to read file to analyze '") + docpath + "': " + err.what());
			}
			catch (const std::runtime_error& err)
			{
				throw std::runtime_error( std::string( "failed to read file to analyze '") + docpath + "': " + err.what());
			}
			analyzerInstance.reset( analyzer->createDocumentAnalyzerInstance( documentFile));
		}
		while (analyzerInstance->hasMore())
		{
			strus::analyzer::Document doc
				= analyzerInstance->analyzeNext();

			if (!doc.subDocumentTypeName().empty())
			{
				std::cout << "-- document " << doc.subDocumentTypeName() << std::endl;
			}
			std::vector<strus::analyzer::Term>::const_iterator
				ti = doc.searchIndexTerms().begin(), te = doc.searchIndexTerms().end();

			std::cout << std::endl << "search index terms:" << std::endl;
			for (; ti != te; ++ti)
			{
				std::cout << ti->pos()
					  << " " << ti->type()
					  << " '" << ti->value() << "'"
					  << std::endl;
			}

			std::vector<strus::analyzer::Term>::const_iterator
				fi = doc.forwardIndexTerms().begin(), fe = doc.forwardIndexTerms().end();

			std::cout << std::endl << "forward index terms:" << std::endl;
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


