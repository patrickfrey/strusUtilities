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
#ifndef _STRUS_ANALYZER_MAP_HPP_INCLUDED
#define _STRUS_ANALYZER_MAP_HPP_INCLUDED
#include "strus/documentClass.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "private/utils.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class DocumentClass;
/// \brief Forward declaration
class AnalyzerObjectBuilderInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class AnalyzerMap
{
public:
	AnalyzerMap( const AnalyzerObjectBuilderInterface* builder_, ErrorBufferInterface* errorhnd_)
		:m_builder(builder_),m_errorhnd(errorhnd_){}
	AnalyzerMap( const AnalyzerMap& o)
		:m_map(o.m_map),m_builder(o.m_builder),m_errorhnd(o.m_errorhnd){}

	void defineProgram(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& prgfile);

	DocumentAnalyzerInterface* get( const DocumentClass& dclass) const;

private:
	void defineAnalyzerProgramSource(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& analyzerProgramSource);

	typedef std::map<std::string,utils::SharedPtr<DocumentAnalyzerInterface> > Map;
	Map m_map;
	const strus::AnalyzerObjectBuilderInterface* m_builder;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif

