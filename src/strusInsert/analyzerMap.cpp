/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2013,2014 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#include "analyzerMap.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/private/fileio.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/segmenterInterface.hpp"
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

	if (strus::isAnalyzerMapSource( programSource))
	{
		if (!scheme.empty())
		{
			throw strus::runtime_error( _TXT("document scheme specification only allowed with an analyzer configuration specified"));
		}
		std::vector<AnalyzerMapElement> mapdef;
		strus::loadAnalyzerMap( mapdef, programSource);
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
	utils::SharedPtr<strus::DocumentAnalyzerInterface>
		analyzer( m_builder->createDocumentAnalyzer( segmentername));

	std::string mimeType = segmenter->mimeType();
	const strus::TextProcessorInterface* textproc = m_builder->getTextProcessor();
	strus::loadDocumentAnalyzerProgram( *analyzer, textproc, analyzerProgramSource);

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

