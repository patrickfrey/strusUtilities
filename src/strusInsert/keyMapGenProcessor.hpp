/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2015 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#ifndef _STRUS_KEYMAP_GENERATE_PROCESSOR_HPP_INCLUDED
#define _STRUS_KEYMAP_GENERATE_PROCESSOR_HPP_INCLUDED
#include "strus/index.hpp"
#include <vector>
#include <string>
#include "private/utils.hpp"
#include "analyzerMap.hpp"

namespace strus {

/// \brief Forward declaration
class DocumentAnalyzerInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class FileCrawlerInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class KeyOccurrence
{
public:
	explicit KeyOccurrence( const std::string& name_, std::size_t frequency_=1)
		:m_name(name_),m_frequency(frequency_){}
	KeyOccurrence( const KeyOccurrence& o)
		:m_name(o.m_name),m_frequency(o.m_frequency){}

	const std::string& name() const		{return m_name;}
	std::size_t frequency() const		{return m_frequency;}

private:
	std::string m_name;
	std::size_t m_frequency;
};


typedef std::vector<KeyOccurrence> KeyOccurrenceList;


class KeyMapGenResultList
{
public:
	KeyMapGenResultList(){}
	void push( KeyOccurrenceList& lst);

	void printKeyOccurrenceList( std::ostream& out, std::size_t maxNofResults) const;

private:
	utils::Mutex m_mutex;
	std::vector<KeyOccurrenceList> m_keyOccurrenceListBuf;
};


class KeyMapGenProcessor
{
public:
	KeyMapGenProcessor(
			const TextProcessorInterface* textproc_,
			const AnalyzerMap& analyzerMap_,
			KeyMapGenResultList* que_,
			FileCrawlerInterface* crawler_,
			ErrorBufferInterface* errorhnd_);

	~KeyMapGenProcessor();

	void sigStop();
	void run();

private:
	const TextProcessorInterface* m_textproc;
	AnalyzerMap m_analyzerMap;
	KeyMapGenResultList* m_que;
	FileCrawlerInterface* m_crawler;
	utils::AtomicBool m_terminated;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
