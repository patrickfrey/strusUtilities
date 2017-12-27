/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/reference.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/segmenterInstanceInterface.hpp"
#include "strus/segmenterContextInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/segmenterOptions.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include "private/programOptions.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <cerrno>
#include <cstdio>
#include <memory>

std::string escapeEndOfLine( const std::string& str)
{
	std::string rt;
	std::string::const_iterator si = str.begin(), se = str.end();
	for (;si != se; ++si)
	{
		if (*si == '\r')
		{}
		else if (*si == '\n')
		{
			rt.push_back( ' ');
		}
		else
		{
			rt.push_back( *si);
		}
	}
	return rt;
}

int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
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
				argc, argv, 14,
				"h,help", "v,version", "license",
				"g,segmenter:", "C,contenttype:", "e,expression:",
				"m,module:", "M,moduledir:", "P,prefix:", "i,index",
				"p,position", "q,quot:", "E,esceol", "T,trace:");
		if (opt( "help")) printUsageAndExit = true;
		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error( "%s", _TXT("failed to create module loader"));
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
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME> (default textwolf XML)") << std::endl;
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of the document processed.") << std::endl;
			std::cout << "-e|--expression <EXPR>" << std::endl;
			std::cout << "    " << _TXT("Use the expression <EXPR> to select document contents.") << std::endl;
			std::cout << "    " << _TXT("Select all content if nothing specified)") << std::endl;
			std::cout << "-i|--index" << std::endl;
			std::cout << "    " << _TXT("Print the indices of the expressions matching as prefix with ':'") << std::endl;
			std::cout << "-p|--position" << std::endl;
			std::cout << "    " << _TXT("Print the positions of the expressions matching as prefix") << std::endl;
			std::cout << "-q|--quot <STR>" << std::endl;
			std::cout << "    " << _TXT("Use the string <STR> as quote for the result (default \"\'\")") << std::endl;
			std::cout << "-P|--prefix <STR>" << std::endl;
			std::cout << "    " << _TXT("Use the string <STR> as prefix for the result") << std::endl;
			std::cout << "-E|--esceol" << std::endl;
			std::cout << "    " << _TXT("Escape end of line with space") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string docpath = opt[0];
		std::string segmenterName;
		std::string contenttype;
		std::string resultPrefix;
		std::string resultQuot;
		bool printIndices = opt( "index");
		bool printPositions = opt( "position");
		bool doEscapeEndOfLine = opt( "esceol");
		if (opt( "prefix"))
		{
			resultPrefix = opt[ "prefix"];
		}
		if (opt( "quot"))
		{
			resultQuot = opt[ "quot"];
		}
		if (opt( "segmenter"))
		{
			segmenterName = opt[ "segmenter"];
		}
		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
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

		// Create objects for segmenter:
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface>
			analyzerBuilder( moduleLoader->createAnalyzerObjectBuilder());
		if (!analyzerBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create analyzer object builder"));

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* aproxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( aproxy);
		}

		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error( "%s", _TXT("failed to get text processor"));

		// Load the document and get its properties:
		strus::InputStream input( docpath);
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty() && !strus::parseDocumentClass( documentClass, contenttype, errorBuffer.get()))
		{
			throw strus::runtime_error( "%s", _TXT("failed to parse document class"));
		}
		if (!documentClass.defined())
		{
			char hdrbuf[ 4096];
			std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
			if (input.error())
			{
				throw strus::runtime_error( _TXT("failed to read document file '%s': %s"), docpath.c_str(), ::strerror(input.error())); 
			}
			if (!textproc->detectDocumentClass( documentClass, hdrbuf, hdrsize, hdrsize < sizeof(hdrbuf)))
			{
				throw strus::runtime_error( "%s", _TXT("failed to detect document class")); 
			}
		}
		// Create the document segmenter either defined by the document class or by content or by the name specified:
		const strus::SegmenterInterface* segmenterType;
		strus::analyzer::SegmenterOptions segmenteropts;
		if (segmenterName.empty())
		{
			segmenterType = textproc->getSegmenterByMimeType( documentClass.mimeType());
			if (!segmenterType) throw strus::runtime_error(_TXT("failed to find document segmenter specified by MIME type '%s'"), documentClass.mimeType().c_str());
			if (!documentClass.scheme().empty())
			{
				segmenteropts = textproc->getSegmenterOptions( documentClass.scheme());
			}
		}
		else
		{
			segmenterType = textproc->getSegmenterByName( segmenterName);
			if (!segmenterType) throw strus::runtime_error(_TXT("failed to find document segmenter specified by name '%s'"), segmenterName.c_str());
		}
		strus::local_ptr<strus::SegmenterInstanceInterface> segmenter( segmenterType->createInstance( segmenteropts));
		if (!segmenter.get()) throw strus::runtime_error( "%s", _TXT("failed to segmenter instance"));

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
			segmenter->defineSelectorExpression( 0, "");
		}

		// Create the segmenter
		strus::local_ptr<strus::SegmenterContextInterface> segmenterContext( segmenter->createContext( documentClass));
		if (!segmenterContext.get()) throw strus::runtime_error( "%s", _TXT("failed to segmenter context"));

		// Process the document:
		enum {SegmenterBufSize=8192};
		char buf[ SegmenterBufSize];
		bool eof = false;

		while (!eof)
		{
			std::size_t readsize = input.read( buf, sizeof(buf));
			if (readsize < sizeof(buf))
			{
				if (input.error())
				{
					throw strus::runtime_error( _TXT("failed to read document file '%s': %s"), docpath.c_str(), ::strerror(input.error())); 
				}
				eof = true;
			}
			segmenterContext->putInput( buf, readsize, eof);

			// Segment the input:
			int segid;
			strus::SegmenterPosition segpos;
			const char* segdata;
			std::size_t segsize;
			while (segmenterContext->getNext( segid, segpos, segdata, segsize))
			{
				if (!resultPrefix.empty())
				{
					std::cout << resultPrefix;
				}
				if (printIndices)
				{
					std::cout << segid << ": ";
				}
				if (printPositions)
				{
					std::cout << segpos << " ";
				}
				if (doEscapeEndOfLine)
				{
					std::cout << resultQuot << escapeEndOfLine( std::string(segdata,segsize))
						<< resultQuot << std::endl;
				}
				else
				{
					std::cout << resultQuot << std::string(segdata,segsize)
						<< resultQuot << std::endl;
				}
			}
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("unhandled error in segment document"));
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


