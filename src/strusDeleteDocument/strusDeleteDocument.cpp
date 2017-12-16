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
#include "strus/storageTransactionInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "private/programOptions.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/local_ptr.hpp"
#include "private/version.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
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

static void deleteDocuments( strus::StorageClientInterface* storage, int nofargs, const char** argv)
{
	strus::local_ptr<strus::StorageTransactionInterface> transaction( storage->createTransaction());
	int ai=0, ae=nofargs;
	for (; ai != ae; ++ai)
	{
		transaction->deleteDocument( argv[ai]);
	}
	transaction->commit();
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
				argc, argv, 8,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:",
				"r,rpc:", "s,storage:", "T,trace:");
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
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusDeleteDocument [options] <docid>" << std::endl;
			std::cout << "<docid>  = " << _TXT("docid of the document to delete") << std::endl;
			std::cout << _TXT("description: Deletes a document in the storage.") << std::endl;
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
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
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

		// Parse arguments:
		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--storage", "--rpc");
			storagecfg = opt["storage"];
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

		deleteDocuments( storage.get(), opt.nofargs(), opt.argv());
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("failed to delete documents"));
		}
		storage->close();
		std::cerr << strus::string_format( _TXT("done %u documents deleted"), (unsigned int)opt.nofargs()) << std::endl;
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


