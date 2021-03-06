/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_KEYMAP_GENERATE_PROCESSOR_HPP_INCLUDED
#define _STRUS_KEYMAP_GENERATE_PROCESSOR_HPP_INCLUDED
#include "strus/storage/index.hpp"
#include <vector>
#include <string>
#include "strus/base/thread.hpp"
#include "strus/base/atomic.hpp"
#include "private/documentAnalyzer.hpp"

namespace strus {

/// \brief Forward declaration
class DocumentAnalyzerInstanceInterface;
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
	strus::mutex m_mutex;
	std::vector<KeyOccurrenceList> m_keyOccurrenceListBuf;
};


class KeyMapGenProcessor
{
public:
	KeyMapGenProcessor(
			const TextProcessorInterface* textproc_,
			const strus::DocumentAnalyzer* analyzerMap_,
			const analyzer::DocumentClass& defaultDocumentClass_,
			KeyMapGenResultList* que_,
			FileCrawlerInterface* crawler_,
			ErrorBufferInterface* errorhnd_);

	~KeyMapGenProcessor();

	void sigStop();
	void run();

private:
	const TextProcessorInterface* m_textproc;
	const strus::DocumentAnalyzer* m_analyzerMap;
	analyzer::DocumentClass m_defaultDocumentClass;
	KeyMapGenResultList* m_que;
	FileCrawlerInterface* m_crawler;
	strus::AtomicFlag m_terminated;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
