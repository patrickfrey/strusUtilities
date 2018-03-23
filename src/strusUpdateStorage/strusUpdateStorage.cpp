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
#include "strus/programLoader.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/attributeReaderInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/local_ptr.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <map>


static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& config, strus::ErrorBufferInterface* errorhnd)
{
	std::string configstr( config);
	std::string dbname;
	(void)strus::extractStringFromConfigString( dbname, configstr, "database", errorhnd);
	if (errorhnd->hasError()) throw strus::runtime_error(_TXT("cannot evaluate database: %s"), errorhnd->fetchError());

	strus::local_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
	if (!dbi) throw strus::runtime_error( "%s", _TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (!sti) throw strus::runtime_error( "%s", _TXT("failed to get storage interface"));

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient), errorhnd);
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient), errorhnd);
}

static std::multimap<std::string,strus::Index> loadAttributeDocnoMap(
		strus::StorageClientInterface* storage, const std::string& attributeName)
{
	std::multimap<std::string,strus::Index> rt;
	strus::local_ptr<strus::AttributeReaderInterface> attributeReader( storage->createAttributeReader());
	if (!attributeReader.get()) throw strus::runtime_error( "%s", _TXT("failed to create attribute reader"));
	strus::Index ehnd = attributeReader->elementHandle( attributeName.c_str());
	if (ehnd == 0) throw strus::runtime_error(_TXT("unknown attribute name '%s'"), attributeName.c_str());
	strus::Index di = 1, de = storage->maxDocumentNumber()+1;
	for (; di != de; ++di)
	{
		attributeReader->skipDoc( di);
		std::string name = attributeReader->getValue( ehnd);
		if (!name.empty())
		{
			rt.insert( std::pair<std::string,strus::Index>( name, di));
		}
	}
	return rt;
}


int main( int argc, const char* argv[])
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
				errorBuffer.get(), argc, argv, 15,
				"h,help", "v,version", "license",
				"G,debug:", "m,module:", "M,moduledir:", "L,logerror:",
				"r,rpc:", "s,storage:", "c,commit:",
				"a,attribute:", "x,mapattribute:",
				"m,metadata:","u,useraccess", "T,trace:");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}
		if (opt( "help"))
		{
			printUsageAndExit = true;
		}
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
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusUpdateStorage [options] <updatefile>" << std::endl;
			std::cout << "<updatefile>  = " << _TXT("file with the batch of updates ('-' for stdin)") << std::endl;
			std::cout << _TXT("description: Executes a batch of updates of attributes, meta data") << std::endl;
			std::cout << "             " << _TXT("or user rights in a storage.") << std::endl;
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
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
			std::cout << "-a|--attribute <NAME>" << std::endl;
			std::cout << "    " << _TXT("The update batch is a list of attributes assignments") << std::endl;
			std::cout << "    " << _TXT("The name of the updated attribute is <NAME>.") << std::endl;
			std::cout << "-m|--metadata <NAME>" << std::endl;
			std::cout << "    " << _TXT("The update batch is a list of meta data assignments.") << std::endl;
			std::cout << "    " << _TXT("The name of the updated meta data element is <NAME>.") << std::endl;
			std::cout << "-u|--useraccess" << std::endl;
			std::cout << "    " << _TXT("The update batch is a list of user right assignments.") << std::endl;
			std::cout << "-x|--mapattribute <ATTR>" << std::endl;
			std::cout << "    " << _TXT("The update document is selected by the attribute <ATTR> as key,") << std::endl;
			std::cout << "    " << _TXT("instead of the document id or document number.") << std::endl;
			std::cout << "-c|--commit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of updates per transaction (default 10000)") << std::endl;
			std::cout << "    " << _TXT("If <N> is set to 0 then only one commit is done at the end") << std::endl;
			std::cout << "-L|--logerror <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write the last error occurred to <FILE> in case of an exception")  << std::endl;
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
		// Set logger:
		if (opt("logerror"))
		{
			std::string filename( opt["logerror"]);
			logfile = fopen( filename.c_str(), "a+");
			if (!logfile) throw strus::runtime_error(_TXT("error loading log file '%s' for appending (errno %u)"), filename.c_str(), errno);
			errorBuffer->setLogFile( logfile);
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("error in initialization"));
		}

		// Parse arguments:
		std::string storagecfg;
		std::multimap<std::string,strus::Index> attributemap;
		std::multimap<std::string,strus::Index>* attributemapref = 0;
		std::string mapattribute;
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--storage", "--rpc");
			storagecfg = opt["storage"];
		}
		if (opt("mapattribute"))
		{
			mapattribute = opt["mapattribute"];
		}
		// Create objects for storage document update:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error( "%s",  _TXT("error creating rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error( "%s",  _TXT("error creating rpc client"));
			(void)messaging.release();
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( "%s",  _TXT("error creating rpc storage object builder"));
		}
		else
		{
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( "%s",  _TXT("error creating storage object builder"));
		}
		strus::local_ptr<strus::StorageClientInterface>
			storage( strus::createStorageClient( storageBuilder.get(), errorBuffer.get(), storagecfg));
		if (!storage.get()) throw strus::runtime_error( "%s", _TXT("failed to create storage client"));
		if (!mapattribute.empty())
		{
			attributemap = loadAttributeDocnoMap( storage.get(), mapattribute);
			attributemapref = &attributemap;
		}
		enum UpdateOperation
		{
			UpdateOpAttribute,
			UpdateOpMetadata,
			UpdateOpUserAccess
		};
		UpdateOperation updateOperation;
		std::string elemname;
		std::string updateBatchPath( opt[0]);

		if (opt("metadata"))
		{
			if (opt("attribute")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s") ,"--attribute", "--metadata");
			if (opt("useraccess")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--useraccess", "--metadata");
			elemname = opt["metadata"];
			updateOperation = UpdateOpMetadata;
		}
		else if (opt("attribute"))
		{
			if (opt("useraccess")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--useraccess", "--attribute");
			elemname = opt["attribute"];
			updateOperation = UpdateOpAttribute;
		}
		else if (opt("useraccess"))
		{
			updateOperation = UpdateOpUserAccess;
		}
		else
		{
			throw strus::runtime_error(_TXT("no update operation type specified as option (one of %s is mandatory)"), "--attribute,--metadata,--useraccess");
		}
		unsigned int nofUpdates = 0;
		unsigned int transactionSize = 10000;
		if (opt("commit"))
		{
			transactionSize = opt.asUint( "commit");
		}
		switch (updateOperation)
		{
			case UpdateOpMetadata:
				nofUpdates = strus::loadDocumentMetaDataAssignments(
						*storage, elemname, attributemapref, updateBatchPath, transactionSize, errorBuffer.get());
				break;
			case UpdateOpAttribute:
				nofUpdates = strus::loadDocumentAttributeAssignments(
						*storage, elemname, attributemapref, updateBatchPath, transactionSize, errorBuffer.get());
				break;
			case UpdateOpUserAccess:
				nofUpdates = strus::loadDocumentUserRightsAssignments(
						*storage, attributemapref, updateBatchPath, transactionSize, errorBuffer.get());
				break;
		}
		if (!nofUpdates && errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("error in update storage"));
		}
		storage->close();
		std::cerr << strus::string_format( _TXT("done %u update operations"), nofUpdates) << std::endl;
		if (logfile) fclose( logfile);
		if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
		{
			std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
		}
		std::cerr << _TXT("done.") << std::endl;
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


