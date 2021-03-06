/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/lib/filecrawler.hpp"
#include "strus/fileCrawlerInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/storage/index.hpp"
#include "strus/reference.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/thread.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "private/documentAnalyzer.hpp"
#include "private/programLoader.hpp"
#include "keyMapGenProcessor.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <memory>

static std::string getFileArg( const std::string& filearg, strus::ModuleLoaderInterface* moduleLoader)
{
	std::string programFileName = filearg;
	std::string programDir;
	int ec;

	if (strus::isExplicitPath( programFileName))
	{
		ec = strus::getParentPath( programFileName, programDir);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file directory from explicit path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		moduleLoader->addResourcePath( programDir);
	}
	else
	{
		std::string filedir;
		std::string filenam;
		ec = strus::getFileName( programFileName, filenam);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file name from absolute path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		ec = strus::getParentPath( programFileName, filedir);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file directory from absolute path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		programDir = filedir;
		programFileName = filenam;
		moduleLoader->addResourcePath( programDir);
	}
	return programFileName;
}

int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::DebugTraceInterface* dbgtrace = strus::createDebugTrace_standard( 2);
	if (!dbgtrace)
	{
		std::cerr << _TXT("failed to create debug trace") << std::endl;
		return -1;
	}
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2, dbgtrace/*passed with ownership*/));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	try
	{
		bool printUsageAndExit = false;
		strus::ProgramOptions opt(
				errorBuffer.get(), argc_, argv_, 14,
				"h,help",  "v,version", "license",
				"G,debug:", "t,threads:", "u,unit:",
				"n,results:","m,module:", "x,extension:",
				"s,segmenter:", "C,contenttype:", "M,moduledir:", "R,resourcedir:",
				"T,trace:");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}

		int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
		}
		if (opt( "help")) printUsageAndExit = true;

		// Enable debugging selected with option 'debug':
		{
			std::vector<std::string> dbglist = opt.list( "debug");
			std::vector<std::string>::const_iterator gi = dbglist.begin(), ge = dbglist.end();
			for (; gi != ge; ++gi)
			{
				if (!dbgtrace->enable( *gi))
				{
					throw strus::runtime_error(_TXT("failed to enable debug '%s'"), gi->c_str());
				}
			}
		}

		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw std::runtime_error( _TXT("failed to create module loader"));
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
		if (opt("license"))
		{
			std::vector<std::string> licenses_3rdParty = moduleLoader->get3rdPartyLicenseTexts();
			std::vector<std::string>::const_iterator ti = licenses_3rdParty.begin(), te = licenses_3rdParty.end();
			if (ti != te) std::cout << _TXT("3rd party licenses:") << std::endl;
			for (; ti != te; ++ti)
			{
				std::cout << *ti << std::endl;
			}
			std::cout << std::endl;
			if (!printUsageAndExit) return 0;
		}
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
			std::vector<std::string> versions_3rdParty = moduleLoader->get3rdPartyVersionTexts();
			std::vector<std::string>::const_iterator vi = versions_3rdParty.begin(), ve = versions_3rdParty.end();
			if (vi != ve) std::cout << _TXT("3rd party versions:") << std::endl;
			for (; vi != ve; ++vi)
			{
				std::cout << *vi << std::endl;
			}
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
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Issue debug messages for component <COMP> to stderr") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-s|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME>") << std::endl;
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of all documents processed.") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("Grab only the files with extension <EXT> (default all files)") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of threads to use") << std::endl;
			std::cout << "-u|--unit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files processed per iteration (default 1000)") << std::endl;
			std::cout << "-n|--results <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of elements in the key map generated") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			return rt;
		}

		// Declare trace proxy objects:
		typedef strus::Reference<strus::TraceProxy> TraceReference;
		std::vector<TraceReference> trace;
		if (opt("trace"))
		{
			std::vector<std::string> tracecfglist( opt.list("trace"));
			std::vector<std::string>::const_iterator ti = tracecfglist.begin(), te = tracecfglist.end();
			for (; ti != te; ++ti)
			{
				trace.push_back( new strus::TraceProxy( moduleLoader.get(), *ti, errorBuffer.get()));
			}
		}
		// [1] Build objects:
		unsigned int unitSize = 1000;
		if (opt( "unit"))
		{
			unitSize = opt.asUint( "unit");
		}
		unsigned int nofResults = opt.asUint( "results");
		std::string fileext = "";
		std::string segmenterName;
		std::string contenttype;

		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
		}
		if (opt( "segmenter"))
		{
			segmenterName = opt[ "segmenter"];
		}
		if (opt( "extension"))
		{
			fileext = opt[ "extension"];
			if (fileext.size() && fileext[0] != '.')
			{
				fileext = std::string(".") + fileext;
			}
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
		std::string programFileName = getFileArg( opt[0], moduleLoader.get());
		std::string datapath = opt[1];

		// Create root objects:
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface>
			analyzerBuilder( moduleLoader->createAnalyzerObjectBuilder());
		if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create analyzer object builder"));

		// Create proxy objects if tracing enabled:
		{
			std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
			for (; ti != te; ++ti)
			{
				strus::AnalyzerObjectBuilderInterface* aproxy = (*ti)->createProxy( analyzerBuilder.get());
				analyzerBuilder.release();
				analyzerBuilder.reset( aproxy);
			}
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}

		// Create objects for keymap generation:
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw std::runtime_error( _TXT("failed to get text processor"));

		// Try to determine document class:
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			documentClass = strus::parse_DocumentClass( contenttype, errorBuffer.get());
			if (!documentClass.defined() && errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to parse document class"));
			}
		}
		else if (strus::isFile( datapath))
		{
			strus::InputStream input( datapath);
			char hdrbuf[ 4096];
			std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
			if (input.error())
			{
				throw strus::runtime_error( _TXT("failed to read document file '%s': %s"), datapath.c_str(), ::strerror(input.error())); 
			}
			if (!textproc->detectDocumentClass( documentClass, hdrbuf, hdrsize, hdrsize < sizeof(hdrbuf)))
			{
				throw strus::runtime_error( "%s",  _TXT("failed to detect document class")); 
			}
		}

		// [2] Load analyzer program(s):
		strus::DocumentAnalyzer analyzerMap( analyzerBuilder.get(), documentClass, segmenterName, programFileName, errorBuffer.get());

		// [3] Start threads:
		strus::KeyMapGenResultList resultList;
		strus::local_ptr<strus::FileCrawlerInterface> fileCrawler( strus::createFileCrawlerInterface( datapath, unitSize, fileext, errorBuffer.get()));
		if (!fileCrawler.get()) throw std::runtime_error( errorBuffer->fetchError());

		if (nofThreads == 0)
		{
			strus::KeyMapGenProcessor processor(
				textproc, &analyzerMap, documentClass,
				&resultList, fileCrawler.get(), errorBuffer.get());
			processor.run();
		}
		else
		{
			std::vector<strus::Reference<strus::KeyMapGenProcessor> > processorList;
			for (int ti = 0; ti<nofThreads; ++ti)
			{
				processorList.push_back(
					new strus::KeyMapGenProcessor(
						textproc, &analyzerMap, documentClass, 
						&resultList, fileCrawler.get(), errorBuffer.get()));
			}
			{
				std::vector<strus::Reference<strus::thread> > threadGroup;
				for (int ti=0; ti<nofThreads; ++ti)
				{
					strus::KeyMapGenProcessor* tc = processorList[ ti].get();
					strus::Reference<strus::thread> th( new strus::thread( &strus::KeyMapGenProcessor::run, tc));
					threadGroup.push_back( th);
				}
				std::vector<strus::Reference<strus::thread> >::iterator gi = threadGroup.begin(), ge = threadGroup.end();
				for (; gi != ge; ++gi) (*gi)->join();
			}
		}
		// [3] Final merge:
		std::cerr << std::endl << _TXT("merging results:") << std::endl;
		resultList.printKeyOccurrenceList( std::cout, nofResults);
		
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("unhandled error in generate key map"));
		}
		std::cerr << _TXT("done.") << std::endl;
		if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
		{
			std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
		}
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
		return -2;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer->fetchError();
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << e.what() << ": " << errormsg << std::endl;
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
	if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
	{
		std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
	}
	return -1;
}


