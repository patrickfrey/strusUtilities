/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/vectorSpaceModelInterface.hpp"
#include "strus/vectorSpaceModelBuilderInterface.hpp"
#include "strus/vectorSpaceModelInstanceInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "strus/programLoader.hpp"
#include "strus/reference.hpp"
#include "private/version.hpp"
#include "strus/errorBufferInterface.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

#undef STRUS_LOWLEVEL_DEBUG
#define DEFAULT_LOAD_MODULE   "modstrus_storage_vectorspace_std"
#define DEFAULT_VECTOR_MODEL  "vector_std"

static strus::ErrorBufferInterface* g_errorBuffer = 0;


int main( int argc, const char* argv[])
{
	int rt = 0;
	std::auto_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	g_errorBuffer = errorBuffer.get();

	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 9,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:", "T,trace:",
				"s,config:", "S,configfile:", "f,file:");
		if (opt( "help")) printUsageAndExit = true;
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
		if (!moduleLoader->loadModule( DEFAULT_LOAD_MODULE))
		{
			std::cerr << _TXT("failed to load module ") << "'" << DEFAULT_LOAD_MODULE << "': " << errorBuffer->fetchError() << std::endl;
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
			std::cerr << std::endl;
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
				rt = 2;
			}
		}
		std::string config;
		int nof_config = 0;
		if (opt("configfile"))
		{
			nof_config += 1;
			std::string configfile = opt[ "configfile"];
			int ec = strus::readFile( configfile, config);
			if (ec) throw strus::runtime_error(_TXT("failed to read configuration file %s (errno %u): %s"), configfile.c_str(), ec, ::strerror(ec));

			std::string::iterator di = config.begin(), de = config.end();
			for (; di != de; ++di)
			{
				if ((unsigned char)*di < 32) *di = ' ';
			}
		}
		if (opt("config"))
		{
			nof_config += 1;
			config = opt[ "config"];
		}
		if (nof_config > 1)
		{
			std::cerr << _TXT("conflicting configuration options specified: --config and --configfile") << std::endl;
			rt = 3;
			printUsageAndExit = true;
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusVectorSpace [options] <command>" << std::endl;
			std::cout << _TXT("description: Utility program for processing data with a vector space model.") << std::endl;
			std::cout << _TXT("<command>     :command to perform, one of the following:") << std::endl;
			std::cout << _TXT("                 'names'    = store or print vector term names in original file") << std::endl;
			std::cout << _TXT("                 'store'    = store model without learning step") << std::endl;
			std::cout << _TXT("                 'learn'    = unsupervised learning of features") << std::endl;
			std::cout << _TXT("                 'feature'  = map all input features according to model") << std::endl;
			std::cout << _TXT("                 'class'    = same as feature but with inverted output") << std::endl;
			std::cout << _TXT("<inputfile>   :input file to process (optional)") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>.") << std::endl;
			std::cout << "    " << _TXT("The module modstrus_storage_vectorspace is implicitely defined") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-s|--config <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the vector space model configuration string as <CONFIG>") << std::endl;
			std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    " << _TXT("Define the vector space model configuration file as <FILENAME>") << std::endl;
			std::cout << "    " << _TXT("<FILENAME> is a file containing the configuration string") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "-f|--file <INFILE>" << std::endl;
			std::cout << "    " << _TXT("Declare the input file with the vectors to process a <INFILE>") << std::endl;
			std::cout << "    " << _TXT("The format of this file is declared with -F.") << std::endl;
			std::cout << "-F|--format <INFMT>" << std::endl;
			std::cout << "    " << _TXT("Declare the input file format of the processed data to be <INFMT>") << std::endl;
			std::cout << "    " << _TXT("Possible formats:") << std::endl;
			std::cout << "    " << _TXT("  'text_ssv'     (default) for text with and space delimited columns") << std::endl;
			std::cout << "    " << _TXT("  'bin_word2vec' for the google word2vec binary format little endian") << std::endl;
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
		// Get arguments:
		Command command = getCommand( opt[0]);
		std::vector<std::string> inputfiles;
		if (opt("file"))
		{
			inputfiles = opt.list( "file");
		}
		// Create root object:
		std::auto_ptr<strus::StorageObjectBuilderInterface>
			storageBuilder( moduleLoader->createStorageObjectBuilder());
		if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::StorageObjectBuilderInterface* sproxy = (*ti)->createProxy( storageBuilder.get());
			storageBuilder.release();
			storageBuilder.reset( sproxy);
		}

		// Create objects:
		std::string modelname;
		if (!strus::extractStringFromConfigString( modelname, config, "model", errorBuffer.get()))
		{
			modelname = DEFAULT_VECTOR_MODEL;
			if (errorBuffer->hasError()) throw strus::runtime_error("failed to parse vector space model from configuration");
		}
		std::string dbname;
		(void)strus::extractStringFromConfigString( dbname, config, "database", errorBuffer.get());
		if (errorBuffer->hasError()) throw strus::runtime_error(_TXT("cannot evaluate database: %s"));

		const strus::VectorSpaceModelInterface* vsi = storageBuilder->getVectorSpaceModel( modelname);
		if (!vsi) throw strus::runtime_error(_TXT("failed to get vector space model interface"));
		const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
		if (!dbi) throw strus::runtime_error(_TXT("failed to get database interface"));
		std::auto_ptr<VectorSpaceModelBuilderInterface> builder( vsi->createBuilder( config, dbi));
		if (!builder.get()) throw strus::runtime_error(_TXT("failed to create vector space model builder"));
		std::vector<std::string>::const_iterator fi = inputfiles.begin(), fe = inputfiles.end();
		for (; fi != fe; ++fi)
		{
			if (!strus::loadVectorSpaceModelVectors( builder.get(), inputfile, g_errorBuffer))
			{
				throw strus::runtime_error(_TXT("failed to load input"));
			}
			if (!builder->commit())
			{
				throw strus::runtime_error(_TXT("load input commit failed"));
			}
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in command"));
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
	return -1;
}


