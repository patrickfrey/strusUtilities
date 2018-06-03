/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/storage_objbuild.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/reference.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/index.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/thread.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "fileCrawler.hpp"
#include "commitQueue.hpp"
#include "insertProcessor.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>

static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& config, strus::ErrorBufferInterface* errorhnd)
{
	std::string configstr( config);
	std::string dbname;
	(void)strus::extractStringFromConfigString( dbname, configstr, "database", errorhnd);
	if (errorhnd->hasError()) throw strus::runtime_error(_TXT("cannot evaluate database: %s"), errorhnd->fetchError());

	strus::local_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
	if (!dbi) throw std::runtime_error( _TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (!sti) throw std::runtime_error( _TXT("failed to get storage interface"));

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient), errorhnd);
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient), errorhnd);
}


int main( int argc_, const char* argv_[])
{
	int rt = 0;
	FILE* logfile = 0;
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
				errorBuffer.get(), argc_, argv_, 18,
				"h,help", "v,version", "license",
				"G,debug:", "t,threads:", "c,commit:", "f,fetch:",
				"g,segmenter:", "C,contenttype:", "m,module:",
				"L,logerror:", "M,moduledir:", "R,resourcedir:",
				"r,rpc:", "x,extension:", "s,storage:",
				"S,configfile:", "V,verbose", "T,trace:");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}

		int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
			if (!errorBuffer->setMaxNofThreads( nofThreads+2))
			{
				std::cerr << _TXT("failed to set number of threads for error buffer (option --threads)") << std::endl;
				return -1;
			}
		}
		if (opt( "help")) printUsageAndExit = true;
		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw std::runtime_error( _TXT("failed to create module loader"));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir" ,"--rpc");
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
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--module", "--rpc");
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
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
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
		std::string storagecfg;
		if (opt("configfile"))
		{
			if (opt("storage")) throw strus::runtime_error(_TXT("conflicting configuration options specified: '%s' and '%s'"), "--storage", "--configfile");
			std::string configfile = opt[ "configfile"];
			int ec = strus::readFile( configfile, storagecfg);
			if (ec) throw strus::runtime_error(_TXT("failed to read configuration file %s (errno %u)"), configfile.c_str(), ec);

			std::string::iterator di = storagecfg.begin(), de = storagecfg.end();
			for (; di != de; ++di)
			{
				if ((unsigned char)*di < 32) *di = ' ';
			}
		}
		if (opt("storage"))
		{
			if (opt("configfile")) throw strus::runtime_error(_TXT("conflicting configuration options specified: '%s' and '%s'"), "--storage", "--configfile");
			storagecfg = opt[ "storage"];
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusInsert [options] <program> <docpath>" << std::endl;
			std::cout << "<program> = " << _TXT("path of analyzer program or analyzer map program") << std::endl;
			std::cout << "<docpath> = " << _TXT("path of document or directory to insert") << std::endl;
			std::cout << _TXT("description: Insert a document or a set of documents into a storage.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), storagecfg, errorBuffer.get());
			}
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration file as <FILENAME>") << std::endl;
			std::cout << "    " << _TXT("<FILENAME> is a file containing the configuration string") << std::endl;
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Issue debug messages for component <COMP> to stderr") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME>") << std::endl;
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of all documents inserted.") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("Grab only the files with extension <EXT> (default all files)") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of inserter threads to use") << std::endl;
			std::cout << "-c|--commit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of documents inserted per transaction (default 1000)") << std::endl;
			std::cout << "-f|--fetch <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files fetched in each inserter iteration") << std::endl;
			std::cout << "    " << _TXT("Default is the value of option '--commit' (one document/file)") << std::endl;
			std::cout << "-L|--logerror <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write the last error occurred to <FILE> in case of an exception")  << std::endl;
			std::cout << "-V|--verbose" << std::endl;
			std::cout << "    " << _TXT("verbose output") << std::endl;
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
		unsigned int transactionSize = 1000;
		if (opt("logerror"))
		{
			std::string filename( opt["logerror"]);
			logfile = fopen( filename.c_str(), "a+");
			if (!logfile) throw strus::runtime_error(_TXT("error loading log file '%s' for appending (errno %u)"), filename.c_str(), errno);
			errorBuffer->setLogFile( logfile);
		}
		if (opt("commit"))
		{
			transactionSize = opt.asUint( "commit");
		}
		unsigned int fetchSize = transactionSize;
		if (opt("fetch"))
		{
			fetchSize = opt.asUint( "fetch");
		}
		std::string analyzerprg = opt[0];
		std::string datapath = opt[1];
		std::string fileext = "";
		std::string segmentername;
		std::string contenttype;
		bool verbose = opt( "verbose");

		if (opt( "segmenter"))
		{
			segmentername = opt[ "segmenter"];
		}
		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
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
			throw strus::runtime_error( "%s",  _TXT("failed to evaluate resource path"));
		}
		if (!resourcepath.empty())
		{
			moduleLoader->addResourcePath( resourcepath);
		}
		else
		{
			moduleLoader->addResourcePath( "./");
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}

		// Create objects for inserter:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		strus::local_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw std::runtime_error( _TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw std::runtime_error( _TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create rpc analyzer object builder"));
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create rpc storage object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create analyzer object builder"));
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create storage object builder"));
		}

		// Create proxy objects if tracing enabled:
		{
			std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
			for (; ti != te; ++ti)
			{
				strus::AnalyzerObjectBuilderInterface* aproxy = (*ti)->createProxy( analyzerBuilder.get());
				analyzerBuilder.release();
				analyzerBuilder.reset( aproxy);
				strus::StorageObjectBuilderInterface* sproxy = (*ti)->createProxy( storageBuilder.get());
				storageBuilder.release();
				storageBuilder.reset( sproxy);
			}
		}
		strus::local_ptr<strus::StorageClientInterface>
			storage( strus::createStorageClient( storageBuilder.get(), errorBuffer.get(), storagecfg));
		if (!storage.get()) throw std::runtime_error( _TXT("failed to create storage client"));

		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw std::runtime_error( _TXT("failed to get text processor"));

		// Try to determine document class:
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			if (!strus::parseDocumentClass( documentClass, contenttype, errorBuffer.get()))
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

		// Load analyzer program(s):
		strus::AnalyzerMap analyzerMap( analyzerBuilder.get(), errorBuffer.get());
		if (analyzerMap.isAnalyzerConfigSource( analyzerprg))
		{
			analyzerMap.loadDefaultAnalyzerProgram( documentClass, segmentername, analyzerprg);
		}
		else
		{
			if (!segmentername.empty())
			{
				throw strus::runtime_error(_TXT("specified default segmenter (option --segmenter) '%s' with analyzer map as argument"), segmentername.c_str());
			}
			analyzerMap.loadAnalyzerMap( analyzerprg);
		}

		// Start inserter process:
		strus::local_ptr<strus::CommitQueue>
			commitQue( new strus::CommitQueue( storage.get(), verbose, errorBuffer.get()));

		strus::FileCrawler fileCrawler( datapath, fetchSize, fileext);
		if (nofThreads == 0)
		{
			strus::InsertProcessor inserter(
				storage.get(), textproc, &analyzerMap, documentClass, commitQue.get(),
				&fileCrawler, transactionSize, verbose, errorBuffer.get());
			inserter.run();
		}
		else
		{
			std::vector<strus::Reference<strus::InsertProcessor> > processorList;
			processorList.reserve( nofThreads);
			for (int ti = 0; ti<nofThreads; ++ti)
			{
				processorList.push_back(
					new strus::InsertProcessor(
						storage.get(), textproc, &analyzerMap, documentClass, commitQue.get(),
						&fileCrawler, transactionSize, verbose, errorBuffer.get()));
			}
			{
				std::vector<strus::Reference<strus::thread> > threadGroup;
				for (int ti=0; ti<nofThreads; ++ti)
				{
					strus::InsertProcessor* tc = processorList[ ti].get();
					strus::Reference<strus::thread> th( new strus::thread( &strus::InsertProcessor::run, tc));
					threadGroup.push_back( th);
				}
				std::vector<strus::Reference<strus::thread> >::iterator gi = threadGroup.begin(), ge = threadGroup.end();
				for (; gi != ge; ++gi) (*gi)->join();
			}
		}
		storage->close();

		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("unhandled error in insert storage"));
		}
		if (commitQue->errors().size() > 0)
		{
			std::cerr << std::endl << "finished, but with " << commitQue->errors().size() << " transactions failed." << std::endl;
		}
		else
		{
			if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
			{
				std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
			}
			std::cerr << _TXT("done.") << std::endl;
		}
		if (logfile) fclose( logfile);
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
	if (logfile) fclose( logfile);
	return -1;
}


