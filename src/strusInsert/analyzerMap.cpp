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
		if (!scheme.empty())
		{
			throw strus::runtime_error( _TXT("document scheme specification only allowed with an analyzer configuration specified"));
		}
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
				mi->segmenter = segmenter;
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
		defineAnalyzerProgramSource( scheme, segmenter, programSource);
	}
}

void AnalyzerMap::defineAnalyzerProgramSource(
		const std::string& scheme,
		const std::string& segmentername,
		const std::string& analyzerProgramSource)
{
	std::auto_ptr<strus::SegmenterInterface>
		segmenter( m_builder->createSegmenter( segmentername));
	if (!segmenter.get()) throw strus::runtime_error(_TXT("error creating segmenter"));

	utils::SharedPtr<strus::DocumentAnalyzerInterface>
		analyzer( m_builder->createDocumentAnalyzer( segmentername));
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

DocumentAnalyzerInterface* AnalyzerMap::get( const DocumentClass& dclass) const
{
	if (dclass.scheme().empty())
	{
		std::string key( dclass.mimeType());
		Map::const_iterator di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
	}
	else
	{
		std::string key( dclass.mimeType() + ":" + dclass.scheme());
		Map::const_iterator di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
		std::string altkey( dclass.mimeType());
		di = m_map.find( altkey);
		if (di != m_map.end()) return di->second.get();
	}
	return 0;
}

