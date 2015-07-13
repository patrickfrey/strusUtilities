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
#include <stdexcept>

using namespace strus;

void AnalyzerMap::defineProgram(
		const std::string& mimeType,
		const std::string& scheme,
		const std::string& segmenter,
		const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec)
	{
		std::ostringstream msg;
		msg << " (file system error " << ec << ")"; 
		throw std::runtime_error( std::string( "failed to load program file ") + prgfile + msg.str());
	}
	if (strus::isAnalyzerMapSource( programSource))
	{
		if (!scheme.empty())
		{
			throw std::runtime_error( "document scheme specification only allowed with an analyzer configuration specified");
		}
		std::vector<AnalyzerMapElement> mapdef;
		strus::loadAnalyzerMap( mapdef, programSource);
		std::vector<AnalyzerMapElement>::iterator mi = mapdef.begin(), me = mapdef.end();
		for (; mi != me; ++mi)
		{
			if (mi->mimeType.empty())
			{
				if (mimeType.empty())
				{
					mi->mimeType = "text/xml";
				}
				else
				{
					mi->mimeType = mimeType;
				}
			}
			if (mi->segmenter.empty())
			{
				mi->segmenter = segmenter;
			}
			programSource.clear();
			ec = strus::readFile( mi->prgFilename, programSource);
			if (ec)
			{
				std::ostringstream msg;
				msg << " (file system error " << ec << ")"; 
				throw std::runtime_error( std::string( "failed to load program file ") + mi->prgFilename + msg.str());
			}
			try
			{
				defineAnalyzerProgramSource( mi->mimeType, mi->scheme, mi->segmenter, programSource);
			}
			catch (const std::runtime_error& err)
			{
				throw std::runtime_error( std::string( "loading analyzer program file '") + mi->prgFilename + "': " + err.what());
			}
		}
	}
	else
	{
		if (mimeType.empty())
		{
			defineAnalyzerProgramSource( "text/xml", scheme, segmenter, programSource);
		}
		else
		{
			defineAnalyzerProgramSource( mimeType, scheme, segmenter, programSource);
		}
	}
}

void AnalyzerMap::defineAnalyzerProgramSource(
		const std::string& mimeType,
		const std::string& scheme,
		const std::string& segmenter,
		const std::string& analyzerProgramSource)
{
	utils::SharedPtr<strus::DocumentAnalyzerInterface>
		analyzer( m_builder->createDocumentAnalyzer( segmenter));

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

