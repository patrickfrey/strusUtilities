/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_CHECK_INSERT_PROCESSOR_HPP_INCLUDED
#define _STRUS_CHECK_INSERT_PROCESSOR_HPP_INCLUDED
#include "private/documentAnalyzer.hpp"
#include "strus/base/atomic.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class StorageClientInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class DocumentAnalyzerInstanceInterface;
/// \brief Forward declaration
class FileCrawlerInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class CheckInsertProcessor
{
public:
	CheckInsertProcessor(
			StorageClientInterface* storage_,
			const TextProcessorInterface* textproc_,
			const strus::DocumentAnalyzer* analyzerMap_,
			const analyzer::DocumentClass& defaultDocumentClass_,
			FileCrawlerInterface* crawler_,
			const std::string& logfile_,
			ErrorBufferInterface* errorhnd_);

	~CheckInsertProcessor();

	void sigStop();
	void run();

private:
	StorageClientInterface* m_storage;
	const TextProcessorInterface* m_textproc;
	const strus::DocumentAnalyzer* m_analyzerMap;
	analyzer::DocumentClass m_defaultDocumentClass;
	FileCrawlerInterface* m_crawler;
	strus::AtomicFlag m_terminated;
	std::string m_logfile;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
