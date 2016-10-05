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
#include "strus/moduleLoaderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/vectorSpaceModelInterface.hpp"
#include "strus/vectorSpaceModelBuilderInterface.hpp"
#include "strus/vectorSpaceModelInstanceInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "strus/programLoader.hpp"
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
#include <iostream>
#include <cstring>
#include <stdexcept>

#undef STRUS_LOWLEVEL_DEBUG
#define DEFAULT_LOAD_MODULE   "modstrus_storage_vectorspace"
#define DEFAULT_VECTOR_MODEL  "vector_std"

static strus::ErrorBufferInterface* g_errorBuffer = 0;

enum Command
{
	CmdLearnFeatures,
	CmdMapFeatures
};

static Command getCommand( const std::string& name)
{
	if (strus::utils::caseInsensitiveEquals( name, "learn"))
	{
		return CmdLearnFeatures;
	}
	else if (strus::utils::caseInsensitiveEquals( name, "map"))
	{
		return CmdMapFeatures;
	}
	else
	{
		throw strus::runtime_error(_TXT("unknown command '%s'"), name.c_str());
	}
}

static void doLearnFeatures( const strus::VectorSpaceModelInterface* vsi, const std::string& config, const strus::FeatureVectorDefFormat& fmt, const std::string& content)
{
	std::vector<strus::FeatureVectorDef> samples;

	if (!strus::parseFeatureVectors( samples, fmt, content, g_errorBuffer))
	{
		throw strus::runtime_error(_TXT("could not load training data"));
	}
	std::auto_ptr<strus::VectorSpaceModelBuilderInterface> builder( vsi->createBuilder( config));

	std::vector<strus::FeatureVectorDef>::const_iterator si = samples.begin(), se = samples.end();
	unsigned int sidx = 0;
	for (; si != se; ++si,++sidx)
	{
		builder->addSampleVector( si->vec);
		if ((sidx & 1023) == 0 && g_errorBuffer->hasError()) break;
		fprintf( stderr, "\radded %u vectors    ", sidx);
	}
	fprintf( stderr, "\radded %u vectors  (done)\n", sidx);
	if (g_errorBuffer->hasError())
	{
		throw strus::runtime_error(_TXT("error adding vector space model samples"));
	}
	std::cerr << "learning features from samples ..." << std::endl;
	if (!builder->finalize())
	{
		throw strus::runtime_error(_TXT("error building vector space model"));
	}
	std::cerr << "storing model ..." << std::endl;
	if (!builder->store())
	{
		throw strus::runtime_error(_TXT("error storing vector space model"));
	}
}

static void doMapFeatures( const strus::VectorSpaceModelInterface* vsi, const std::string& config, const strus::FeatureVectorDefFormat& fmt, const std::string& content)
{
	std::vector<strus::FeatureVectorDef> samples;
	if (!strus::parseFeatureVectors( samples, fmt, content, g_errorBuffer))
	{
		throw strus::runtime_error(_TXT("could not load features to map"));
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
	g_errorBuffer = errorBuffer.get();

	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 9,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:",
				"s,config:", "S,configfile:", "T,trace:",
				"f,format:");
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
			if (opt.nofargs() > 2)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			else if (opt.nofargs() < 2)
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
			if (ec) throw strus::runtime_error(_TXT("failed to read configuration file %s (errno %u)"), configfile.c_str(), ec);

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
			std::cout << _TXT("usage:") << " strusVectorSpace [options] <command> <inputfile>" << std::endl;
			std::cout << _TXT("description: Utility program for processing data with a vector space model.") << std::endl;
			std::cout << _TXT("<command>     :command to perform, one of the following:") << std::endl;
			std::cout << _TXT("                 'learn' = unsupervised learning of features") << std::endl;
			std::cout << _TXT("                 'map'   = map all input features according to model") << std::endl;
			std::cout << _TXT("<inputfile>   :input file to process") << std::endl;
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
			std::cout << "-f|--format <INFMT>" << std::endl;
			std::cout << "    " << _TXT("declare the input file format of the processed data to be <INFMT>") << std::endl;
			std::cout << "    " << _TXT("  (default 'text' for text with and space delimited columns)") << std::endl;
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
		std::string inputfile( opt[1]);

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
		const strus::VectorSpaceModelInterface* vsi = storageBuilder->getVectorSpaceModel( modelname);
		if (!vsi) throw strus::runtime_error(_TXT("failed to get vector space model interface"));

		strus::FeatureVectorDefFormat format = strus::FeatureVectorDefTextssv;
		if (opt("format"))
		{
			if (!strus::parseFeatureVectorDefFormat( format, opt["format"], errorBuffer.get()))
			{
				throw strus::runtime_error(_TXT("wrong format option: %s"), errorBuffer->fetchError());
			}
		}
		std::string inputstr;
		switch (command)
		{
			case CmdLearnFeatures:
				doLearnFeatures( vsi, config, format, inputstr);
			break;
			case CmdMapFeatures:
				doMapFeatures( vsi, config, format, inputstr);
			break;
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


