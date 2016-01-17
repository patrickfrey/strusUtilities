/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2015 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/documentClass.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/segmenterInstanceInterface.hpp"
#include "strus/segmenterContextInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/reference.hpp"
#include "strus/analyzer/term.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/inputStream.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
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
	std::auto_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 9,
				"h,help", "v,version", "s,segmenter:", "e,expression:",
				"m,module:", "M,moduledir:",
				"i,index", "p,position", "q,quot:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
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
			std::cout << _TXT("usage:") << " strusSegment [options] <document>" << std::endl;
			std::cout << "<document>  = " << _TXT("path to document to segment ('-' for stdin)") << std::endl;
			std::cout << _TXT("description: Segments a document with the expressions (-e) specified") << std::endl;
			std::cout << "             " << _TXT("and dumps the resulting segments to stdout.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-s|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME> (default textwolf XML)") << std::endl;
			std::cout << "-e|--expression <EXPR>" << std::endl;
			std::cout << "    " << _TXT("Use the expression <EXPR> to select documents (default '//()')") << std::endl;
			std::cout << "-i|--index" << std::endl;
			std::cout << "    " << _TXT("Print the indices of the expressions matching as prefix with ':'") << std::endl;
			std::cout << "-p|--position" << std::endl;
			std::cout << "    " << _TXT("Print the positions of the expressions matching as prefix") << std::endl;
			std::cout << "-q|--quot <STR>" << std::endl;
			std::cout << "    " << _TXT("Use the string <STR> as quote for the result (default \"\'\")") << std::endl;
			return rt;
		}
		std::string docpath = opt[0];
		std::string segmenterName;
		bool printIndices = opt( "index");
		bool printPositions = opt( "position");
		std::string resultQuot = "'";
		if (opt( "quot"))
		{
			resultQuot = opt[ "quot"];
		}
		if (opt( "segmenter"))
		{
			segmenterName = opt[ "segmenter"];
		}
		// Create objects for segmenter:
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface>
			analyzerBuilder( moduleLoader->createAnalyzerObjectBuilder());
		if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create analyzer object builder"));
		std::auto_ptr<strus::SegmenterInterface>
			segmentertype( analyzerBuilder->createSegmenter( segmenterName));
		if (!segmentertype.get()) throw strus::runtime_error(_TXT("failed to segmenter interface"));
		std::auto_ptr<strus::SegmenterInstanceInterface>
			segmenter( segmentertype->createInstance());
		if (!segmenter.get()) throw strus::runtime_error(_TXT("failed to segmenter instance"));
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor"));

		// Load expressions:
		if (opt("expression"))
		{
			std::vector<std::string> exprlist( opt.list("expression"));
			std::vector<std::string>::const_iterator ei = exprlist.begin(), ee = exprlist.end();
			for (int eidx=1; ei != ee; ++ei,++eidx)
			{
				segmenter->defineSelectorExpression( eidx, *ei);
			}
		}
		else
		{
			segmenter->defineSelectorExpression( 0, "//()");
		}

		// Load the document and get its properties:
		strus::InputStream input( docpath);
		char hdrbuf[ 1024];
		std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
		strus::DocumentClass dclass;
		if (!textproc->detectDocumentClass( dclass, hdrbuf, hdrsize))
		{
			throw strus::runtime_error(_TXT("failed to detect document class")); 
		}
		std::auto_ptr<strus::SegmenterContextInterface>
			segmenterContext( segmenter->createContext( dclass));
		if (!segmenterContext.get()) throw strus::runtime_error(_TXT("failed to segmenter context"));

		// Process the document:
		enum {SegmenterBufSize=8192};
		char buf[ SegmenterBufSize];
		bool eof = false;

		while (!eof)
		{
			std::size_t readsize = input.read( buf, sizeof(buf));
			if (!readsize)
			{
				eof = true;
				continue;
			}
			segmenterContext->putInput( buf, readsize, readsize != SegmenterBufSize);

			// Segment the input:
			int segid;
			strus::SegmenterPosition segpos;
			const char* segdata;
			std::size_t segsize;
			while (segmenterContext->getNext( segid, segpos, segdata, segsize))
			{
				if (printIndices)
				{
					std::cout << segid << ": ";
				}
				if (printPositions)
				{
					std::cout << segpos << " ";
				}
				std::cout << resultQuot << std::string(segdata,segsize)
						<< resultQuot << std::endl;
			}
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in segment document"));
		}
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
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
	return -1;
}


