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
#include "strus/vectorStorageSearchInterface.hpp"
#include "strus/vectorStorageDumpInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionBase.hpp"
#include "strus/reference.hpp"
#include "strus/constants.hpp"
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
#include "strus/base/thread.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <ctime>
#include <sys/time.h>
#include <cerrno>
#include <cstdio>
#include <limits>

#undef STRUS_LOWLEVEL_DEBUG
#define FEATNUM_PREFIX_CHAR   '%'

static strus::ErrorBufferInterface* g_errorBuffer = 0;

static double getTimeStamp()
{
	struct timeval now;
	gettimeofday( &now, NULL);
	return (double)now.tv_usec / 1000000.0 + now.tv_sec;
}

static strus::Index getFeatureIndex( const strus::VectorStorageClientInterface* vsmodel, const char* inspectarg)
{
	strus::Index idx;
	if (inspectarg[0] == FEATNUM_PREFIX_CHAR && inspectarg[1] >= '0' && inspectarg[1] <= '9')
	{
		idx = strus::numstring_conv::toint( inspectarg+1, std::numeric_limits<strus::Index>::max());
		if (idx < 0) std::runtime_error( _TXT("feature number must not be negative"));
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
		std::cout << uniq;
	}
	std::cout << std::endl;
}

enum FeatureResultPrintMode
{
	PrintIndex,
	PrintName,
	PrintIndexName
};

static void printResultFeatures( const strus::VectorStorageClientInterface* vsmodel, const std::vector<strus::Index>& res_, FeatureResultPrintMode mode, bool sortuniq)
{
	std::vector<strus::Index> res( res_);
	if (sortuniq)
	{
		std::sort( res.begin(), res.end());
	}
	std::vector<strus::Index>::const_iterator ri = res.begin(), re = res.end();
	std::size_t ridx = 0;
	while (ri != re)
	{
		strus::Index uniq = *ri;
		for (; ri != re && uniq == *ri; ++ri){}
	
		if (ridx++) std::cout << " ";
		if (mode == PrintIndex || mode == PrintIndexName)
		{
			std::cout << uniq;
			if (mode == PrintIndexName) std::cout << ":";
		}
		if (mode == PrintName || mode == PrintIndexName)
		{
			std::cout << vsmodel->featureName( uniq);
		}
	}
	std::cout << std::endl;
}

static void printResultFeaturesWithWeights( const strus::VectorStorageClientInterface* vsmodel, const std::vector<strus::VectorQueryResult>& res, FeatureResultPrintMode mode)
{
	std::vector<strus::VectorQueryResult>::const_iterator ri = res.begin(), re = res.end();
	for (;ri != re; ++ri)
	{
		if (mode == PrintIndex || mode == PrintIndexName)
		{
			std::cout << ri->featidx();
			if (mode == PrintIndexName) std::cout << ":";
		}
		if (mode == PrintName || mode == PrintIndexName)
		{
			std::cout << vsmodel->featureName( ri->featidx());
		}
		std::ostringstream weightbuf;
		weightbuf << std::fixed << std::setprecision(6) << ri->weight();
		std::cout << " " << weightbuf.str() << std::endl;
	}
}

static std::vector<double> parseNextVectorOperand( const strus::VectorStorageClientInterface* vsmodel, std::size_t& argidx, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<double> rt;
	if (argidx >= inspectargsize)
	{
		throw std::runtime_error( _TXT("unexpected end of arguments"));
	}
	char sign = '+';
	const char* argptr = 0;
	if (inspectarg[ argidx][0] == '+' || inspectarg[ argidx][0] == '-')
	{
		sign = inspectarg[ argidx][0];
		if (inspectarg[ argidx][1])
		{
			argptr = inspectarg[ argidx]+1;
		}
		else
		{
			if (++argidx == inspectargsize) throw std::runtime_error( _TXT( "unexpected end of arguments"));
			argptr = inspectarg[ argidx];
		}
		++argidx;
	}
	else
	{
		argptr = inspectarg[ argidx];
		++argidx;
	}
	strus::Index featidx = getFeatureIndex( vsmodel, argptr);
	rt = vsmodel->featureVector( featidx);
	if (sign == '-')
	{
		std::vector<double>::iterator vi = rt.begin(), ve = rt.end();
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
static VectorOperator parseNextVectorOperator( const strus::VectorStorageClientInterface* vsmodel, std::size_t& argidx, const char** inspectarg, std::size_t inspectargsize)
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

static std::vector<double> addVector( const std::vector<double>& arg1, const std::vector<double>& arg2)
{
	std::vector<double> rt;
	std::vector<double>::const_iterator i1 = arg1.begin(), e1 = arg1.end();
	std::vector<double>::const_iterator i2 = arg2.begin(), e2 = arg2.end();
	for (; i1 != e1 && i2 != e2; ++i1,++i2)
	{
		rt.push_back( *i1 + *i2);
	}
	return rt;
}

static std::vector<double> subVector( const std::vector<double>& arg1, const std::vector<double>& arg2)
{
	std::vector<double> rt;
	std::vector<double>::const_iterator i1 = arg1.begin(), e1 = arg1.end();
	std::vector<double>::const_iterator i2 = arg2.begin(), e2 = arg2.end();
	for (; i1 != e1 && i2 != e2; ++i1,++i2)
	{
		rt.push_back( *i1 - *i2);
	}
	return rt;
}

class VectorStorageSearchCosimReal
	:public strus::VectorStorageSearchInterface
{
public:
	VectorStorageSearchCosimReal( const strus::VectorStorageClientInterface* vsmodel_, const strus::Index& range_from_, const strus::Index& range_to_)
		:vsmodel(vsmodel_),range_from(range_from_),range_to(range_to_){}
	virtual ~VectorStorageSearchCosimReal(){}

	static void insertResultSet( std::set<strus::VectorQueryResult>& reslist, unsigned int maxNofResults, double sim, const strus::Index& fidx)
	{
		if (reslist.size() >= maxNofResults)
		{
			if (reslist.rbegin()->weight() < sim)
			{
				reslist.insert( strus::VectorQueryResult( fidx, sim));
				reslist.erase( reslist.begin());
			}
		}
		else
		{
			reslist.insert( strus::VectorQueryResult( fidx, sim));
		}
	}

	virtual std::vector<strus::VectorQueryResult> findSimilar( const std::vector<double>& vec, unsigned int maxNofResults) const
	{
		try
		{
			std::set<strus::VectorQueryResult> reslist;
			for (strus::Index fidx=range_from; fidx<range_to; ++fidx)
			{
				std::vector<double> candidate_vec = vsmodel->featureVector( fidx);
				if (candidate_vec.empty())
				{
					throw strus::runtime_error(_TXT("vector for feature %u not found"), (unsigned int)fidx);
				}
				double sim = vsmodel->vectorSimilarity( vec, candidate_vec);
				if (sim > 0.7)
				{
					insertResultSet( reslist, maxNofResults, sim, fidx);
				}
			}
			return std::vector<strus::VectorQueryResult>( reslist.rbegin(), reslist.rend());
		}
		CATCH_ERROR_MAP_RETURN( _TXT("error in find similar: %s"), *g_errorBuffer, std::vector<strus::VectorQueryResult>());
	}

	virtual std::vector<strus::VectorQueryResult> findSimilarFromSelection( const std::vector<strus::Index>& candidates, const std::vector<double>& vec, unsigned int maxNofResults) const
	{
		try
		{
			std::set<strus::VectorQueryResult> reslist;
			std::vector<strus::Index>::const_iterator ci = candidates.begin(), ce = candidates.end();
			for (; ci != ce; ++ci)
			{
				if (*ci >= range_from && *ci < range_to)
				{
					std::vector<double> candidate_vec = vsmodel->featureVector( *ci);
					double sim = vsmodel->vectorSimilarity( vec, candidate_vec);
					insertResultSet( reslist, maxNofResults, sim, *ci);
				}
			}
			return std::vector<strus::VectorQueryResult>( reslist.rbegin(), reslist.rend());
		}
		CATCH_ERROR_MAP_RETURN( _TXT("error in find similar: %s"), *g_errorBuffer, std::vector<strus::VectorQueryResult>());
	}

	virtual void close(){}

private:
	const strus::VectorStorageClientInterface* vsmodel;
	strus::Index range_from;
	strus::Index range_to;
};


class FindSimProcess
{
public:
	explicit FindSimProcess( strus::VectorStorageSearchInterface* searcher_)
		:m_searcher( searcher_){}
	FindSimProcess( const FindSimProcess& o)
		:m_searcher(o.m_searcher),m_feats(o.m_feats){}

	void run( const std::vector<double>& vec, unsigned int maxNofRanks)
	{
		m_feats = m_searcher->findSimilar( vec, maxNofRanks);
	}

	const std::vector<strus::VectorQueryResult>& results() const
	{
		return m_feats;
	}

private:
	strus::Reference<strus::VectorStorageSearchInterface> m_searcher;
	std::vector<strus::VectorQueryResult> m_feats;
};

static void inspectVectorOperations( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize, FeatureResultPrintMode mode, unsigned int maxNofRanks, unsigned int nofThreads, bool doMeasureDuration, bool withWeights, bool withRealSimilarityMeasure)
{
	if (inspectargsize == 0) throw std::runtime_error( _TXT("too few arguments (at least one argument expected)"));
	std::size_t argidx=0;
	std::vector<double> res = parseNextVectorOperand( vsmodel, argidx, inspectarg, inspectargsize);
	while (argidx < inspectargsize)
	{
		VectorOperator opr = parseNextVectorOperator( vsmodel, argidx, inspectarg, inspectargsize);
		std::vector<double> arg = parseNextVectorOperand( vsmodel, argidx, inspectarg, inspectargsize);
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
	std::vector<strus::VectorQueryResult> results;
	std::vector<strus::Index> feats;

	if (nofThreads == 0)
	{
		double startTime = 0.0;
		strus::Reference<strus::VectorStorageSearchInterface> searcher;
		if (withRealSimilarityMeasure)
		{
			searcher.reset( new VectorStorageSearchCosimReal( vsmodel, 0, vsmodel->nofFeatures()));
		}
		else
		{
			searcher.reset( vsmodel->createSearcher( 0, vsmodel->nofFeatures()));
		}
		if (doMeasureDuration)
		{
			startTime = getTimeStamp();
		}
		results = searcher->findSimilar( res, maxNofRanks);
		if (doMeasureDuration)
		{
			double endTime = getTimeStamp();
			double duration = endTime - startTime;
			std::cerr << strus::string_format( _TXT("operation duration: %.4f seconds"), duration) << std::endl;
		}
	}
	else
	{
		double startTime = 0.0;
		unsigned int chunksize = (vsmodel->nofFeatures() + nofThreads - 1) / nofThreads;
		std::vector<FindSimProcess> procar;
		{
			unsigned int ti = 0, te = nofThreads;
			for (unsigned int range_from=0; ti != te; ++ti,range_from+=chunksize)
			{
				if (withRealSimilarityMeasure)
				{
					procar.push_back( FindSimProcess( new VectorStorageSearchCosimReal( vsmodel, range_from, range_from + chunksize)));
				}
				else
				{
					procar.push_back( FindSimProcess( vsmodel->createSearcher( range_from, range_from + chunksize)));
				}
			}
		}
		if (doMeasureDuration)
		{
			startTime = getTimeStamp();
		}
		std::vector<strus::Reference<strus::thread> > threadGroup;
		int ti = 0, te = procar.size();
		for (ti=0; ti<te; ++ti)
		{
			strus::Reference<strus::thread> th( new strus::thread( &FindSimProcess::run, &procar[ti], res, maxNofRanks));
			threadGroup.push_back( th);
		}
		std::vector<strus::Reference<strus::thread> >::iterator gi = threadGroup.begin(), ge = threadGroup.end();
		for (; gi != ge; ++gi) (*gi)->join();

		std::vector<FindSimProcess>::const_iterator pi = procar.begin(), pe = procar.end();
		for (; pi != pe; ++pi)
		{
			results.insert( results.end(), pi->results().begin(), pi->results().end());
		}
		std::sort( results.begin(), results.end());
		results.resize( std::min( maxNofRanks, (unsigned int)results.size()));
		if (doMeasureDuration)
		{
			double endTime = getTimeStamp();
			double duration = endTime - startTime;
			std::cerr << strus::string_format( _TXT("operation duration: %.4f seconds"), duration) << std::endl;
		}
	}
	if (withWeights)
	{
		printResultFeaturesWithWeights( vsmodel, results, mode);
	}
	else
	{
		std::vector<strus::VectorQueryResult>::const_iterator ri = results.begin(), re = results.end();
		for (; ri != re; ++ri)
		{
			feats.push_back( ri->featidx());
		}
		printResultFeatures( vsmodel, feats, mode, false);
	}
}

// Inspect strus::VectorStorageClientInterface::conceptClassNames()
static void inspectConceptClassNames( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	std::vector<std::string> clnames = vsmodel->conceptClassNames();
	std::vector<std::string>::const_iterator ci = clnames.begin(), ce = clnames.end();
	for (; ci != ce; ++ci)
	{
		std::cout << *ci << " ";
	}
	std::cout << std::endl;
}

// Inspect strus::VectorStorageClientInterface::featureConcepts()
static void inspectFeatureConcepts( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<strus::Index> far;
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (class name as first argument expected)"));
	std::string clname = inspectarg[0];
	std::size_t ai = 1, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		far.push_back( getFeatureIndex( vsmodel, inspectarg[ai]));
	}
	std::vector<strus::Index> res;
	std::vector<strus::Index>::const_iterator fi = far.begin(), fe = far.end();
	for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
	{
		std::vector<strus::Index> car = vsmodel->featureConcepts( clname, *fi);
		if (car.empty() && g_errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("failed to get feature concepts"));
		}
		res.insert( res.end(), car.begin(), car.end());
	}
	printUniqResultConcepts( res);
}

// Inspect strus::VectorStorageClientInterface::featureVector()
static void inspectFeatureVector( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 1) throw strus::runtime_error(_TXT("too many arguments (maximum %u arguments expected)"), 1U);
	if (inspectargsize == 1)
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
				std::cout << fi << " ";
				printResultVector( vec);
			}
		}
	}
}

// Inspect strus::VectorStorageClientInterface::featureName()
static void inspectFeatureName( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 0)
	{
		std::vector<strus::Index> far;
		std::size_t ai = 0, ae = inspectargsize;
		for (; ai != ae; ++ai)
		{
			if (inspectarg[ai][0] == FEATNUM_PREFIX_CHAR && inspectarg[ai][1] >= '0' && inspectarg[ai][1] <= '9')
			{
				std::cerr << strus::string_format( _TXT("you do not have to specify '%c', feature number expected as input"), FEATNUM_PREFIX_CHAR) << std::endl;
				far.push_back( getFeatureIndex( vsmodel, inspectarg[ai]));
			}
			else
			{
				far.push_back( strus::numstring_conv::toint( inspectarg[ai], std::numeric_limits<strus::Index>::max()));
			}
		}
		std::vector<strus::Index>::const_iterator fi = far.begin(), fe = far.end();
		for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
		{
			std::string name = vsmodel->featureName( *fi);
			if (name.empty() && g_errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to get feature name"));
			}
			if (fidx) std::cout << " ";
			std::cout << name;
		}
		std::cout << std::endl;
	}
	else
	{
		strus::Index fi = 0, fe = vsmodel->nofFeatures();
		for (; fi != fe; ++fi)
		{
			std::cout << fi << " " << vsmodel->featureName( fi) << std::endl;
		}
	}
}

// Inspect strus::VectorStorageClientInterface::featureIndex()
static void inspectFeatureIndex( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	std::vector<strus::Index> far;
	std::size_t ai = 0, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		far.push_back( vsmodel->featureIndex( inspectarg[ai]));
		if (far.back() < 0 && g_errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("failed to get feature index"));
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

// Inspect strus::VectorStorageClientInterface::conceptFeatures()
static void inspectConceptFeatures( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize, FeatureResultPrintMode mode)
{
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (class name as first argument expected)"));
	std::string clname = inspectarg[0];

	if (inspectargsize > 1)
	{
		std::vector<strus::Index> car;
		std::size_t ai = 1, ae = inspectargsize;
		for (; ai != ae; ++ai)
		{
			car.push_back( strus::numstring_conv::toint( inspectarg[ai], std::numeric_limits<strus::Index>::max()));
		}
		std::vector<strus::Index> res;
		std::vector<strus::Index>::const_iterator ci = car.begin(), ce = car.end();
		for (std::size_t cidx=0; ci != ce; ++ci,++cidx)
		{
			std::vector<strus::Index> far = vsmodel->conceptFeatures( clname, *ci);
			if (far.empty() && g_errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to get concept features"));
			}
			res.insert( res.end(), far.begin(), far.end());
		}
		printResultFeatures( vsmodel, res, mode, true);
	}
	else
	{
		strus::Index ci = 1, ce = vsmodel->nofConcepts( clname)+1;
		for (; ci != ce; ++ci)
		{
			std::vector<strus::Index> far = vsmodel->conceptFeatures( clname, ci);
			if (far.empty()) continue;
			std::cout << ci << " ";
			printResultFeatures( vsmodel, far, mode, true);
		}
	}
}

// Inspect strus::VectorStorageClientInterface::featureConcepts() & conceptFeatures()
static void inspectNeighbourFeatures( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize, FeatureResultPrintMode mode)
{
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (class name as first argument expected)"));
	std::string clname = inspectarg[0];

	std::vector<strus::Index> far;
	std::size_t ai = 1, ae = inspectargsize;
	for (; ai != ae; ++ai)
	{
		far.push_back( getFeatureIndex( vsmodel, inspectarg[ai]));
	}
	std::set<strus::Index> concepts;
	std::vector<strus::Index>::const_iterator fi = far.begin(), fe = far.end();
	for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
	{
		std::vector<strus::Index> car = vsmodel->featureConcepts( clname, *fi);
		if (car.empty() && g_errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("failed to get feature concepts"));
		}
		std::vector<strus::Index>::const_iterator ci = car.begin(), ce = car.end();
		for (; ci != ce; ++ci)
		{
			concepts.insert( *ci);
		}
	}
	std::vector<strus::Index> res;
	std::set<strus::Index>::const_iterator ci = concepts.begin(), ce = concepts.end();
	for (std::size_t cidx=0; ci != ce; ++ci,++cidx)
	{
		std::vector<strus::Index> car = vsmodel->conceptFeatures( clname, *ci);
		if (car.empty() && g_errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("failed to get concept features"));
		}
		res.insert( res.end(), car.begin(), car.end());
	}
	printResultFeatures( vsmodel, res, mode, true);
}

// Inspect strus::VectorStorageClientInterface::nofConcepts()
static void inspectNofConcepts( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize < 1) throw std::runtime_error( _TXT("too few arguments (class name as first argument expected)"));
	if (inspectargsize > 1) throw std::runtime_error( _TXT("too many arguments (only class name as argument expected)"));
	std::string clname = inspectarg[0];

	std::cout << vsmodel->nofConcepts( clname) << std::endl;
}

// Inspect strus::VectorStorageClientInterface::nofFeatures()
static void inspectNofFeatures( const strus::VectorStorageClientInterface* vsmodel, const char**, std::size_t inspectargsize)
{
	if (inspectargsize > 0) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	std::cout << vsmodel->nofFeatures() << std::endl;
}

static void inspectFeatureSimilarity( const strus::VectorStorageClientInterface* vsmodel, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize < 2) throw strus::runtime_error(_TXT("too few arguments (%u arguments expected)"), 2U);
	if (inspectargsize > 2) throw strus::runtime_error(_TXT("too many arguments (%u arguments expected)"), 2U);
	strus::Index f1 = getFeatureIndex( vsmodel, inspectarg[0]);
	strus::Index f2 = getFeatureIndex( vsmodel, inspectarg[1]);
	std::vector<double> v1 = vsmodel->featureVector( f1);
	std::vector<double> v2 = vsmodel->featureVector( f2);
	std::ostringstream res;
	res << std::setprecision(6) << std::fixed << vsmodel->vectorSimilarity( v1, v2) << std::endl;
	std::cout << res.str();
}

// Inspect strus::VectorStorageClientInterface::config()
static void inspectConfig( const strus::VectorStorageClientInterface* vsmodel, const char**, std::size_t inspectargsize)
{
	if (inspectargsize) throw std::runtime_error( _TXT("too many arguments (no arguments expected)"));
	std::cout << vsmodel->config() << std::endl;
}

// Inspect dump of VSM storage with VectorStorageDumpInterface
static void inspectDump( const strus::VectorStorageInterface* vsi, const strus::DatabaseInterface* dbi, const std::string& config, const char** inspectarg, std::size_t inspectargsize)
{
	if (inspectargsize > 1) throw std::runtime_error( _TXT("too many arguments (one argument expected)"));
	strus::local_ptr<strus::VectorStorageDumpInterface> dumpitr( vsi->createDump( config, dbi, inspectargsize?inspectarg[0]:""));
	const char* chunk;
	std::size_t chunksize;
	while (dumpitr->nextChunk( chunk, chunksize))
	{
		std::cout << std::string( chunk, chunksize);
		if (g_errorBuffer->hasError()) throw std::runtime_error( _TXT("error dumping VSM storage to stdout"));
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
				errorBuffer.get(), argc, argv, 13,
				"h,help", "v,version", "license",
				"G,debug:", "m,module:", "M,moduledir:", "T,trace:",
				"s,config:", "S,configfile:",
				"t,threads:", "D,time", "N,nofranks:",
				"x,realmeasure");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}
		if (opt( "help")) printUsageAndExit = true;

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
			std::cout << "            \"classnames\"" << std::endl;
			std::cout << "               = " << _TXT("Return all names of concept classes of the model.") << std::endl;
			std::cout << "            \"featcon\" <classname> { <feat> }" << std::endl;
			std::cout << "               = " << strus::string_format( _TXT("Take a single or list of feature numbers (with '%c' prefix) or names as input."), FEATNUM_PREFIX_CHAR) << std::endl;
			std::cout << "                 " << _TXT("Return a sorted list of indices of concepts of the class <classname> assigned to it.") << std::endl;
			std::cout << "            \"featvec\" <feat>" << std::endl;
			std::cout << "               = " << strus::string_format( _TXT("Take a single feature number (with '%c' prefix) or name as input."), FEATNUM_PREFIX_CHAR) << std::endl;
			std::cout << "                 " << _TXT("Return the vector assigned to it.") << std::endl;
			std::cout << "            \"featname\" { <feat> }" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of feature numbers as input.") << std::endl;
			std::cout << "                 " << _TXT("Return the list of names assigned to it.") << std::endl;
			std::cout << "            \"featidx\" { <featname> }" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of feature names as input.") << std::endl;
			std::cout << "                 " << _TXT("Return the list of indices assigned to it.") << std::endl;
			std::cout << "            \"featsim\" <feat1> <feat2>" << std::endl;
			std::cout << "               = " << strus::string_format( _TXT("Take two feature numbers (with '%c' prefix) or names as input."), FEATNUM_PREFIX_CHAR) << std::endl;
			std::cout << "                 " << _TXT("Return the cosine similarity, a value between 0.0 and 1.0.") << std::endl;
			std::cout << "            \"confeat\" or \"confeatidx\" \"confeatname\" <classname> { <conceptno> }" << std::endl;
			std::cout << "               = " << _TXT("Take a single or list of concept numbers of the class <classname> as input.") << std::endl;
			std::cout << "                 " << _TXT("Return a sorted list of features assigned to it.") << std::endl;
			std::cout << "                 " << _TXT("\"confeatidx\" prints only the result feature indices.") << std::endl;
			std::cout << "                 " << _TXT("\"confeatname\" prints only the result feature names.") << std::endl;
			std::cout << "                 " << _TXT("\"confeat\" prints both indices and names.") << std::endl;
			std::cout << "            \"nbfeat\" or \"nbfeatidx\" \"nbfeatname\" <classname> { <feat> }" << std::endl;
			std::cout << "               = " << strus::string_format( _TXT("Take a single or list of feature numbers (with '%c' prefix) or names as input."), FEATNUM_PREFIX_CHAR) << std::endl;
			std::cout << "                 " << _TXT("Return a list of features reachable over any shared concept of the class <classname>.") << std::endl;
			std::cout << "                 " << _TXT("\"nbfeat\" prints both indices and names.") << std::endl;
			std::cout << "                 " << _TXT("\"nbfeatname\" prints only the result feature names.") << std::endl;
			std::cout << "                 " << _TXT("\"nbfeat\" prints both indices and names.") << std::endl;
			std::cout << "            \"opfeat\"  or \"opfeatname\" { <expr> }" << std::endl;
			std::cout << "               = " << strus::string_format( _TXT("Take an arithmetic expression of feature numbers (with '%c' prefix) or names as input."), FEATNUM_PREFIX_CHAR) << std::endl;
			std::cout << "                 " << _TXT("Return a list of features found.") << std::endl;
			std::cout << "            \"opfeatw\"  or \"opfeatwname\" { <expr> }" << std::endl;
			std::cout << "               = " << _TXT("same as \"opfeat\", resp. \"opfeatname\" but print also the result weights.") << std::endl;
			std::cout << "            \"nofcon\"" << std::endl;
			std::cout << "               = " << _TXT("Get the number of concepts of the class <classname> defined.") << std::endl;
			std::cout << "            \"noffeat\"" << std::endl;
			std::cout << "               = " << _TXT("Get the number of features defined.") << std::endl;
			std::cout << "            \"config\"" << std::endl;
			std::cout << "               = " << _TXT("Get the configuration the vector storage.") << std::endl;
			std::cout << "                 " << _TXT("Select the vector storage type with the parameter 'storage'.") << std::endl;
			std::cout << "            \"dump\" [ <dbprefix> ]" << std::endl;
			std::cout << "               = " << _TXT("Dump the contents of the VSM repository.") << std::endl;
			std::cout << "                 " << _TXT("The optional parameter <dbprefix> selects a specific block type.") << std::endl;
			std::cout << _TXT("description: Inspects some data defined in a vector space model build.") << std::endl;
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
			std::cout << "    " << _TXT("Define the vector space model configuration string as <CONFIG>") << std::endl;
			std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    " << _TXT("Define the vector space model configuration file as <FILENAME>") << std::endl;
			std::cout << "    " << _TXT("<FILENAME> is a file containing the configuration string") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			std::cout << "-D|--time" << std::endl;
			std::cout << "    " << _TXT("Do measure duration of operation (only for search)") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of threads to use (only for search)") << std::endl;
			std::cout << "    " << _TXT("Default is no multithreading (N=0)") << std::endl;
			std::cout << "-x|--realmeasure" << std::endl;
			std::cout << "    " << _TXT("Calculate real values of similarities for search and compare") << std::endl;
			std::cout << "    " << _TXT("of methods 'opfeat','opfeatname','opfeatw' and 'opfeatwname'.") << std::endl;
			std::cout << "-N|--nofranks <N>" << std::endl;
			std::cout << "    " << _TXT("Limit the number of results to for searches to <N> (default 20)") << std::endl;
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
		bool realmeasure( opt("realmeasure"));
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
		std::string modelname;
		if (!strus::extractStringFromConfigString( modelname, config, "storage", errorBuffer.get()))
		{
			modelname = strus::Constants::standard_vector_storage();
			if (errorBuffer->hasError()) throw strus::runtime_error("failed get vector space storage type from configuration");
		}
		bool doMeasureDuration = opt( "time");
		unsigned int maxNofRanks = 20;
		if (opt("nofranks"))
		{
			maxNofRanks = opt.asUint("nofranks");
		}
		unsigned int nofThreads = 0;	//... default use no multithreading
		if (opt("threads"))
		{
			nofThreads = opt.asUint("threads");
		}
		std::string dbname;
		(void)strus::extractStringFromConfigString( dbname, config, "database", errorBuffer.get());
		if (errorBuffer->hasError()) throw std::runtime_error( _TXT("cannot evaluate database"));

		const strus::VectorStorageInterface* vsi = storageBuilder->getVectorStorage( modelname);
		if (!vsi) throw std::runtime_error( _TXT("failed to get vector space model interface"));
		const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbname);
		if (!dbi) throw std::runtime_error( _TXT("failed to get database interface"));

		strus::local_ptr<strus::VectorStorageClientInterface> vsmodel( vsi->createClient( config, dbi));
		if (!vsmodel.get()) throw std::runtime_error( _TXT("failed to create vector space model client interface"));

		std::string what = opt[0];
		const char** inspectarg = opt.argv() + 1;
		std::size_t inspectargsize = opt.nofargs() - 1;

		// Do inspect what is requested:
		if (strus::caseInsensitiveEquals( what, "classnames"))
		{
			inspectConceptClassNames( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featsim"))
		{
			inspectFeatureSimilarity( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featcon"))
		{
			inspectFeatureConcepts( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featvec"))
		{
			inspectFeatureVector( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featname"))
		{
			inspectFeatureName( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "featidx"))
		{
			inspectFeatureIndex( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "confeatidx"))
		{
			inspectConceptFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintIndex);
		}
		else if (strus::caseInsensitiveEquals( what, "confeatname"))
		{
			inspectConceptFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintName);
		}
		else if (strus::caseInsensitiveEquals( what, "confeat"))
		{
			inspectConceptFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintIndexName);
		}
		else if (strus::caseInsensitiveEquals( what, "opfeat"))
		{
			inspectVectorOperations( vsmodel.get(), inspectarg, inspectargsize, PrintIndexName, maxNofRanks, nofThreads, doMeasureDuration, false/*with weights*/, realmeasure);
		}
		else if (strus::caseInsensitiveEquals( what, "opfeatw"))
		{
			inspectVectorOperations( vsmodel.get(), inspectarg, inspectargsize, PrintIndexName, maxNofRanks, nofThreads, doMeasureDuration, true/*with weights*/, realmeasure);
		}
		else if (strus::caseInsensitiveEquals( what, "opfeatname"))
		{
			inspectVectorOperations( vsmodel.get(), inspectarg, inspectargsize, PrintName, maxNofRanks, nofThreads, doMeasureDuration, false/*with weights*/, realmeasure);
		}
		else if (strus::caseInsensitiveEquals( what, "opfeatwname"))
		{
			inspectVectorOperations( vsmodel.get(), inspectarg, inspectargsize, PrintName, maxNofRanks, nofThreads, doMeasureDuration, true/*with weights*/, realmeasure);
		}
		else if (strus::caseInsensitiveEquals( what, "nbfeatidx"))
		{
			inspectNeighbourFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintIndex);
		}
		else if (strus::caseInsensitiveEquals( what, "nbfeatname"))
		{
			inspectNeighbourFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintName);
		}
		else if (strus::caseInsensitiveEquals( what, "nbfeat"))
		{
			inspectNeighbourFeatures( vsmodel.get(), inspectarg, inspectargsize, PrintIndexName);
		}
		else if (strus::caseInsensitiveEquals( what, "nofcon"))
		{
			inspectNofConcepts( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "noffeat"))
		{
			inspectNofFeatures( vsmodel.get(), inspectarg, inspectargsize);
		}
		else if (strus::caseInsensitiveEquals( what, "config"))
		{
			inspectConfig( vsmodel.get(), inspectarg, inspectargsize);
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
	return -1;
}


