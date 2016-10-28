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
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <stdexcept>

#undef STRUS_LOWLEVEL_DEBUG
#define DEFAULT_LOAD_MODULE   "modstrus_storage_vectorspace_std"
#define DEFAULT_VECTOR_MODEL  "vector_std"

static strus::ErrorBufferInterface* g_errorBuffer = 0;

static strus::Index getFeatureIndex( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char* inspectarg)
{
	strus::Index idx;
	if (inspectarg[0] == '#' && inspectarg[1] >= '0' && inspectarg[1] <= '9')
	{
		idx = strus::utils::toint( inspectarg+1);
		if (idx < 0) strus::runtime_error(_TXT("feature number must not be negative"));
	}
	else
	{
		idx = vsmodel->featureIndex( inspectarg);
		if (idx < 0)
		{
			if (g_errorBuffer->hasError()) throw strus::runtime_error(_TXT("feature with name '%s' could not be retrieved"), inspectarg);
			throw strus::runtime_error(_TXT("feature with name '%s' not found"), inspectarg);
		}
	}
	return idx;
}

static void printResultVector( const std::vector<double>& vec)
{
	std::ostringstream out;
	std::vector<double>::const_iterator vi = vec.begin(), ve = vec.end();
	for (unsigned int vidx=0; vi != ve; ++vi,++vidx)
	{
		if (vidx) out << " ";
		out << *vi;
	}
	std::cout << out.str() << std::endl;
}

static void printUniqResultConcepts( const std::vector<strus::Index>& res_)
{
	std::vector<strus::Index> res( res_);
	std::sort( res.begin(), res.end());
	std::vector<strus::Index>::const_iterator ri = res.begin(), re = res.end();
	std::size_t ridx = 0;
	while (ri != re)
	{
		strus::Index uniq = *ri;
		for (; ri != re && uniq == *ri; ++ri){}

		if (ridx++) std::cout << " ";
		std::cout << *ri;
	}
	std::cout << std::endl;
}

// Inspect strus::VectorSpaceModelInstanceInterface::mapVectorToConcepts()
void inspectMapVectorToConcepts( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<double> vec;
	std::size_t ai = 0, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		vec.push_back( strus::utils::tofloat( inspectarg[ai]));
	}
	std::vector<strus::Index> car = vsmodel->mapVectorToConcepts( vec);
	if (car.empty() && g_errorBuffer->hasError())
	{
		throw strus::runtime_error(_TXT("failed to map vector to concept features"));
	}
	printUniqResultConcepts( car);
}

// Inspect strus::VectorSpaceModelInstanceInterface::featureConcepts()
void inspectFeatureConcepts( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<strus::Index> far;
	std::size_t ai = 0, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		far.push_back( getFeatureIndex( vsmodel, inspectarg[ai]));
	}
	std::vector<strus::Index> res;
	std::vector<strus::Index>::const_iterator fi = far.begin(), fe = far.end();
	for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
	{
		std::vector<strus::Index> car = vsmodel->featureConcepts( *fi);
		if (car.empty() && g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to get feature concepts"));
		}
		res.insert( res.end(), car.begin(), car.end());
	}
	printUniqResultConcepts( res);
}

// Inspect strus::VectorSpaceModelInstanceInterface::featureVector()
void inspectFeatureVector( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize < 1) throw strus::runtime_error(_TXT("too few arguments (at least %u arguments expected)"), 1U);
	if (inspectargsize > 2) throw strus::runtime_error(_TXT("too many arguments (maximum %u arguments expected)"), 2U);
	if (inspectargsize == 2)
	{
		strus::Index idx = getFeatureIndex( vsmodel, inspectarg[0]);
		std::ostringstream out;
		out << std::setprecision(6) << std::fixed;
		std::vector<double> vec = vsmodel->featureVector( idx);
		printResultVector( vec);
	}
	else
	{
		strus::Index fi = 0, fe = vsmodel->nofFeatures();
		for (; fi != fe; ++fi)
		{
			std::vector<double> vec = vsmodel->featureVector( fi);
			if (!vec.empty())
			{
				std::cout << fi << ":";
				printResultVector( vec);
			}
		}
	}
}

// Inspect strus::VectorSpaceModelInstanceInterface::featureName()
void inspectFeatureName( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<strus::Index> far;
	std::size_t ai = 0, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		if (inspectarg[ai][0] == '#' && inspectarg[ai][1] >= '0' && inspectarg[ai][1] <= '9')
		{
			std::cerr << "you do not have to specify '#', feature number expected as input" << std::endl;
			far.push_back( getFeatureIndex( vsmodel, inspectarg[ai]));
		}
		else
		{
			far.push_back( strus::utils::toint( inspectarg[ai]));
		}
	}
	std::vector<strus::Index>::const_iterator fi = far.begin(), fe = far.end();
	for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
	{
		std::string name = vsmodel->featureName( *fi);
		if (name.empty() && g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to get feature name"));
		}
		if (fidx) std::cout << " ";
		std::cout << name;
	}
	std::cout << std::endl;
}

// Inspect strus::VectorSpaceModelInstanceInterface::featureIndex()
void inspectFeatureIndex( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<strus::Index> far;
	std::size_t ai = 0, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		far.push_back( vsmodel->featureIndex( inspectarg[ai]));
		if (far.back() < 0 && g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to get feature index"));
		}
	}
	std::vector<strus::Index>::const_iterator fi = far.begin(), fe = far.end();
	for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
	{
		if (fidx) std::cout << " ";
		std::cout << *fi;
	}
	std::cout << std::endl;
}

enum FeatureResultPrintMode
{
	PrintIndex,
	PrintName,
	PrintIndexName
};

// Inspect strus::VectorSpaceModelInstanceInterface::conceptFeatures()
void inspectConceptFeatures( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize, FeatureResultPrintMode mode)
{
	std::vector<strus::Index> car;
	std::size_t ai = 0, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		car.push_back( strus::utils::toint( inspectarg[ai]));
	}
	std::vector<strus::Index> res;
	std::vector<strus::Index>::const_iterator ci = car.begin(), ce = car.end();
	for (std::size_t cidx=0; ci != ce; ++ci,++cidx)
	{
		std::vector<strus::Index> far = vsmodel->conceptFeatures( *ci);
		if (far.empty() && g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to get concept features"));
		}
		res.insert( res.end(), far.begin(), far.end());
	}
	std::sort( res.begin(), res.end());
	std::vector<strus::Index>::const_iterator ri = res.begin(), re = res.end();
	std::size_t ridx = 0;
	while (ri != re)
	{
		strus::Index uniq = *ri;
		for (; ri != re && uniq == *ri; ++ri){}

		if (ridx++) std::cout << " ";
		if (mode == PrintIndex || mode == PrintIndexName)
		{
			std::cout << *ri;
			if (mode == PrintIndexName) std::cout << ":";
		}
		if (mode == PrintName || mode == PrintIndexName)
		{
			std::cout << vsmodel->featureName( *ri);
		}
	}
}

// Inspect strus::VectorSpaceModelInstanceInterface::nofConcepts()
void inspectNofConcepts( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char**, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw strus::runtime_error(_TXT("too many arguments (no arguments expected)"), 2U);
	std::cout << vsmodel->nofConcepts() << std::endl;
}

// Inspect strus::VectorSpaceModelInstanceInterface::nofFeatures()
void inspectNofFeatures( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char**, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw strus::runtime_error(_TXT("too many arguments (no arguments expected)"), 2U);
	std::cout << vsmodel->nofFeatures() << std::endl;
}

// Inspect strus::VectorSpaceModelInstanceInterface::config()
void inspectConfig( const strus::VectorSpaceModelInstanceInterface* vsmodel, const char**, std::size_t inspectargsize)
{
	if (inspectargsize) throw strus::runtime_error(_TXT("too many arguments (no arguments expected)"));
	std::cout << vsmodel->config() << std::endl;
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
				argc, argv, 8,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:", "T,trace:",
				"s,config:", "S,configfile:" );
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
			std::cout << _TXT("usage:") << " strusInspectVsm [options] <what...>" << std::endl;
			std::cout << "<what>    : " << _TXT("what to inspect:") << std::endl;
			std::cout << "            \"mapvec\"" << std::endl;
			std::cout << "               = " << _TXT("Take a vector of double precision floats as input.") << std::endl;
			std::cout << "               = " << _TXT("Return a list of indices of concepts near it.") << std::endl;
			std::cout << "            \"featcon\"" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of feature numbers (with '#' prefix) or names as input.") << std::endl;
			std::cout << "               = " << _TXT("Return a sorted list of indices of concepts assigned to it.") << std::endl;
			std::cout << "            \"featvec\"" << std::endl;
			std::cout << "               = " << _TXT("Take a single feature number (with '#' prefix) or name as input.") << std::endl;
			std::cout << "               = " << _TXT("Return the vector assigned to it.") << std::endl;
			std::cout << "            \"featname\"" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of feature numbers as input.") << std::endl;
			std::cout << "               = " << _TXT("Return the list of names assigned to it.") << std::endl;
			std::cout << "            \"featidx\"" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of feature names as input.") << std::endl;
			std::cout << "               = " << _TXT("Return the list of indices assigned to it.") << std::endl;
			std::cout << "            \"confeat\" or \"confeatidx\" \"confeatname\"" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of concept numbers as input.") << std::endl;
			std::cout << "               = " << _TXT("Return a sorted list of features assigned to it.") << std::endl;
			std::cout << "               = " << _TXT("\"confeatidx\" prints only the result feature indices.") << std::endl;
			std::cout << "               = " << _TXT("\"confeatname\" prints only the result feature names.") << std::endl;
			std::cout << "               = " << _TXT("\"confeat\" prints both indices and names.") << std::endl;
			std::cout << "            \"nofcon\"" << std::endl;
			std::cout << "               = " << _TXT("Get the number of concepts defined.") << std::endl;
			std::cout << "            \"noffeat\"" << std::endl;
			std::cout << "               = " << _TXT("Get the number of features defined.") << std::endl;
			std::cout << "            \"config\"" << std::endl;
			std::cout << "               = " << _TXT("Get the configuration the VSM repository was created with.") << std::endl;
			std::cout << _TXT("description: Inspects some data defined in a vector space model build.") << std::endl;
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

		std::auto_ptr<strus::VectorSpaceModelInstanceInterface> vsmodel( vsi->createInstance( config, dbi));
		if (!vsmodel.get()) throw strus::runtime_error(_TXT("failed to create vector space model instance"));

		std::string what = opt[0];
		const char** inspectarg = opt.argv() + 1;
		std::size_t inspectargsize = opt.nofargs() - 1;

		// Do inspect what is requested:
		if (strus::utils::caseInsensitiveEquals( what, "mapvec"))
		{
			inspectMapVectorToConcepts( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "featcon"))
		{
			inspectFeatureConcepts( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "featvec"))
		{
			inspectFeatureVector( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "featname"))
		{
			inspectFeatureName( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "featidx"))
		{
			inspectFeatureIndex( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "confeatidx"))
		{
			inspectConceptFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintIndex);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "confeatname"))
		{
			inspectConceptFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintName);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "confeat"))
		{
			inspectConceptFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintIndexName);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "nofcon"))
		{
			inspectNofConcepts( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "noffeat"))
		{
			inspectNofFeatures( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "config"))
		{
			inspectConfig( vsmodel.get(), inspectarg, inspectargsize);
		}
		else
		{
			throw strus::runtime_error( _TXT( "unknown item to inspect '%s'"), what.c_str());
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


