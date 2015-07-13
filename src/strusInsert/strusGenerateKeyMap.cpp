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
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/index.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
#include "fileCrawler.hpp"
#include "keyMapGenProcessor.hpp"
#include "thread.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 10,
				"h,help", "t,threads:", "u,unit:",
				"n,results:", "v,version", "m,module:", "x,extension:",
				"s,segmenter:", "M,moduledir:", "R,resourcedir:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus analyzer version " << STRUS_ANALYZER_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
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

		if (printUsageAndExit)
		{
			std::cout << "usage: strusGenerateKeyMap [options] <program> <docpath>" << std::endl;
			std::cout << "<program> = path of analyzer program" << std::endl;
			std::cout << "<docpath> = path of document or directory to insert" << std::endl;
			std::cout << "description: Dumps a list of terms as result of document" << std::endl;
			std::cout << "    anaylsis of a file or directory. The dump can be loaded by" << std::endl;
			std::cout << "    the storage on startup to create a map of frequently used terms." << std::endl;
			std::cout << "options:" << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "   Print this usage and do nothing else" << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    Print the program version and do nothing else" << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    Load components from module <MOD>" << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    Search modules to load first in <DIR>" << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    Search resource files for analyzer first in <DIR>" << std::endl;
			std::cout << "-s|--segmenter <NAME>" << std::endl;
			std::cout << "    Use the document segmenter with name <NAME> (default textwolf XML)" << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    Grab the files with extension <EXT> (default \".xml\")" << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    Set <N> as number of threads to use"  << std::endl;
			std::cout << "-u|--unit <N>" << std::endl;
			std::cout << "    Set <N> as number of files processed per iteration (default 1000)" << std::endl;
			std::cout << "-n|--results <N>" << std::endl;
			std::cout << "    Set <N> as number of elements in the key map generated" << std::endl;
			return rt;
		}

		// [1] Build objects:
		unsigned int nofThreads = opt.asUint( "threads");
		unsigned int unitSize = 1000;
		if (opt( "unit"))
		{
			unitSize = opt.asUint( "unit");
		}
		unsigned int nofResults = opt.asUint( "results");
		std::string fileext = ".xml";
		std::string segmenter;
		if (opt( "segmenter"))
		{
			segmenter = opt[ "segmenter"];
		}
		if (opt( "extension"))
		{
			fileext = opt[ "extension"];
			if (fileext.size() && fileext[0] != '.')
			{
				fileext = std::string(".") + fileext;
			}
		}
		std::string analyzerprg = opt[0];
		std::string datapath = opt[1];

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

		// Create objects for keymap generation:
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface>
			analyzerBuilder( moduleLoader->createAnalyzerObjectBuilder());
		strus::utils::ScopedPtr<strus::DocumentAnalyzerInterface>
			analyzer( analyzerBuilder->createDocumentAnalyzer( segmenter));
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();

		// [2] Load analyzer program(s):
		strus::AnalyzerMap analyzerMap( analyzerBuilder.get());
		analyzerMap.defineProgram( "text/xml", "", segmenter, analyzerprg);

		strus::KeyMapGenResultList resultList;
		strus::FileCrawler* fileCrawler
			= new strus::FileCrawler(
				datapath, unitSize, nofThreads*5+5, fileext);

		// [3] Start threads:
		std::auto_ptr< strus::Thread< strus::FileCrawler> >
			fileCrawlerThread(
				new strus::Thread< strus::FileCrawler >( fileCrawler,
					"filecrawler"));
		std::cout.flush();
		fileCrawlerThread->start();

		if (nofThreads == 0)
		{
			strus::KeyMapGenProcessor processor(
				textproc, analyzerMap, &resultList, fileCrawler);
			processor.run();
		}
		else
		{
			std::auto_ptr< strus::ThreadGroup< strus::KeyMapGenProcessor > >
				processors( new strus::ThreadGroup<strus::KeyMapGenProcessor>( "keymapgen"));

			for (unsigned int ti = 0; ti<nofThreads; ++ti)
			{
				processors->start(
					new strus::KeyMapGenProcessor(
						textproc, analyzerMap, &resultList, fileCrawler));
			}
			processors->wait_termination();
		}
		fileCrawlerThread->wait_termination();

		// [3] Final merge:
		std::cerr << std::endl << "merging results:" << std::endl;
		resultList.printKeyOccurrenceList( std::cout, nofResults);
		
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


