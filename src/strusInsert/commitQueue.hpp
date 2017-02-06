/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_INSERTER_COMMIT_QUEUE_HPP_INCLUDED
#define _STRUS_INSERTER_COMMIT_QUEUE_HPP_INCLUDED
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/index.hpp"
#include "strus/reference.hpp"
#include "private/utils.hpp"
#include <vector>
#include <string>
#include <queue>

namespace strus {
/// \brief Forward declaration
class ErrorBufferInterface;

class CommitQueue
{
public:
	CommitQueue(
			StorageClientInterface* storage_,
			bool verbose_,
			ErrorBufferInterface* errorhnd_)
		:m_storage(storage_),m_nofDocuments(0),m_nofOpenTransactions(0),m_verbose(verbose_),m_errorhnd(errorhnd_)
	{
		m_nofDocuments = m_storage->nofDocumentsInserted();
	}

	void pushTransaction( StorageTransactionInterface* transaction);
	const std::vector<std::string>& errors() const		{return m_errors;}

private:
	typedef Reference<StorageTransactionInterface> StorageTransactionReference;

	void handleWaitingTransactions();
	Reference<StorageTransactionInterface> getNextTransaction();

private:
	StorageClientInterface* m_storage;
	Index m_nofDocuments;
	unsigned int m_nofOpenTransactions;
	std::queue<StorageTransactionReference> m_openTransactions;
	utils::Mutex m_mutex_openTransactions;
	utils::Mutex m_mutex_errors;
	std::vector<std::string> m_errors;
	bool m_verbose;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
