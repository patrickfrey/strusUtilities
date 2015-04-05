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
#include "private/inputStream.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>

struct TermOrder
{
	bool operator()( const strus::analyzer::Term& aa, const strus::analyzer::Term& bb)
	{
		if (aa.pos() != bb.pos()) return (aa.pos() < bb.pos());
		int cmp;
		cmp = aa.type().compare( bb.type());
		if (cmp != 0) return (cmp < 0);
		cmp = aa.value().compare( bb.value());
		if (cmp != 0) return (cmp < 0);
		return false;
	}
};

int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 6,
				"h,help", "v,version", "m,module:",
				"s,segmenter:", "M,moduledir:", "R,resourcedir:");
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

		if (printUsageAndExit)
		{
			std::cerr << "usage: strusAnalyze [options] <program> <document>" << std::endl;
			std::cerr << "<program>   = path of analyzer program" << std::endl;
			std::cerr << "<document>  = path of document to analyze ('-' for stdin)" << std::endl;
			std::cerr << "description: Analyzes a document and dumps the result to stdout." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "   Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			std::cerr << "-R|--resourcedir <DIR>" << std::endl;
			std::cerr << "    Search resource files for analyzer first in <DIR>" << std::endl;
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

		// Set paths for locating resources:
		if (opt("resourcedir"))
		{
			std::vector<std::string> pathlist( opt.list("resourcedir"));
			std::vector<std::string>::const_iterator
				pi = pathlist.begin(), pe = pathlist.end();
			for (; pi != pe; ++pi)
			{
				moduleLoader->addResourcePath( *pi);
			}
		}
		moduleLoader->addResourcePath( strus::getParentPath( analyzerprg));

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
			std::cerr << "ERROR failed to load analyzer program " << analyzerprg << " (file system error " << ec << ")" << std::endl;
			return 4;
		}
		strus::loadDocumentAnalyzerProgram( *analyzer, analyzerProgramSource);

		// Load the document:
		strus::InputStream input( docpath);
		std::auto_ptr<strus::DocumentAnalyzerInstanceInterface>
			analyzerInstance( analyzer->createInstance());

		enum {AnalyzerBufSize=8192};
		char buf[ AnalyzerBufSize];
		bool eof = false;

		while (!eof)
		{
			std::size_t readsize = input.read( buf, sizeof(buf));
			if (!readsize)
			{
				eof = true;
				continue;
			}
			analyzerInstance->putInput( buf, readsize, readsize != AnalyzerBufSize);

			// Analyze the document and print the result:
			strus::analyzer::Document doc;
			while (analyzerInstance->analyzeNext( doc))
			{
				if (!doc.subDocumentTypeName().empty())
				{
					std::cout << "-- document " << doc.subDocumentTypeName() << std::endl;
				}
				std::vector<strus::analyzer::Term> itermar = doc.searchIndexTerms();
				std::sort( itermar.begin(), itermar.end(), TermOrder());
	
				std::vector<strus::analyzer::Term>::const_iterator
					ti = itermar.begin(), te = itermar.end();
	
				std::cout << std::endl << "search index terms:" << std::endl;
				for (; ti != te; ++ti)
				{
					std::cout << ti->pos()
						  << " " << ti->type()
						  << " '" << ti->value() << "'"
						  << std::endl;
				}
	
				std::vector<strus::analyzer::Term> ftermar = doc.forwardIndexTerms();
				std::sort( ftermar.begin(), ftermar.end(), TermOrder());
	
				std::vector<strus::analyzer::Term>::const_iterator
					fi = ftermar.begin(), fe = ftermar.end();
	
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


