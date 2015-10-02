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
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/documentClass.hpp"
#include "strus/reference.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
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
	strus::ErrorBufferInterface* errorBuffer = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		errorBuffer = strus::createErrorBuffer_standard( stderr, 2);
		if (!errorBuffer) throw strus::runtime_error( _TXT("failed to create error buffer"));

		opt = strus::ProgramOptions(
				argc, argv, 7,
				"h,help", "v,version", "m,module:", "r,rpc:",
				"g,segmenter:", "M,moduledir:", "R,resourcedir:");
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
				std::cerr << _TXT("error too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
			{
				std::cerr << _TXT("error too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface>
				moduleLoader( strus::createModuleLoader( errorBuffer));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
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
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--module", "--rpc");
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->loadModule( *mi);
			}
		}

		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusAnalyze [options] <program> <document>" << std::endl;
			std::cout << "<program>   = " << _TXT("path of analyzer program") << std::endl;
			std::cout << "<document>  = " << _TXT("path of document to analyze ('-' for stdin)") << std::endl;
			std::cout << _TXT("description: Analyzes a document and dumps the result to stdout.") << std::endl;
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
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME> (default textwolf XML)") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
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
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--resourcedir", "--rpc");
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
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;

		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
		}
		std::auto_ptr<strus::DocumentAnalyzerInterface>
			analyzer( analyzerBuilder->createDocumentAnalyzer( segmenter));

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			throw strus::runtime_error( _TXT("failed to load analyzer program %s (file system error %u)"), analyzerprg.c_str(), ec);
		}
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		strus::loadDocumentAnalyzerProgram( *analyzer, textproc, analyzerProgramSource);

		// Load the document and get its properties:
		strus::InputStream input( docpath);
		char hdrbuf[ 1024];
		std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
		strus::DocumentClass dclass;
		if (!textproc->detectDocumentClass( dclass, hdrbuf, hdrsize))
		{
			throw strus::runtime_error( _TXT("failed to detect document class")); 
		}
		std::auto_ptr<strus::DocumentAnalyzerContextInterface>
			analyzerContext( analyzer->createContext( dclass));

		// Process the document:
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
			analyzerContext->putInput( buf, readsize, readsize != AnalyzerBufSize);

			// Analyze the document and print the result:
			strus::analyzer::Document doc;
			while (analyzerContext->analyzeNext( doc))
			{
				if (!doc.subDocumentTypeName().empty())
				{
					std::cout << "-- " << strus::utils::string_sprintf( _TXT("document type name %s"), doc.subDocumentTypeName().c_str()) << std::endl;
				}
				std::vector<strus::analyzer::Term> itermar = doc.searchIndexTerms();
				std::sort( itermar.begin(), itermar.end(), TermOrder());
	
				std::vector<strus::analyzer::Term>::const_iterator
					ti = itermar.begin(), te = itermar.end();
	
				std::cout << std::endl << _TXT("search index terms:") << std::endl;
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
	
				std::cout << std::endl << _TXT("forward index terms:") << std::endl;
				for (; fi != fe; ++fi)
				{
					std::cout << fi->pos()
						  << " " << fi->type()
						  << " '" << fi->value() << "'"
						  << std::endl;
				}
	
				std::vector<strus::analyzer::MetaData>::const_iterator
					mi = doc.metadata().begin(), me = doc.metadata().end();
	
				std::cout << std::endl << _TXT("metadata:") << std::endl;
				for (; mi != me; ++mi)
				{
					std::cout << mi->name()
						  << " '" << mi->value() << "'"
						  << std::endl;
				}
	
				std::vector<strus::analyzer::Attribute>::const_iterator
					ai = doc.attributes().begin(), ae = doc.attributes().end();
		
				std::cout << std::endl << _TXT("attributes:") << std::endl;
				for (; ai != ae; ++ai)
				{
					std::cout << ai->name()
						  << " '" << ai->value() << "'"
						  << std::endl;
				}
			}
		}
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



