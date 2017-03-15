/*
 * Copyright (c) 2016 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Program pre-calculating and storing statistics for documents in meta data
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/storage_objbuild.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/lib/scalarfunc.hpp"
#include "strus/reference.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/documentTermIteratorInterface.hpp"
#include "strus/statisticsProcessorInterface.hpp"
#include "strus/statisticsIteratorInterface.hpp"
#include "strus/statisticsViewerInterface.hpp"
#include "strus/scalarFunctionParserInterface.hpp"
#include "strus/scalarFunctionInterface.hpp"
#include "strus/scalarFunctionInstanceInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "strus/numericVariant.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/string_format.hpp"
#include "private/version.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <map>

#undef STRUS_LOWLEVEL_DEBUG

static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& config, strus::ErrorBufferInterface* errorhnd)
{
	std::string configstr( config);
	std::string dbname;
	(void)strus::extractStringFromConfigString( dbname, configstr, "database", errorhnd);
	if (errorhnd->hasError()) throw strus::runtime_error(_TXT("cannot evaluate database: %s"), errorhnd->fetchError());

	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
	if (!dbi) throw strus::runtime_error(_TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (!sti) throw strus::runtime_error(_TXT("failed to get storage interface"));

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient), errorhnd);
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient), errorhnd);
}

typedef std::map<std::string,strus::GlobalCounter> DfMap;

static void fillDfMap( DfMap& dfmap, strus::GlobalCounter& collectionSize, const std::string& feattype, strus::StorageClientInterface* storage)
{
	const strus::StatisticsProcessorInterface* statproc = storage->getStatisticsProcessor();
	if (!statproc) throw strus::runtime_error(_TXT("failed to get statistics processor"));
	strus::Reference<strus::StatisticsIteratorInterface> statitr( storage->createInitStatisticsIterator());
	if (!statitr.get()) throw strus::runtime_error(_TXT("failed to initialize statistics iterator"));
	collectionSize += storage->nofDocumentsInserted();
	const char* statmsg;
	std::size_t statmsgsize;
	while (statitr->getNext( statmsg, statmsgsize))
	{
		strus::Reference<strus::StatisticsViewerInterface>
			viewer( statproc->createViewer( statmsg, statmsgsize));
		if (!viewer.get()) throw strus::runtime_error(_TXT("failed to statistics viewer"));

		strus::StatisticsViewerInterface::DocumentFrequencyChange dfchg;
		while (viewer->nextDfChange( dfchg))
		{
			if (feattype == dfchg.type())
			{
				dfmap[ dfchg.value()] += dfchg.increment();
			}
		}
	}
}

static void updateStorageWithFormula( const DfMap& dfmap, const std::string& feattype, const std::string& fieldname, strus::StorageClientInterface* storage, unsigned int transactionSize, const strus::ScalarFunctionInstanceInterface* func, const strus::ScalarFunctionInstanceInterface* normfunc)
{
	strus::Reference<strus::StorageTransactionInterface>
		transaction( storage->createTransaction());
	if (!transaction.get()) throw strus::runtime_error(_TXT("failed to create storage transaction"));
	unsigned int transactionCount = 0;
	unsigned int transactionTotalCount = 0;
	strus::Reference<strus::DocumentTermIteratorInterface>
		termitr( storage->createDocumentTermIterator( feattype));
	if (!termitr.get()) throw strus::runtime_error(_TXT("failed to create document term iterator"));
	strus::Index di = 1, de = storage->maxDocumentNumber();
	for (; di <= de; ++di)
	{
		strus::Index docno = termitr->skipDoc( di);
		if (!docno) return;
		double weight = 0;
		strus::DocumentTermIteratorInterface::Term term;
		while (termitr->nextTerm( term))
		{
			std::string termval( termitr->termValue( term.termno));
			double args[2];
			DfMap::const_iterator dfi = dfmap.find( termval);
			if (dfi == dfmap.end()) throw strus::runtime_error(_TXT("df for '%s' not found in map"), termval.c_str());
			args[0] = dfi->second;
			args[1] = term.tf;
			weight += func->call( args, 2);
		}
		weight = normfunc->call( &weight, 1);
		transaction->updateMetaData( docno, fieldname, strus::NumericVariant( weight));
		if (++transactionCount >= transactionSize)
		{
			if (!transaction->commit()) throw strus::runtime_error(_TXT("transaction commit failed"));
			transaction.reset( storage->createTransaction());
			if (!transaction.get()) throw strus::runtime_error(_TXT("failed to create storage transaction"));

			transactionTotalCount += transactionCount;
			fprintf( stderr, "\rupdated %u documents           ", transactionTotalCount);
			transactionCount = 0;
		}
		
	}
	if (transactionCount)
	{
		transactionTotalCount += transactionCount;
		transactionCount = 0;
		if (!transaction->commit()) throw strus::runtime_error(_TXT("transaction commit failed"));
		fprintf( stderr, "updated %u documents\n", transactionTotalCount);
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
				argc, argv, 9,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:",
				"r,rpc:", "s,storage:", "c,commit:",
				"T,trace:");
		if (opt( "help"))
		{
			printUsageAndExit = true;
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
			if (opt.nofargs() < 3)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() > 4)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusUpdateStorageCalcStatistics [options] <metadata> <feattype> <formula> <sumnorm>" << std::endl;
			std::cout << "<metadata>  = " << _TXT("meta data element to store the result") << std::endl;
			std::cout << "<feattype>  = " << _TXT("search index feature type to calculate the result with") << std::endl;
			std::cout << "<formula>   = " << _TXT("meta formula to calculate the result with") << std::endl;
			std::cout << "<sumnorm>   = " << _TXT("formula to normalize the sum of results (identity function is default)") << std::endl;
			std::cout << _TXT("description: Calculate a formula for each document in the storages") << std::endl;
			std::cout << "              " << _TXT("and update a metadata field with the result.") << std::endl;
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
			std::cout << "    " << _TXT("Define a storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
			std::cout << "-c|--commit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of updates per transaction (default 10000)") << std::endl;
			std::cout << "    " << _TXT("If <N> is set to 0 then only one commit is done at the end") << std::endl;
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
		std::vector<std::string> storagecfgs;
		std::string fieldname( opt[0]);
		std::string feattype( opt[1]);
		std::string formula( opt[2]);
		std::string sumnorm;
		if (opt.nofargs() > 3)
		{
			sumnorm = opt[3];
			if (0==std::strchr( sumnorm.c_str(), '(') && 0==std::strchr( sumnorm.c_str(), '_'))
			{
				sumnorm = sumnorm + "(_0)";
			}
		}
		else
		{
			sumnorm = "_0";
		}
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--storage", "--rpc");
			storagecfgs = opt.list( "storage");
		}

		// Create objects for storage document update:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error( _TXT("error creating rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error( _TXT("error creating rpc client"));
			(void)messaging.release();
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( _TXT("error creating rpc storage object builder"));
		}
		else
		{
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( _TXT("error creating storage object builder"));
		}
		// Calculate the df map:
		DfMap dfmap;
		strus::GlobalCounter collectionSize = 0;
		strus::GlobalCounter collectionNofTerms = 0;
		std::vector<std::string>::iterator
			ci = storagecfgs.begin(), ce = storagecfgs.end();
		for (; ci != ce; ++ci)
		{
			std::string configstr = *ci;
			std::string cfgvalue;
			if (!strus::extractStringFromConfigString( cfgvalue, configstr, "statsproc", errorBuffer.get()))
			{
				*ci = "statsproc=default;" + *ci;
			}
			std::auto_ptr<strus::StorageClientInterface>
				storage( strus::createStorageClient( storageBuilder.get(), errorBuffer.get(), *ci));
			if (!storage.get())
			{
				throw strus::runtime_error(_TXT("failed to open storage '%s'"), ci->c_str());
			}
			fillDfMap( dfmap, collectionSize, feattype, storage.get());
		}
		collectionNofTerms = dfmap.size();

		// Build the functions for calculating the statistics:
		strus::Reference<strus::ScalarFunctionParserInterface> funcparser( strus::createScalarFunctionParser_default( errorBuffer.get()));
		if (!funcparser.get()) throw strus::runtime_error(_TXT("failed to load scalar function parser"));

		std::vector<std::string> args;
		args.push_back("df");
		args.push_back("tf");
		strus::Reference<strus::ScalarFunctionInterface>
			func( funcparser->createFunction( formula, args));
		if (!func.get()) throw strus::runtime_error(_TXT("failed to parse scalar function '%s'"), formula.c_str());
		strus::Reference<strus::ScalarFunctionInterface>
			normfunc( funcparser->createFunction( sumnorm, std::vector<std::string>()));
		if (!normfunc.get()) throw strus::runtime_error(_TXT("failed to parse scalar function '%s'"), sumnorm.c_str());

		strus::Reference<strus::ScalarFunctionInstanceInterface>
			funcinst( func->createInstance());
		if (!funcinst.get()) throw strus::runtime_error(_TXT("failed to create scalar function instance of '%s'"), formula.c_str());
		strus::Reference<strus::ScalarFunctionInstanceInterface>
			normfuncinst( normfunc->createInstance());
		if (!normfuncinst.get()) throw strus::runtime_error(_TXT("failed to create scalar function instance of '%s'"), sumnorm.c_str());

		// Initialize some variables, if referenced
		std::vector<std::string> variables = func->getVariables();
		std::vector<std::string>::const_iterator vi = variables.begin(), ve = variables.end();
		for (; vi != ve; ++vi)
		{
			if (strus::utils::caseInsensitiveEquals( *vi, "N")) funcinst->setVariableValue( "N", collectionSize);
			if (strus::utils::caseInsensitiveEquals( *vi, "T")) funcinst->setVariableValue( "T", collectionNofTerms);
		}
		variables = normfunc->getVariables();
		vi = variables.begin(), ve = variables.end();
		for (; vi != ve; ++vi)
		{
			if (strus::utils::caseInsensitiveEquals( *vi, "N")) normfuncinst->setVariableValue( "N", collectionSize);
			if (strus::utils::caseInsensitiveEquals( *vi, "T")) normfuncinst->setVariableValue( "T", collectionNofTerms);
		}

		// Do the updates:
		unsigned int transactionSize = 10000;
		if (opt("commit"))
		{
			transactionSize = opt.asUint( "commit");
		}
		ci = storagecfgs.begin(), ce = storagecfgs.end();
		for (; ci != ce; ++ci)
		{
			std::auto_ptr<strus::StorageClientInterface>
				storage( strus::createStorageClient( storageBuilder.get(), errorBuffer.get(), *ci));
			if (!storage.get())
			{
				throw strus::runtime_error(_TXT("failed to open storage '%s'"), ci->c_str());
			}
			fprintf( stderr, "update storage '%s':\n", ci->c_str());
			updateStorageWithFormula( dfmap, feattype, fieldname, storage.get(), transactionSize, funcinst.get(), normfuncinst.get());
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("error in update storage"));
		}
		fprintf( stderr, "done\n");
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


