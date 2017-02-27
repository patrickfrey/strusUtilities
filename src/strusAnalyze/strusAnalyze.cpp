/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
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
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/version.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/analyzer/segmenterOptions.hpp"
#include "strus/reference.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <map>

#undef STRUS_LOWLEVEL_DEBUG

struct TermOrder
{
	bool operator()( const strus::analyzer::Term& aa, const strus::analyzer::Term& bb)
	{
		if (aa.pos() != bb.pos()) return (aa.pos() < bb.pos());
		if (aa.len() != bb.len()) return (aa.len() < bb.len());
		int cmp;
		cmp = aa.type().compare( bb.type());
		if (cmp != 0) return (cmp < 0);
		cmp = aa.value().compare( bb.value());
		if (cmp != 0) return (cmp < 0);
		return false;
	}
};

static bool skipSpace( char const*& di)
{
	while (*di && (unsigned char)*di <= 32) ++di;
	return *di != '\0';
}

static void skipIdent( char const*& di)
{
	for (; *di && (((unsigned char)(*di|32) >= 'a' && (unsigned char)(*di|32) <= 'z') || *di == '_'); ++di){}
}

typedef std::map<std::string,std::string> DumpConfig;
typedef std::pair<std::string,std::string> DumpConfigElem;

static DumpConfigElem getNextDumpConfigElem( char const*& di)
{
	skipSpace( di);
	char const* de = di;
	skipIdent( de);
	std::string type( di, de-di);
	std::string value;
	di = de;
	skipSpace( di);
	if (*di == '=')
	{
		++di;
		skipSpace( di);
		if (*di == '\'' || *di == '\"')
		{
			char eb = *di++;
			char const* valstart = di;
			for (; *di && *di != eb; ++di){}
			value = strus::utils::unescape( std::string( valstart, di-valstart));
			if (*di) ++di;
			skipSpace( di);
		}
		else
		{
			char const* valstart = di;
			for (; *di && *di != ',' && (unsigned char)*di >= 32; ++di){}
			value.append( valstart, di-valstart);
			skipSpace( di);
		}
	}
	if (*di == ',')
	{
		++di;
	}
	else if (*di)
	{
		throw strus::runtime_error(_TXT("illegal token in dump configuration string at '%s'"), di);
	}
	return DumpConfigElem( type, value);
}


static void filterTerms( std::vector<strus::analyzer::Term>& termar, const DumpConfig& dumpConfig, const std::vector<strus::analyzer::Term>& inputtermar)
{
	std::vector<strus::analyzer::Term>::const_iterator
		ti = inputtermar.begin(), te = inputtermar.end();
	for (; ti != te; ++ti)
	{
		DumpConfig::const_iterator dci = dumpConfig.find( ti->type());
		if (dci != dumpConfig.end())
		{
			if (dci->second.empty())
			{
				termar.push_back( *ti);
			}
			else
			{
				termar.push_back( strus::analyzer::Term( ti->type(), dci->second, ti->pos(), ti->len()));
			}
		}
	}
}

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
				argc, argv, 11,
				"h,help", "v,version", "license", "m,module:",
				"M,moduledir:", "r,rpc:", "T,trace:", "R,resourcedir:",
				"g,segmenter:", "C,contenttype:", "D,dump:");
		if (opt( "help")) printUsageAndExit = true;
		std::auto_ptr<strus::ModuleLoaderInterface>
				moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));

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
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
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
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME>") << std::endl;
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of the document analyzed.") << std::endl;
			std::cout << "-D|--dump <DUMPCFG>" << std::endl;
			std::cout << "    " << _TXT("Dump ouput according <DUMPCFG>.") << std::endl;
			std::cout << "    " << _TXT("<DUMPCFG> is a comma separated list of types or type value assignments.") << std::endl;
			std::cout << "    " << _TXT("A type in <DUMPCFG> specifies the type to dump.") << std::endl;
			std::cout << "    " << _TXT("A value an optional replacement of the term value.") << std::endl;
			std::cout << "    " << _TXT("This kind of output is suitable for content analysis.") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string analyzerprg = opt[0];
		std::string docpath = opt[1];
		std::string segmentername;
		std::string contenttype;
		DumpConfig dumpConfig;
		bool doDump = false;
		if (opt( "segmenter"))
		{
			segmentername = opt[ "segmenter"];
		}
		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
		}
		if (opt( "dump"))
		{
			doDump = true;
			std::string ds = opt[ "dump"];
			char const* di = ds.c_str();
			while (skipSpace( di))
			{
				DumpConfigElem dt( getNextDumpConfigElem( di));
				dumpConfig.insert( dt);
#ifdef STRUS_LOWLEVEL_DEBUG
				if (dt.second.empty())
				{
					std::cerr << strus::string_format( "got dump config [%s]", dt.first.c_str()) << std::endl;
				}
				else
				{
					std::cerr << strus::string_format( "got dump config [%s] = '%s'", dt.first.c_str(), dt.second.c_str()) << std::endl;
				}
#endif
			}
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
		std::string resourcepath;
		if (0!=strus::getParentPath( analyzerprg, resourcepath))
		{
			throw strus::runtime_error( _TXT("failed to evaluate resource path"));
		}
		if (!resourcepath.empty())
		{
			moduleLoader->addResourcePath( resourcepath);
		}

		// Create objects for analyzer:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;

		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error(_TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error(_TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create rpc analyzer object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create analyzer object builder"));
		}

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* proxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( proxy);
		}

		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor"));

		// Load the document and get its properties:
		strus::InputStream input( docpath);
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			if (!strus::parseDocumentClass( documentClass, contenttype, errorBuffer.get()))
			{
				throw strus::runtime_error(_TXT("failed to parse document class"));
			}
		}
		else
		{
			char hdrbuf[ 1024];
			std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
			if (input.error())
			{
				throw strus::runtime_error( _TXT("failed to read document file '%s': %s"), docpath.c_str(), ::strerror(input.error())); 
			}
			if (!textproc->detectDocumentClass( documentClass, hdrbuf, hdrsize))
			{
				throw strus::runtime_error( _TXT("failed to detect document class")); 
			}
		}

		// Get the document segmenter type either defined by the document class or by content or by the name specified:
		const strus::SegmenterInterface* segmenter;
		strus::analyzer::SegmenterOptions segmenteropts;
		if (segmentername.empty())
		{
			segmenter = textproc->getSegmenterByMimeType( documentClass.mimeType());
			if (!segmenter) throw strus::runtime_error(_TXT("failed to find document segmenter specified by MIME type '%s'"), documentClass.mimeType().c_str());
			if (!documentClass.scheme().empty())
			{
				segmenteropts = textproc->getSegmenterOptions( documentClass.scheme());
			}
		}
		else
		{
			segmenter = textproc->getSegmenterByName( segmentername);
			if (!segmenter) throw strus::runtime_error(_TXT("failed to find document segmenter specified by name '%s'"), segmentername.c_str());
		}

		// Create the document analyzer:
		std::auto_ptr<strus::DocumentAnalyzerInterface> analyzer( analyzerBuilder->createDocumentAnalyzer( segmenter, segmenteropts));
		if (!analyzer.get()) throw strus::runtime_error(_TXT("failed to create document analyzer"));

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			throw strus::runtime_error( _TXT("failed to load analyzer program %s (file system error %u)"), analyzerprg.c_str(), ec);
		}
		if (!strus::loadDocumentAnalyzerProgram( *analyzer, textproc, analyzerProgramSource, true/*allow includes*/, std::cerr, errorBuffer.get()))
		{
			throw strus::runtime_error( _TXT("failed to load analyzer program %s"), analyzerprg.c_str());
		}
		std::auto_ptr<strus::DocumentAnalyzerContextInterface>
			analyzerContext( analyzer->createContext( documentClass));
		if (!analyzerContext.get()) throw strus::runtime_error(_TXT("failed to create document analyzer context"));

		// Process the document:
		enum {AnalyzerBufSize=8192};
		char buf[ AnalyzerBufSize];
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
			analyzerContext->putInput( buf, readsize, eof);

			// Analyze the document and print the result:
			strus::analyzer::Document doc;
			while (analyzerContext->analyzeNext( doc))
			{
				if (doDump)
				{
					std::vector<strus::analyzer::Term> termar;
					std::vector<strus::analyzer::MetaData>::const_iterator
						mi = doc.metadata().begin(), me = doc.metadata().end();
					for (; mi != me; ++mi)
					{
						DumpConfig::const_iterator dci = dumpConfig.find( mi->name());
						if (dci != dumpConfig.end())
						{
							if (dci->second.empty())
							{
								termar.push_back( strus::analyzer::Term( mi->name(), mi->value().tostring().c_str(), 0/*pos*/, 0/*len*/));
							}
							else
							{
								termar.push_back( strus::analyzer::Term( mi->name(), dci->second, 0/*pos*/, 0/*len*/));
							}
						}
					}
					std::vector<strus::analyzer::Attribute>::const_iterator
						ai = doc.attributes().begin(), ae = doc.attributes().end();
					for (; ai != ae; ++ai)
					{
						DumpConfig::const_iterator dci = dumpConfig.find( ai->name());
						if (dci != dumpConfig.end())
						{
							if (dci->second.empty())
							{
								termar.push_back( strus::analyzer::Term( ai->name(), ai->value(), 0/*pos*/, 0/*len*/));
							}
							else
							{
								termar.push_back( strus::analyzer::Term( ai->name(), dci->second, 0/*pos*/, 0/*len*/));
							}
						}
					}
					filterTerms( termar, dumpConfig, doc.searchIndexTerms());
					filterTerms( termar, dumpConfig, doc.forwardIndexTerms());

					std::sort( termar.begin(), termar.end(), TermOrder());

					std::vector<strus::analyzer::Term>::const_iterator
						ti = termar.begin(), te = termar.end();
					for (unsigned int tidx=0; ti != te; ++ti,++tidx)
					{
						if (tidx) std::cout << ' ';
						std::cout << ti->value();
					}
				}
				else
				{
					if (!doc.subDocumentTypeName().empty())
					{
						std::cout << "-- " << strus::string_format( _TXT("document type name %s"), doc.subDocumentTypeName().c_str()) << std::endl;
					}
					std::vector<strus::analyzer::Term> itermar = doc.searchIndexTerms();
					std::sort( itermar.begin(), itermar.end(), TermOrder());
		
					std::vector<strus::analyzer::Term>::const_iterator
						ti = itermar.begin(), te = itermar.end();

					std::cout << std::endl << _TXT("search index terms:") << std::endl;
					for (; ti != te; ++ti)
					{
						std::cout << ti->pos() << ":" << ti->len()
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
							  << " '" << mi->value().tostring().c_str() << "'"
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
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("error in analyze document"));
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



