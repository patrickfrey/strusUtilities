/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "commitQueue.hpp"
#include "private/internationalization.hpp"
#include "strus/errorBufferInterface.hpp"
#include <cstdio>
#include <stdexcept>

using namespace strus;

void CommitQueue::handleWaitingTransactions()
{
	for (;;)
	{
		try
		{
			Reference<StorageTransactionInterface> transaction = getNextTransaction();
			if (!transaction.get()) break;
			if (!transaction->commit())
			{
				throw strus::runtime_error(_TXT("transaction commit failed"));
			}
			Index totalNofDocuments = m_storage->nofDocumentsInserted();
			Index nofDocsInserted = totalNofDocuments - m_nofDocuments;
			if (m_verbose)
			{
				::printf( _TXT("inserted %u documents (total %u)\n"),
						nofDocsInserted, totalNofDocuments);
				::fflush(stdout);
			}
			else
			{
				::printf( _TXT("\rinserted %u documents (total %u)          "),
						nofDocsInserted, totalNofDocuments);
				::fflush(stdout);
			}
		}
		catch (const std::bad_alloc&)
		{
			m_errorhnd->report( _TXT("out of memory handling transaction in queue"));
			fprintf( stderr, _TXT("out of memory handling transaction in queue\n"));
		}
		catch (const std::exception& err)
		{
			const char* errmsg = m_errorhnd->fetchError();
			if (errmsg)
			{
				m_errorhnd->report( _TXT("error handling transaction in queue: %s, %s"), err.what(), errmsg);
				fprintf( stderr, _TXT("error handling transaction in queue: %s, %s\n"), err.what(), errmsg);
			}
			else
			{
				m_errorhnd->report( _TXT("error handling transaction in queue: %s"), err.what());
				fprintf( stderr, _TXT("error handling transaction in queue: %s\n"), err.what());
			}
			utils::ScopedLock lock( m_mutex_errors);
			m_errors.push_back( m_errorhnd->fetchError());
		}
	}
}

void CommitQueue::pushTransaction( StorageTransactionInterface* transaction)
{
	{
		utils::ScopedLock lock( m_mutex_openTransactions);
		m_openTransactions.push( StorageTransactionReference( transaction));
		++m_nofOpenTransactions;
	}
	handleWaitingTransactions();
}

Reference<StorageTransactionInterface> CommitQueue::getNextTransaction()
{
	utils::ScopedLock lock( m_mutex_openTransactions);

	if (m_nofOpenTransactions == 0) return Reference<StorageTransactionInterface>();
	--m_nofOpenTransactions;
	Reference<StorageTransactionInterface>  rt = m_openTransactions.front();
	m_openTransactions.pop();
	return rt;
}

