/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "analyzerMap.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/base/fileio.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <stdexcept>

using namespace strus;

AnalyzerMap::AnalyzerMap( const AnalyzerObjectBuilderInterface* builder_, const std::string& prgfile_, const std::string& defaultSegmenterName_, ErrorBufferInterface* errorhnd_)
	:m_map(),m_defaultAnalyzerProgramSource(),m_defaultSegmenterName(defaultSegmenterName_)
	,m_defaultSegmenter(defaultSegmenterName_.empty()?0:builder_->getSegmenter( defaultSegmenterName_))
	,m_builder(builder_),m_errorhnd(errorhnd_)
{
	defineDefaultProgram( prgfile_);
}

void AnalyzerMap::defineProgram(
		const std::string& scheme,
		const std::string& segmenter,
		const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u"), prgfile.c_str(), ec);

	if (strus::isAnalyzerMapSource( programSource, m_errorhnd))
	{
			throw strus::runtime_error( _TXT("analyzer map loaded instead of analyzer program"));
	}
	if (m_errorhnd->hasError())
	{
		throw strus::runtime_error(_TXT("error detecting analyzer configuration file type"));
	}
	defineAnalyzerProgramSource( scheme, segmenter, programSource);
}

void AnalyzerMap::defineDefaultProgram(
		const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u"), prgfile.c_str(), ec);

	if (strus::isAnalyzerMapSource( programSource, m_errorhnd))
	{
		std::vector<AnalyzerMapElement> mapdef;
		if (!strus::loadAnalyzerMap( mapdef, programSource, m_errorhnd))
		{
			throw strus::runtime_error( _TXT( "error loading analyzer map"));
		}
		std::vector<AnalyzerMapElement>::iterator mi = mapdef.begin(), me = mapdef.end();
		for (; mi != me; ++mi)
		{
			if (mi->segmenter.empty())
			{
				mi->segmenter = m_defaultSegmenterName;
			}
			programSource.clear();
			ec = strus::readFile( mi->prgFilename, programSource);
			if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u)"), mi->prgFilename.c_str(), ec);

			try
			{
				defineAnalyzerProgramSource( mi->scheme, mi->segmenter, programSource);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error( _TXT( "loading analyzer program file '%s': %s"), mi->prgFilename.c_str(), err.what());
			}
		}
	}
	else
	{
		if (m_errorhnd->hasError())
		{
			throw strus::runtime_error(_TXT("error detecting analyzer configuration file type"));
		}
		m_defaultAnalyzerProgramSource = programSource;
		if (m_defaultSegmenter)
		{
			defineAnalyzerProgramSource( 0/*scheme*/, m_defaultSegmenter, m_defaultAnalyzerProgramSource);
		}
	}
}

void AnalyzerMap::defineAnalyzerProgramSource(
		const std::string& scheme,
		const strus::SegmenterInterface* segmenter,
		const std::string& analyzerProgramSource)
{
	utils::SharedPtr<strus::DocumentAnalyzerInterface>
		analyzer( m_builder->createDocumentAnalyzer( segmenter));
	if (!analyzer.get()) throw strus::runtime_error(_TXT("error creating analyzer"));

	std::string mimeType = segmenter->mimeType();
	const strus::TextProcessorInterface* textproc = m_builder->getTextProcessor();
	if (!strus::loadDocumentAnalyzerProgram( *analyzer, textproc, analyzerProgramSource, m_errorhnd))
	{
		throw strus::runtime_error( _TXT("failed to load analyzer configuration program"));
	}
	if (!scheme.empty())
	{
		m_map[ mimeType + ":" + scheme] = analyzer;
	}
	m_map[ mimeType] = analyzer;
}

void AnalyzerMap::defineAnalyzerProgramSource(
		const std::string& scheme,
		const std::string& segmentername,
		const std::string& analyzerProgramSource)
{
	const strus::SegmenterInterface* segmenter = m_builder->getSegmenter( segmentername);
	if (!segmenter) throw strus::runtime_error(_TXT("error getting segmenter by name"));
	defineAnalyzerProgramSource( scheme, segmenter, analyzerProgramSource);
}

DocumentAnalyzerInterface* AnalyzerMap::get( const DocumentClass& dclass)
{
	DocumentAnalyzerInterface* rt = 0;
	if (dclass.scheme().empty())
	{
		std::string key( dclass.mimeType());
		Map::const_iterator di = m_map.find( key);
		if (di != m_map.end()) rt = di->second.get();
	}
	else
	{
		std::string key( dclass.mimeType() + ":" + dclass.scheme());
		Map::const_iterator di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
		std::string altkey( dclass.mimeType());
		di = m_map.find( altkey);
		if (di != m_map.end()) rt = di->second.get();
	}
	if (!rt && !m_defaultAnalyzerProgramSource.empty())
	{
		const SegmenterInterface* segmenter;
		if (m_defaultSegmenter && 0==std::strcmp( m_defaultSegmenter->mimeType(), dclass.mimeType().c_str()))
		{
			segmenter = m_defaultSegmenter;
		}
		else
		{
			segmenter = m_builder->findMimeTypeSegmenter( dclass.mimeType());
		}
		if (segmenter)
		{
			defineAnalyzerProgramSource( ""/*scheme*/, segmenter, m_defaultAnalyzerProgramSource);
			std::string key( dclass.mimeType());
			Map::const_iterator di = m_map.find( key);
			if (di == m_map.end()) throw strus::runtime_error(_TXT("failed to declare default analyzer program source on demand"));
			rt = di->second.get();
		}
	}
	return rt;
}

