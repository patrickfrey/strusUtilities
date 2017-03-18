/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "private/analyzerMap.hpp"
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

AnalyzerMap::AnalyzerMap( const AnalyzerObjectBuilderInterface* builder_, ErrorBufferInterface* errorhnd_)
	:m_map()
	,m_builder(builder_),m_textproc(builder_->getTextProcessor())
	,m_warnings(),m_errorhnd(errorhnd_)
{
	if (!m_textproc) throw strus::runtime_error(_TXT("failed to get text processor"));
}

static const std::string getAnalyzerMapKey( const std::string& mimeType, const std::string& encoding, const std::string& scheme)
{
	if (!scheme.empty())
	{
		if (!encoding.empty())
		{
			return utils::tolower( mimeType + "[" + encoding + "]" + ":" + scheme);
		}
		else
		{
			return utils::tolower( mimeType + ":" + scheme);
		}
	}
	else
	{
		if (!encoding.empty())
		{
			return utils::tolower( mimeType + "[" + encoding + "]");
		}
		else
		{
			return utils::tolower( mimeType);
		}
	}
}

bool AnalyzerMap::isAnalyzerConfigSource( const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u"), prgfile.c_str(), ec);
	return strus::isAnalyzerConfigSource( programSource, m_errorhnd);
}

void AnalyzerMap::loadDefaultAnalyzerProgram(
	const analyzer::DocumentClass& documentClass,
	const std::string& segmentername,
	const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u"), prgfile.c_str(), ec);

	const strus::SegmenterInterface* segmenter;
	if (segmentername.empty() && documentClass.defined())
	{
		segmenter = m_textproc->getSegmenterByMimeType( documentClass.mimeType());
		if (!segmenter) throw strus::runtime_error( _TXT( "failed to load segmenter by MIME type '%s': %s"), documentClass.mimeType().c_str(), m_errorhnd->fetchError());
	}
	else
	{
		segmenter = m_textproc->getSegmenterByName( segmentername);
		if (!segmenter) throw strus::runtime_error( _TXT( "failed to load segmenter by name '%s': %s"), segmentername.c_str(), m_errorhnd->fetchError());
	}
	utils::SharedPtr<strus::DocumentAnalyzerInterface>
		analyzer( m_builder->createDocumentAnalyzer( segmenter));
	if (!analyzer.get()) throw strus::runtime_error(_TXT("error creating analyzer"));

	if (!strus::loadDocumentAnalyzerProgram( *analyzer, m_textproc, programSource, true/*allow includes*/, m_warnings, m_errorhnd))
	{
		throw strus::runtime_error( _TXT("failed to load analyzer configuration program: %s"), m_errorhnd->fetchError());
	}
	m_map[ ""] = analyzer;
}

void AnalyzerMap::loadAnalyzerProgram(
	const analyzer::DocumentClass& documentClass,
	const std::string& segmentername,
	const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u"), prgfile.c_str(), ec);

	const strus::SegmenterInterface* segmenter;
	if (!documentClass.defined())
	{
		throw strus::runtime_error(_TXT("defining analyzer program for undefined document class"));
	}
	if (segmentername.empty())
	{
		segmenter = m_textproc->getSegmenterByMimeType( documentClass.mimeType());
		if (!segmenter) throw strus::runtime_error( _TXT( "failed to load segmenter by mime type '%s': %s"), documentClass.mimeType().c_str(), m_errorhnd->fetchError());
	}
	else
	{
		segmenter = m_textproc->getSegmenterByName( segmentername);
		if (!segmenter) throw strus::runtime_error( _TXT( "failed to load segmenter by name '%s': %s"), segmentername.c_str(), m_errorhnd->fetchError());
	}
	utils::SharedPtr<strus::DocumentAnalyzerInterface>
		analyzer( m_builder->createDocumentAnalyzer( segmenter));
	if (!analyzer.get()) throw strus::runtime_error(_TXT("error creating analyzer"));

	if (!strus::loadDocumentAnalyzerProgram( *analyzer, m_textproc, programSource, true/*allow includes*/, m_warnings, m_errorhnd))
	{
		throw strus::runtime_error( _TXT("failed to load analyzer configuration program: %s"), m_errorhnd->fetchError());
	}
	if (documentClass.mimeType().empty())
	{
		if (!documentClass.scheme().empty())
		{
			throw strus::runtime_error("scheme defined in configuration but no MIME type");
		}
		else if (!documentClass.encoding().empty()) 
		{
			throw strus::runtime_error("encoding defined in configuration but no MIME type");
		}
	}
	std::string key = getAnalyzerMapKey( documentClass.mimeType(), documentClass.encoding(), documentClass.scheme());
	m_map[ key] = analyzer;
}

void AnalyzerMap::loadAnalyzerMap( const std::string& prgfile)
{
	unsigned int ec;
	std::string programSource;
	ec = strus::readFile( prgfile, programSource);
	if (ec) throw strus::runtime_error( _TXT( "failed to load program file '%s' (errno %u"), prgfile.c_str(), ec);

	std::vector<AnalyzerMapElement> mapdef;
	if (!strus::loadAnalyzerMap( mapdef, programSource, m_errorhnd))
	{
		throw strus::runtime_error( _TXT( "error loading analyzer map"));
	}
	std::vector<AnalyzerMapElement>::iterator mi = mapdef.begin(), me = mapdef.end();
	for (; mi != me; ++mi)
	{
		try
		{
			std::string programpath = m_textproc->getResourcePath( mi->program);
			if (programpath.empty())
			{
				throw strus::runtime_error( _TXT( "program path not found"));
			}
			loadAnalyzerProgram( mi->doctype, mi->segmenter, programpath);
		}
		catch (std::runtime_error& err)
		{
			throw strus::runtime_error( _TXT( "failed to load analyzer program '%s': %s"), mi->program.c_str(), err.what());
		}
	}
}

const DocumentAnalyzerInterface* AnalyzerMap::get( const analyzer::DocumentClass& dclass) const
{
	if (!dclass.scheme().empty())
	{
		std::string key = getAnalyzerMapKey( dclass.mimeType(), dclass.encoding(), dclass.scheme());
		Map::const_iterator di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
		key = getAnalyzerMapKey( dclass.mimeType(), "", dclass.scheme());
		di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
	}
	if (!dclass.mimeType().empty())
	{
		std::string key = getAnalyzerMapKey( dclass.mimeType(), dclass.encoding(), "");
		Map::const_iterator di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
		key = getAnalyzerMapKey( dclass.mimeType(), "", "");
		di = m_map.find( key);
		if (di != m_map.end()) return di->second.get();
	}
	Map::const_iterator di = m_map.find( "");
	if (di != m_map.end()) return di->second.get();
	throw strus::runtime_error(_TXT("no analyzer defined for this document class"));
}

