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

enum Command
{
	CmdStoreModel,
	CmdLearnFeatures,
	CmdMapFeatures,
	CmdMapClasses
};

static const char* commandName( Command cmd)
{
	switch (cmd)
	{
		case CmdStoreModel: return "store";
		case CmdLearnFeatures: return "learn";
		case CmdMapFeatures: return "feature";
		case CmdMapClasses: return "class";
	}
	return 0;
}

static Command getCommand( const std::string& name)
{
	if (strus::utils::caseInsensitiveEquals( name, "store"))
	{
		return CmdStoreModel;
	}
	else if (strus::utils::caseInsensitiveEquals( name, "learn"))
	{
		return CmdLearnFeatures;
	}
	else if (strus::utils::caseInsensitiveEquals( name, "feature"))
	{
		return CmdMapFeatures;
	}
	else if (strus::utils::caseInsensitiveEquals( name, "class"))
	{
		return CmdMapClasses;
	}
	else
	{
		throw strus::runtime_error(_TXT("unknown command '%s'"), name.c_str());
	}
}

// Read input vectors data file:
static strus::FeatureVectorList readInputVectors( const std::string& inputfile, const strus::FeatureVectorDefFormat& fmt)
{
	strus::FeatureVectorList rt;
	std::string inputstr;
	int ec = strus::readFile( inputfile, inputstr);
	if (ec) throw strus::runtime_error(_TXT("failed to read input file %s (errno %u): %s"), inputfile.c_str(), ec, ::strerror(ec));
	
	if (!strus::parseFeatureVectors( rt, fmt, inputstr, g_errorBuffer))
	{
		throw strus::runtime_error(_TXT("could not load features to map"));
	}
	return rt;
}

static void doProcessFeatures( const strus::DatabaseInterface* dbi, const strus::VectorSpaceModelInterface* vsi, const std::string& config, const strus::FeatureVectorDefFormat& fmt, const std::string& inputfile, bool withFinalize)
{
	std::auto_ptr<strus::VectorSpaceModelBuilderInterface> builder( vsi->createBuilder( dbi, config));
	if (!builder.get())
	{
		throw strus::runtime_error(_TXT("error initializing vector space model builder"));
	}
	if (!inputfile.empty())
	{
		strus::FeatureVectorList samples = readInputVectors( inputfile, fmt);

		strus::FeatureVectorList::const_iterator si = samples.begin(), se = samples.end();
		unsigned int sidx = 0;
		for (; si != se; ++si,++sidx)
		{
			std::vector<double> vec( si->vec(), si->vec() + si->vecsize());
			builder->addSampleVector( si->name(), vec);
			if ((sidx & 1023) == 0)
			{
				if (g_errorBuffer->hasError()) break;
				fprintf( stderr, "\radded %u vectors    ", sidx);
			}
		}
		if (!builder->commit() || g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("error adding vector space model samples"));
		}
		fprintf( stderr, "\radded %u vectors  (done)\n", sidx);
	}
	if (withFinalize)
	{
		std::cerr << "unsupervised learning of features ..." << std::endl;
		if (!builder->finalize())
		{
			throw strus::runtime_error(_TXT("error building vector space model"));
		}
	}
}

static void doMapFeatures( const strus::DatabaseInterface* dbi, const strus::VectorSpaceModelInterface* vsi, const std::string& config, const strus::FeatureVectorDefFormat& fmt, const std::string& inputfile)
{
	strus::FeatureVectorList samples = readInputVectors( inputfile, fmt);

	std::auto_ptr<strus::VectorSpaceModelInstanceInterface> instance( vsi->createInstance( dbi, config));
	if (!instance.get())
	{
		throw strus::runtime_error(_TXT("error initializing vector space model instance"));
	}
	strus::FeatureVectorList::const_iterator si = samples.begin(), se = samples.end();
	unsigned int sidx = 0;
	for (; si != se; ++si,++sidx)
	{
		std::vector<double> vec( si->vec(), si->vec() + si->vecsize());
		std::vector<strus::Index> features( instance->mapVectorToFeatures( vec));
		if (!features.empty())
		{
			std::cout << si->name();
			std::vector<strus::Index>::const_iterator fi = features.begin(), fe = features.end();
			for (unsigned int fidx=0; fi != fe; ++fi,++fidx)
			{
				std::cout << ' ' << *fi;
			}
			std::cout << std::endl;
		}
	}
	if (g_errorBuffer->hasError())
	{
		throw strus::runtime_error(_TXT("error mapping vectors to features"));
	}
}

static void doMapClasses( const strus::DatabaseInterface* dbi, const strus::VectorSpaceModelInterface* vsi, const std::string& config, const strus::FeatureVectorDefFormat& fmt, const std::string& inputfile)
{
	strus::FeatureVectorList samples = readInputVectors( inputfile, fmt);

	std::auto_ptr<strus::VectorSpaceModelInstanceInterface> instance( vsi->createInstance( dbi, config));
	if (!instance.get())
	{
		throw strus::runtime_error(_TXT("error initializing vector space model instance"));
	}
	typedef std::multimap<strus::Index,strus::Index> ClassesMap;
	typedef std::pair<strus::Index,strus::Index> ClassesElem;
	ClassesMap classesmap;

	strus::FeatureVectorList::const_iterator si = samples.begin(), se = samples.end();
	unsigned int sidx = 0;
	for (; si != se; ++si,++sidx)
	{
		std::vector<double> vec( si->vec(), si->vec() + si->vecsize());
		std::vector<strus::Index> features( instance->mapVectorToFeatures( vec));
		if (!features.empty())
		{
			std::vector<strus::Index>::const_iterator fi = features.begin(), fe = features.end();
			for (unsigned int fidx=0; fi != fe; ++fi,++fidx)
			{
				classesmap.insert( ClassesElem( *fi, sidx));
			}
		}
		if ((sidx & 1023) == 0)
		{
			if (g_errorBuffer->hasError()) break;
			fprintf( stderr, "\rmapped %u vectors    ", sidx);
		}
	}
	fprintf( stderr, "\rmapped %u vectors  (done)\n", sidx);
	if (g_errorBuffer->hasError())
	{
		throw strus::runtime_error(_TXT("error mapping vectors"));
	}
	ClassesMap::const_iterator ci = classesmap.begin(), ce = classesmap.end();
	while (ci != ce)
	{
		strus::Index key = ci->first;
		std::cout << key;
		for (; ci != ce && ci->first == key; ++ci)
		{
			std::cout << ' ' << samples[ ci->second].name();
		}
		std::cout << std::endl;
	}
	if (g_errorBuffer->hasError())
	{
		throw strus::runtime_error(_TXT("error mapping vectors to classes"));
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
				argc, argv, 11,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:",
				"s,config:", "S,configfile:", "T,trace:",
				"F,format:", "f,file:", "o,output:");
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
			std::cout << "-o|--output <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write output to file <FILE>") << std::endl;
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
		std::string inputfile;
		std::string outputfile;
		if (opt("file"))
		{
			inputfile = opt["file"];
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
		const strus::VectorSpaceModelInterface* vsi = storageBuilder->getVectorSpaceModel( modelname);
		if (!vsi) throw strus::runtime_error(_TXT("failed to get vector space model interface"));
		const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( config);
		if (!dbi) throw strus::runtime_error(_TXT("failed to get database interface"));

		strus::FeatureVectorDefFormat format = strus::FeatureVectorDefTextssv;
		if (opt("format"))
		{
			if (!strus::parseFeatureVectorDefFormat( format, opt["format"], errorBuffer.get()))
			{
				throw strus::runtime_error(_TXT("wrong format option: %s"), errorBuffer->fetchError());
			}
		}
		if (opt("output"))
		{
			outputfile = opt["output"];
		}

		switch (command)
		{
			case CmdStoreModel:
				if (opt.nofargs() > 1) throw strus::runtime_error(_TXT("too many arguments for command '%s'"), commandName( command));
				doProcessFeatures( dbi, vsi, config, format, inputfile, false);
			break;
			case CmdLearnFeatures:
				if (opt.nofargs() > 1) throw strus::runtime_error(_TXT("too many arguments for command '%s'"), commandName( command));
				doProcessFeatures( dbi, vsi, config, format, inputfile, true);
			break;
			case CmdMapFeatures:
				if (opt.nofargs() > 1) throw strus::runtime_error(_TXT("too many arguments for command '%s'"), commandName( command));
				doMapFeatures( dbi, vsi, config, format, inputfile);
			break;
			case CmdMapClasses:
				if (opt.nofargs() > 1) throw strus::runtime_error(_TXT("too many arguments for command '%s'"), commandName( command));
				doMapClasses( dbi, vsi, config, format, inputfile);
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


