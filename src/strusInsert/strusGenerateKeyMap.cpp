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
#include "strus/lib/error.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/index.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
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
	strus::ErrorBufferInterface* errorBuffer = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc_, argv_, 10,
				"h,help", "t,threads:", "u,unit:",
				"n,results:", "v,version", "m,module:", "x,extension:",
				"s,segmenter:", "M,moduledir:", "R,resourcedir:");

		unsigned int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
		}
		errorBuffer = strus::createErrorBuffer_standard( stderr, nofThreads+2);
		if (!errorBuffer) throw strus::runtime_error( _TXT("failed to create error buffer"));

		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 2)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));
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
				if (!moduleLoader->loadModule( *mi))
				{
					throw strus::runtime_error(_TXT("error failed to load module %s"), mi->c_str());
				}
			}
		}

		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusGenerateKeyMap [options] <program> <docpath>" << std::endl;
			std::cout << "<program> = " << _TXT("path of analyzer program  or analyzer map program") << std::endl;
			std::cout << "<docpath> = " << _TXT("path of document or directory to insert") << std::endl;
			std::cout << _TXT("description: Dumps a list of terms as result of document") << std::endl;
			std::cout << "    " << _TXT("anaylsis of a file or directory. The dump can be loaded by") << std::endl;
			std::cout << "    " << _TXT("the storage on startup to create a map of frequently used terms.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-s|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME> (default textwolf XML)") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("Grab only the files with extension <EXT> (default all files)") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of threads to use") << std::endl;
			std::cout << "-u|--unit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files processed per iteration (default 1000)") << std::endl;
			std::cout << "-n|--results <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of elements in the key map generated") << std::endl;
			return rt;
		}

		// [1] Build objects:
		unsigned int unitSize = 1000;
		if (opt( "unit"))
		{
			unitSize = opt.asUint( "unit");
		}
		unsigned int nofResults = opt.asUint( "results");
		std::string fileext = "";
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
		std::string resourcepath;
		if (!strus::getParentPath( analyzerprg, resourcepath))
		{
			throw strus::runtime_error( _TXT("failed to evaluate resource path"));
		}
		moduleLoader->addResourcePath( resourcepath);

		// Create objects for keymap generation:
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface>
			analyzerBuilder( moduleLoader->createAnalyzerObjectBuilder());
		if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create analyzer object builder"));
		strus::utils::ScopedPtr<strus::DocumentAnalyzerInterface>
			analyzer( analyzerBuilder->createDocumentAnalyzer( segmenter));
		if (!analyzer.get()) throw strus::runtime_error(_TXT("failed to create document analyzer"));
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor"));

		// [2] Load analyzer program(s):
		strus::AnalyzerMap analyzerMap( analyzerBuilder.get(), errorBuffer);
		analyzerMap.defineProgram( ""/*scheme*/, segmenter, analyzerprg);

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
				textproc, analyzerMap, &resultList, fileCrawler, errorBuffer);
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
						textproc, analyzerMap, &resultList, fileCrawler, errorBuffer));
			}
			processors->wait_termination();
		}
		fileCrawlerThread->wait_termination();

		// [3] Final merge:
		std::cerr << std::endl << _TXT("merging results:") << std::endl;
		resultList.printKeyOccurrenceList( std::cout, nofResults);
		
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in generate key map"));
		}
		std::cerr << _TXT("done") << std::endl;
		delete errorBuffer;
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer?errorBuffer->fetchError():0;
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << errormsg << ": " << e.what() << std::endl;
		}
		else
		{
			std::cerr << _TXT("ERROR ") << e.what() << std::endl;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << _TXT("EXCEPTION ") << e.what() << std::endl;
	}
	delete errorBuffer;
	return -1;
}


