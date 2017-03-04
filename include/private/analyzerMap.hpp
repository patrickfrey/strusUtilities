/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_ANALYZER_MAP_HPP_INCLUDED
#define _STRUS_ANALYZER_MAP_HPP_INCLUDED
#include "strus/analyzer/documentClass.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "private/utils.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class AnalyzerObjectBuilderInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class SegmenterInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class AnalyzerMap
{
public:
	AnalyzerMap(
		const AnalyzerObjectBuilderInterface* builder_,
		ErrorBufferInterface* errorhnd_);

	void loadDefaultAnalyzerProgram(
		const std::string& segmenter,
		const std::string& prgfile);

	void loadAnalyzerProgram(
		const analyzer::DocumentClass& documentClass,
		const std::string& segmenter,
		const std::string& prgfile);

	void loadAnalyzerMap( const std::string& prgfile);

	bool isAnalyzerConfigSource( const std::string& prgfile);

	std::string warnings() const					{return m_warnings.str();}

	const DocumentAnalyzerInterface* get(
		const analyzer::DocumentClass& dclass) const;

private:
	typedef std::map<std::string,utils::SharedPtr<DocumentAnalyzerInterface> > Map;

	AnalyzerMap( const AnalyzerMap&){}		//... non copyable
	void operator=( const AnalyzerMap&){}		//... non copyable

private:
	Map m_map;
	const strus::AnalyzerObjectBuilderInterface* m_builder;
	const strus::TextProcessorInterface* m_textproc;
	std::ostringstream m_warnings;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif

