/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_INSERTER_PROCESSOR_HPP_INCLUDED
#define _STRUS_INSERTER_PROCESSOR_HPP_INCLUDED
#include "strus/base/atomic.hpp"
#include "strus/reference.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "private/documentAnalyzer.hpp"

namespace strus {

/// \brief Forward declaration
class StorageClientInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class DocumentAnalyzerInstanceInterface;
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
			const strus::DocumentAnalyzer* analyzerMap_,
			const analyzer::DocumentClass& defaultDocumentClass_,
			CommitQueue* commitque_,
			FileCrawlerInterface* crawler_,
			int transactionSize_,
			bool verbose_,
			ErrorBufferInterface* errorhnd_);

	~InsertProcessor();

	void sigStop();
	void run();
	bool hasError() const	{return m_gotError;}

private:
	void processDocument( const std::string& filename);

private:
	StorageClientInterface* m_storage;
	const TextProcessorInterface* m_textproc;
	const strus::DocumentAnalyzer* m_analyzerMap;
	analyzer::DocumentClass m_defaultDocumentClass;
	CommitQueue* m_commitque;
	FileCrawlerInterface* m_crawler;
	strus::Reference<strus::StorageTransactionInterface> m_transaction;
	int m_transactionSize;
	int m_docCount;
	bool m_verbose;
	bool m_gotError;
	strus::AtomicFlag m_terminated;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
