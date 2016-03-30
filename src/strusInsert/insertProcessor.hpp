/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_INSERTER_PROCESSOR_HPP_INCLUDED
#define _STRUS_INSERTER_PROCESSOR_HPP_INCLUDED
#include "private/utils.hpp"
#include "analyzerMap.hpp"

namespace strus {

/// \brief Forward declaration
class StorageClientInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class DocumentAnalyzerInterface;
/// \brief Forward declaration
class CommitQueue;
/// \brief Forward declaration
class FileCrawlerInterface;
/// \brief Forward declaration
class ErrorBufferInterface;


class InsertProcessor
{
public:
	InsertProcessor(
			StorageClientInterface* storage_,
			const TextProcessorInterface* textproc_,
			const AnalyzerMap& analyzerMap_,
			CommitQueue* commitque_,
			FileCrawlerInterface* crawler_,
			unsigned int transactionSize_,
			bool verbose_,
			ErrorBufferInterface* errorhnd_);

	~InsertProcessor();

	void sigStop();
	void run();

private:
	StorageClientInterface* m_storage;
	const TextProcessorInterface* m_textproc;
	AnalyzerMap m_analyzerMap;
	CommitQueue* m_commitque;
	FileCrawlerInterface* m_crawler;
	unsigned int m_transactionSize;
	bool m_verbose;
	utils::AtomicBool m_terminated;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
