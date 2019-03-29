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
#include "strus/vectorStorageInterface.hpp"
#include "strus/vectorStorageClientInterface.hpp"
#include "strus/vectorStorageDumpInterface.hpp"
#include "strus/valueIteratorInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "strus/reference.hpp"
#include "strus/constants.hpp"
#include "strus/wordVector.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "private/programLoader.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/configParser.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/numstring.hpp"
#include "strus/base/local_ptr.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <ctime>
#include <sys/time.h>
#include <cerrno>
#include <cstdio>
#include <limits>


static strus::ErrorBufferInterface* g_errorBuffer = 0;
static double g_minSimilarity = 0.85;
static double g_speedRecallFactor = 0.9;
static bool g_withRealSimilarityMeasure = false;

static void printVectorStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& config, strus::ErrorBufferInterface* errorhnd)
{
	std::string configstr( config);
	std::string dbname;
	std::string storagename;
	(void)strus::extractStringFromConfigString( dbname, configstr, "database", errorhnd);
	if (!strus::extractStringFromConfigString( storagename, configstr, "storage", errorhnd))
	{
		storagename = strus::Constants::standard_vector_storage();
		if (errorhnd->hasError()) throw strus::runtime_error("failed get vector space storage type from configuration");
	}
	if (errorhnd->hasError()) throw strus::runtime_error(_TXT("cannot evaluate database: %s"), errorhnd->fetchError());

	strus::local_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
	if (!dbi) throw std::runtime_error( _TXT("failed to get database interface"));
	const strus::VectorStorageInterface* sti = storageBuilder->getVectorStorage( storagename);
	if (!sti) throw std::runtime_error( _TXT("failed to get storage interface"));

	std::string storageInfo = strus::string_format(  "storage=<type of storage (optional, default '%s')>", strus::Constants::standard_vector_storage());
	strus::printIndentMultilineString(
				out, 12, storageInfo.c_str(), errorhnd);
	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient), errorhnd);
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::VectorStorageInterface::CmdCreateClient), errorhnd);
}

static double getTimeStamp()
{
	struct timeval now;
	gettimeofday( &now, NULL);
	return (double)now.tv_usec / 1000000.0 + now.tv_sec;
}

static void printResultVector( const strus::WordVector& vec)
{
	std::ostringstream out;
	strus::WordVector::const_iterator vi = vec.begin(), ve = vec.end();
	for (unsigned int vidx=0; vi != ve; ++vi,++vidx)
	{
		if (vidx) out << " ";
		char buf[ 32];
		std::snprintf( buf, sizeof(buf), "%.5f", *vi);
		out << buf;
	}
	std::cout << out.str() << std::endl;
}

static void printFloat( float val)
{
	char buf[ 32];
	std::snprintf( buf, sizeof(buf), "%.5f", val);
	std::cout << buf << std::endl;
}

template<class Type>
static void printArray( const std::vector<Type>& ar)
{
	typename std::vector<Type>::const_iterator ci = ar.begin(), ce = ar.end();
	for (int cidx=0; ci != ce; ++ci,++cidx)
	{
		if (cidx) std::cout << " ";
		std::cout << *ci;
	}
	std::cout << std::endl;
}

static strus::WordVector parseNextVectorOperand( const strus::VectorStorageClientInterface* storage, std::size_t& argidx, const char** inspectarg, std::size_t inspectargsize)
{
	strus::WordVector rt;
	if (argidx >= inspectargsize)
	{
		throw std::runtime_error( _TXT("unexpected end of arguments"));
	}
	char sign = '+';
	std::string type;
	std::string feat;
	if (inspectarg[ argidx][0] == '+' || inspectarg[ argidx][0] == '-')
	{
		sign = inspectarg[ argidx][0];
		if (inspectarg[ argidx][1])
		{
			type = inspectarg[ argidx]+1;
			if (++argidx == inspectargsize) throw std::runtime_error( _TXT( "unexpected end of arguments"));
			feat = inspectarg[ argidx];
		}
		else
		{
			if (++argidx == inspectargsize) throw std::runtime_error( _TXT( "unexpected end of arguments"));
			type = inspectarg[ argidx];
			if (++argidx == inspectargsize) throw std::runtime_error( _TXT( "unexpected end of arguments"));
			feat = inspectarg[ argidx];
		}
	}
	else
	{
		type = inspectarg[ argidx];
		if (++argidx == inspectargsize) throw std::runtime_error( _TXT( "unexpected end of arguments"));
		feat = inspectarg[ argidx];
	}
	++argidx;

	rt = storage->featureVector( type, feat);
	if (rt.empty())
	{
		throw strus::runtime_error( _TXT( "vector of feature %s '%s' not found"), type.c_str(), feat.c_str());
	}
	if (sign == '-')
	{
		strus::WordVector::iterator vi = rt.begin(), ve = rt.end();
		for (; vi != ve; ++vi)
		{
			*vi = -*vi;
		}
	}
	return rt;
}

enum VectorOperator
{
	VectorPlus,
	VectorMinus
};
static VectorOperator parseNextVectorOperator( const strus::VectorStorageClientInterface* storage, std::size_t& argidx, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectarg[ argidx][0] == '+')
	{
		if (!inspectarg[ argidx][1])
		{
			++argidx;
		}
		return VectorPlus;
	}
	if (inspectarg[ argidx][0] == '-')
	{
		if (inspectarg[ argidx][1])
		{
			return VectorPlus;
		}
		else
		{
			++argidx;
			return VectorMinus;
		}
	}
	return VectorPlus;
}

static strus::WordVector addVector( const strus::WordVector& arg1, const strus::WordVector& arg2)
{
	strus::WordVector rt;
	strus::WordVector::const_iterator i1 = arg1.begin(), e1 = arg1.end();
	strus::WordVector::const_iterator i2 = arg2.begin(), e2 = arg2.end();
	for (; i1 != e1 && i2 != e2; ++i1,++i2)
	{
		rt.push_back( *i1 + *i2);
	}
	return rt;
}

static strus::WordVector subVector( const strus::WordVector& arg1, const strus::WordVector& arg2)
{
	strus::WordVector rt;
	strus::WordVector::const_iterator i1 = arg1.begin(), e1 = arg1.end();
	strus::WordVector::const_iterator i2 = arg2.begin(), e2 = arg2.end();
	for (; i1 != e1 && i2 != e2; ++i1,++i2)
	{
		rt.push_back( *i1 - *i2);
	}
	return rt;
}

static strus::WordVector parseVectorOperation( const strus::VectorStorageClientInterface* storage, std::size_t argidx, const char** inspectarg, std::size_t inspectargsize)
{
	strus::WordVector res = parseNextVectorOperand( storage, argidx, inspectarg, inspectargsize);
	while (argidx < inspectargsize)
	{
		VectorOperator opr = parseNextVectorOperator( storage, argidx, inspectarg, inspectargsize);
		strus::WordVector arg = parseNextVectorOperand( storage, argidx, inspectarg, inspectargsize);
		switch (opr)
		{
			case VectorPlus:
				res = addVector( res, arg);
				break;
			case VectorMinus:
				res = subVector( res, arg);
				break;
		}
	}
	return storage->normalize( res);
}

static void inspectSimVector( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	strus::WordVector vec = parseVectorOperation( storage, 0, inspectarg, inspectargsize);
	printResultVector( vec);
}

static void inspectSimFeatSearch( strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize, unsigned int maxNofRanks, bool doMeasureDuration, bool withWeights)
{
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (at least one argument expected)"));
	std::string restype = inspectarg[ 0];
	strus::WordVector vec = parseVectorOperation( storage, 1, inspectarg, inspectargsize);
	std::vector<strus::VectorQueryResult> results;

	storage->prepareSearch( restype);

	double startTime = 0.0;
	if (doMeasureDuration)
	{
		startTime = getTimeStamp();
	}
	results = storage->findSimilar( restype, vec, maxNofRanks, g_minSimilarity, g_speedRecallFactor, g_withRealSimilarityMeasure);
	if (doMeasureDuration)
	{
		double endTime = getTimeStamp();
		double duration = endTime - startTime;
		std::cerr << strus::string_format( _TXT("operation duration: %.4f seconds"), duration) << std::endl;
	}
	if (withWeights)
	{
		std::vector<strus::VectorQueryResult>::const_iterator ri = results.begin(), re = results.end();
		for (; ri != re; ++ri)
		{
			char buf[ 32];
			std::snprintf( buf, sizeof(buf), "%.5f", ri->weight());
			std::cout << ri->value() << " " << buf << std::endl;
		}
	}
	else
	{
		std::vector<strus::VectorQueryResult>::const_iterator ri = results.begin(), re = results.end();
		for (; ri != re; ++ri)
		{
			std::cout << ri->value() << std::endl;
		}
	}
}

// Inspect strus::VectorStorageClientInterface::types()
static void inspectTypes( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	printArray( storage->types());
}

// Inspect strus::VectorStorageClientInterface::nofTypes()
static void inspectNofTypes( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	std::cout << storage->nofTypes() << std::endl;
}

// Inspect strus::VectorStorageClientInterface::nofValues()
static void inspectNofValues( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	std::cout << storage->nofFeatures() << std::endl;
}

// Inspect strus::VectorStorageClientInterface::featureTypes()
static void inspectFeatureTypes( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 1) throw std::runtime_error( _TXT("too many arguments (only feature name as argument expected)"));
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (feature name as first argument expected)"));
	std::string featstr = inspectarg[0];

	printArray( storage->featureTypes( featstr));
}

// Inspect some feature values starting with a lower bound specified:
static void inspectFeatureValues( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize, unsigned int maxNofRanks)
{
	if (inspectargsize > 2) throw std::runtime_error( _TXT("too many arguments (maximum two feature type and optionally starting feature value lower bound expected)"));
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (feature name as first argument expected)"));
	std::string type = inspectarg[0];
	std::string featprefix;
	if (inspectargsize == 2)
	{
		featprefix = inspectarg[1];
	}
	strus::local_ptr<strus::ValueIteratorInterface> valItr( storage->createFeatureValueIterator());
	if (!valItr.get()) throw std::runtime_error(_TXT("failed to create feature value iterator"));
	if (!featprefix.empty())
	{
		valItr->skip( featprefix.c_str(), featprefix.size());
	}
	std::vector<std::string> featValues = valItr->fetchValues( maxNofRanks);

	for (std::vector<std::string>::const_iterator it = featValues.begin(); it != featValues.end(); it++)
	{
		std::cout << *it << std::endl;
	}
}

// Inspect strus::VectorStorageClientInterface::featureVector()
static void inspectFeatureSimilarity( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 4) throw strus::runtime_error(_TXT("too many arguments (%u arguments expected)"), 4U);
	if (inspectargsize < 3) throw strus::runtime_error(_TXT("too few arguments (at least %u arguments expected)"), 3U);
	std::string type1 = inspectarg[0];
	std::string feat1 = inspectarg[1];
	std::string type2 = inspectarg[2];
	std::string feat2;
	if (inspectargsize == 4)
	{
		feat2 = inspectarg[3];
	}
	else
	{
		std::swap( feat2, type2);
		type2 = type1;
	}
	strus::WordVector v1 = storage->featureVector( type1, feat1);
	strus::WordVector v2 = storage->featureVector( type2, feat2);
	if (v1.empty() || v2.empty())
	{
		std::cout << "0" << std::endl;
	}
	else
	{
		printFloat( storage->vectorSimilarity( v1, v2));
	}
}

// Inspect strus::VectorStorageClientInterface::featureVector()
static void inspectFeatureVector( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 2) throw strus::runtime_error(_TXT("too many arguments (%u arguments expected)"), 2U);
	if (inspectargsize < 2) throw strus::runtime_error(_TXT("too few arguments (at least %u arguments expected)"), 2U);
	std::string type = inspectarg[0];
	std::string feat = inspectarg[1];

	strus::WordVector vec = storage->featureVector( type, feat);
	printResultVector( vec);
}

// Inspect strus::VectorStorageClientInterface::nofVectors()
static void inspectNofVectors( const strus::VectorStorageClientInterface* storage, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 1) throw strus::runtime_error(_TXT("too many arguments (%u arguments expected)"), 2U);
	if (inspectargsize == 1)
	{
		std::string type = inspectarg[0];
		std::cout << storage->nofVectors( type) << std::endl;
	}
	else
	{
		std::vector<std::string> types = storage->types();
		std::vector<std::string>::const_iterator ti = types.begin(), te = types.end();
		for (; ti != te; ++ti)
		{
			std::cout << *ti << " " << storage->nofVectors( *ti) << std::endl;
		}
	}
}

// Inspect strus::VectorStorageClientInterface::config()
static void inspectConfig( const strus::VectorStorageClientInterface* storage, const char**, std::size_t inspectargsize)
{
	if (inspectargsize) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	std::cout << storage->config() << std::endl;
}

// Inspect dump of vector storage with VectorStorageDumpInterface
static void inspectDump( const strus::VectorStorageInterface* vsi, const strus::DatabaseInterface* dbi, const std::string& config, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	strus::local_ptr<strus::VectorStorageDumpInterface> dumpitr( vsi->createDump( config, dbi));
	const char* chunk;
	std::size_t chunksize;
	while (dumpitr->nextChunk( chunk, chunksize))
	{
		std::cout << std::string( chunk, chunksize);
		if (g_errorBuffer->hasError()) throw std::runtime_error( _TXT("error dumping vector storage to stdout"));
	}
}

int main( int argc, const char* argv[])
{
	int rt = 0;
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
	g_errorBuffer = errorBuffer.get();

	try
	{
		bool printUsageAndExit = false;
		strus::ProgramOptions opt(
				errorBuffer.get(), argc, argv, 14,
				"h,help", "v,version", "license",
				"G,debug:", "m,module:", "M,moduledir:", "T,trace:",
				"s,config:", "S,configfile:",
				"D,time", "N,nofranks:",
				"Z,minsim:", "Y,recall", "X,realmeasure");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}
		if (opt( "help")) printUsageAndExit = true;

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

		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw std::runtime_error( _TXT("failed to create module loader"));
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
#if STRUS_VECTOR_STD_ENABLED
		if (!moduleLoader->loadModule( strus::Constants::standard_vector_storage_module()))
		{
			std::cerr << _TXT("failed to load module ") << "'" << strus::Constants::standard_vector_storage_module() << "': " << errorBuffer->fetchError() << std::endl;
		}
#endif
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
			std::cout << _TXT("usage:") << " strusInspectVectorStorage [options] <what...>" << std::endl;
			std::cout << "<what>    : " << _TXT("what to inspect:") << std::endl;

			std::cout << "            \"noftypes\"" << std::endl;
			std::cout << "               = " << _TXT("Return the number of types defined in the storage.") << std::endl;
			std::cout << "            \"nofvalues\"" << std::endl;
			std::cout << "               = " << _TXT("Return the number of features defined in the storage.") << std::endl;
			std::cout << "            \"types\"" << std::endl;
			std::cout << "               = " << _TXT("Return feature types defined in the storage.") << std::endl;
			std::cout << "            \"feattypes\" <featname>" << std::endl;
			std::cout << "               = " << _TXT("Return all types assigned to a feature value.") << std::endl;
			std::cout << "            \"featvalues\" <type> [<featname lowerbound>]" << std::endl;
			std::cout << "               = " << _TXT("Return some (nofranks) feature values of a type.") << std::endl;
			std::cout << "                 " << _TXT("Start of result list specified with a lower bound value.") << std::endl;
			std::cout << "            \"featsim\" <feat 1 type> <feat 1 name> <feat 2 type> <feat 2 name>" << std::endl;
			std::cout << "            \"featsim\" <feat type> <feat 1 name> <feat 2 name>" << std::endl;
			std::cout << "               = " << _TXT("Return the cosine distance of two") << std::endl;
			std::cout << "                 " << _TXT("features in  the storage.") << std::endl;
			std::cout << "            \"featvec\" <feat type> <feat name>" << std::endl;
			std::cout << "               = " << _TXT("Return the vector associated with a") << std::endl;
			std::cout << "                 " << _TXT("feature the storage.") << std::endl;
			std::cout << "            \"nofvec\" [<feat type>]" << std::endl;
			std::cout << "               = " << _TXT("Return the number of vectors associated with") << std::endl;
			std::cout << "                 " << _TXT("features the storage.") << std::endl;
			std::cout << "            \"opvec\" <feat type> <feat value> { '+'/'-' <feat type> <feat value> }" << std::endl;
			std::cout << "               = " << _TXT("Return the vector resulting from an addition of") << std::endl;
			std::cout << "                 " << _TXT("vectors in the storage.") << std::endl;
			std::cout << "            \"opfeat\" <result type> <feat type> <feat value> { '+'/'-' <feat type> <feat value> }" << std::endl;
			std::cout << "               = " << _TXT("Return the most similar features to a result of an") << std::endl;
			std::cout << "                 " << _TXT("addition of vectors in the storage.") << std::endl;
			std::cout << "            \"opfeatw\" <result type> <feat type> <feat value> { '+'/'-' <feat type> <feat value> }" << std::endl;
			std::cout << "               = " << _TXT("Same as 'opfeat' but also returning the weights.") << std::endl;
			std::cout << "            \"config\"" << std::endl;
			std::cout << "               = " << _TXT("Get the configuration the vector storage.") << std::endl;
			std::cout << "            \"dump\"" << std::endl;
			std::cout << "               = " << _TXT("Dump the contents of the storage.") << std::endl;
			std::cout << _TXT("description: Inspects some data defined in a vector storage.") << std::endl;
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
			std::cout << "    " << _TXT("Load components from module <MOD>.") << std::endl;
			std::cout << "    " << _TXT("The module modstrus_storage_vector is implicitely defined") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-s|--config <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the vector storage configuration string as <CONFIG>") << std::endl;
			std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
			printVectorStorageConfigOptions( std::cout, moduleLoader.get(), config, errorBuffer.get());
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    " << _TXT("Define the vector storage configuration file as <FILENAME>") << std::endl;
			std::cout << "    " << _TXT("<FILENAME> is a file containing the configuration string") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			std::cout << "-D|--time" << std::endl;
			std::cout << "    " << _TXT("Do measure duration of operation (only for search)") << std::endl;
			std::cout << "-N|--nofranks <N>" << std::endl;
			std::cout << "    " << _TXT("Limit the number of results to for searches to <N> (default 20)") << std::endl;
			std::cout << "-Y|--recall <RC>" << std::endl;
			std::cout << "    " << _TXT("Factor used for prunning candidates when comparing LSH samples") << std::endl;
			std::cout << "    " << _TXT("(default 0.9)") << std::endl;
			std::cout << "-Z|--minsim <SIM>" << std::endl;
			std::cout << "    " << _TXT("Minimum similarity for vector search") << std::endl;
			std::cout << "-X|--realmeasure" << std::endl;
			std::cout << "    " << _TXT("Calculate real values of similarities for search and compare") << std::endl;
			std::cout << "    " << _TXT("of methods 'opfeat','opfeatname','opfeatw' and 'opfeatwname'.") << std::endl;
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
		g_withRealSimilarityMeasure = opt("realmeasure");
		if (opt("minsim"))
		{
			g_minSimilarity = opt.asDouble("minsim");
			if (g_minSimilarity < 0.0 || g_minSimilarity >= 1.0)
			{
				throw strus::runtime_error( _TXT("value of option %s out of range"), "--minsim|-Z");
			}
		}
		if (opt("recall"))
		{
			g_speedRecallFactor = opt.asDouble("recall");
			if (g_speedRecallFactor < 0.0)
			{
				throw strus::runtime_error( _TXT("value of option %s out of range"), "--recall|-Z");
			}
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}

		// Create root object:
		strus::local_ptr<strus::StorageObjectBuilderInterface>
			storageBuilder( moduleLoader->createStorageObjectBuilder());
		if (!storageBuilder.get()) throw std::runtime_error( _TXT("failed to create storage object builder"));

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::StorageObjectBuilderInterface* sproxy = (*ti)->createProxy( storageBuilder.get());
			storageBuilder.release();
			storageBuilder.reset( sproxy);
		}
		// Create objects:
		std::string storagename;
		if (!strus::extractStringFromConfigString( storagename, config, "storage", errorBuffer.get()))
		{
			storagename = strus::Constants::standard_vector_storage();
			if (errorBuffer->hasError()) throw strus::runtime_error("failed get vector space storage type from configuration");
		}
		bool doMeasureDuration = opt( "time");
		unsigned int maxNofRanks = 20;
		if (opt("nofranks"))
		{
			maxNofRanks = opt.asUint("nofranks");
		}
		std::string dbname;
		(void)strus::extractStringFromConfigString( dbname, config, "database", errorBuffer.get());
		if (errorBuffer->hasError()) throw std::runtime_error( _TXT("cannot evaluate database"));

		const strus::VectorStorageInterface* vsi = storageBuilder->getVectorStorage( storagename);
		if (!vsi) throw std::runtime_error( _TXT("failed to get vector storage interface"));
		const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
		if (!dbi) throw std::runtime_error( _TXT("failed to get database interface"));

		strus::local_ptr<strus::VectorStorageClientInterface> storage( vsi->createClient( config, dbi));
		if (!storage.get()) throw std::runtime_error( _TXT("failed to create vector space storage client interface"));

		std::string what = opt[0];
		const char** inspectarg = opt.argv() + 1;
		std::size_t inspectargsize = opt.nofargs() - 1;

		// Do inspect what is requested:
		if (strus::caseInsensitiveEquals( what, "types"))
		{
			inspectTypes( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "noftypes"))
		{
			inspectNofTypes( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "nofvalues"))
		{
			inspectNofValues( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "feattypes"))
		{
			inspectFeatureTypes( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featvalues"))
		{
			inspectFeatureValues( storage.get(), inspectarg, inspectargsize, maxNofRanks);
		}
		else if (strus::caseInsensitiveEquals( what, "featsim"))
		{
			inspectFeatureSimilarity( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featvec"))
		{
			inspectFeatureVector( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "nofvec"))
		{
			inspectNofVectors( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "opvec"))
		{
			inspectSimVector( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "opfeat"))
		{
			inspectSimFeatSearch( storage.get(), inspectarg, inspectargsize, maxNofRanks, doMeasureDuration, false/*with weights*/);
		}
		else if (strus::caseInsensitiveEquals( what, "opfeatw"))
		{
			inspectSimFeatSearch( storage.get(), inspectarg, inspectargsize, maxNofRanks, doMeasureDuration, true/*with weights*/);
		}
		else if (strus::caseInsensitiveEquals( what, "config"))
		{
			inspectConfig( storage.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "dump"))
		{
			inspectDump( vsi, dbi, config, inspectarg, inspectargsize);
		}
		else
		{
			throw strus::runtime_error( _TXT( "unknown item to inspect '%s'"), what.c_str());
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("unhandled error in command"));
		}
		std::cerr << _TXT("done.") << std::endl;
		if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
		{
			std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
		}
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
		return -2;
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
	if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
	{
		std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
	}
	return -1;
}


