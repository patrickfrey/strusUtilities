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
#include "strus/base/thread.hpp"
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
				throw strus::runtime_error( "%s", _TXT("transaction commit failed"));
			}
			Index totalNofDocuments = m_storage->nofDocumentsInserted();
			Index nofDocsInserted = totalNofDocuments - m_nofDocuments;
			if (m_verbose)
			{
				::fprintf( stderr, _TXT("inserted %u documents (total %u)\n"),
						nofDocsInserted, totalNofDocuments);
				::fflush( stderr);
			}
			else
			{
				::fprintf( stderr, _TXT("\rinserted %u documents (total %u)          "),
						nofDocsInserted, totalNofDocuments);
				::fflush( stderr);
			}
		}
		catch (const std::bad_alloc&)
		{
			m_errorhnd->report( *ErrorCode(StrusComponentUtilities,ErrorOperationParse,ErrorCauseOutOfMem), _TXT("out of memory handling transaction in queue"));
			::fprintf( stderr, _TXT("out of memory handling transaction in queue\n"));
			::fflush( stderr);
		}
		catch (const std::exception& err)
		{
			const char* errmsg = m_errorhnd->fetchError();
			if (errmsg)
			{
				m_errorhnd->report( *ErrorCode(StrusComponentUtilities,ErrorOperationParse,ErrorCauseRuntimeError), _TXT("error handling transaction in queue: %s, %s"), err.what(), errmsg);
				::fprintf( stderr, _TXT("error handling transaction in queue: %s, %s\n"), err.what(), errmsg);
				::fflush( stderr);
			}
			else
			{
				m_errorhnd->report( *ErrorCode(StrusComponentUtilities,ErrorOperationParse,ErrorCauseRuntimeError), _TXT("error handling transaction in queue: %s"), err.what());
				::fprintf( stderr, _TXT("error handling transaction in queue: %s\n"), err.what());
				::fflush( stderr);
			}
			strus::scoped_lock lock( m_mutex_errors);
			m_errors.push_back( m_errorhnd->fetchError());
		}
	}
}

void CommitQueue::pushTransaction( StorageTransactionInterface* transaction)
{
	{
		strus::scoped_lock lock( m_mutex_openTransactions);
		m_openTransactions.push( StorageTransactionReference( transaction));
		++m_nofOpenTransactions;
	}
	handleWaitingTransactions();
}

Reference<StorageTransactionInterface> CommitQueue::getNextTransaction()
{
	strus::scoped_lock lock( m_mutex_openTransactions);

	if (m_nofOpenTransactions == 0) return Reference<StorageTransactionInterface>();
	--m_nofOpenTransactions;
	Reference<StorageTransactionInterface>  rt = m_openTransactions.front();
	m_openTransactions.pop();
	return rt;
}

