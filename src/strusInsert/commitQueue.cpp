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
#include "commitQueue.hpp"
#include "private/internationalization.hpp"
#include "strus/errorBufferInterface.hpp"
#include <cstdio>
#include <stdexcept>

using namespace strus;

void CommitQueue::pushTransactionPromise( const Index& mindocno)
{
	utils::ScopedLock lock( m_mutex_promisedTransactions);
	m_promisedTransactions.insert( mindocno);
}

void CommitQueue::breachTransactionPromise( const Index& mindocno)
{
	utils::ScopedLock lock( m_mutex_promisedTransactions);
	m_promisedTransactions.erase( mindocno);

	handleWaitingTransactions();
}

bool CommitQueue::testAndGetFirstPromise( const Index& mindocno)
{
	utils::ScopedLock lock( m_mutex_promisedTransactions);
	if (mindocno == 0 || m_promisedTransactions.size() == 0)
	{
		return true;
	}
	else if (*m_promisedTransactions.begin() == mindocno)
	{
		m_promisedTransactions.erase( m_promisedTransactions.begin());
		return true;
	}
	else
	{
		return false;
	}
}

void CommitQueue::handleWaitingTransactions()
{
	for (;;)
	{
		try
		{
			Index nofDocs;
			Index nofDocsAllocated;
			unsigned int nofOpenTransactions = 0;
			Reference<StorageTransactionInterface>
				transaction = getNextTransaction(
					nofDocs, nofDocsAllocated, nofOpenTransactions);
			if (!transaction.get()) break;
			if (!transaction->commit())
			{
				throw strus::runtime_error(_TXT("transaction commit failed"));
			}
			Index totalNofDocuments = m_storage->nofDocumentsInserted();
			Index nofDocsInserted = totalNofDocuments - m_nofDocuments;
			::printf( "\rinserted %u documents (total %u), %u transactions open     ",
					nofDocsInserted, totalNofDocuments, nofOpenTransactions);
			::fflush(stdout);
		}
		catch (const std::bad_alloc&)
		{
			std::cerr << _TXT("out of memory handling transaction in queue") << std::endl;
		}
		catch (const std::exception& err)
		{
			const char* errmsg = m_errorhnd->fetchError();
			if (errmsg)
			{
				std::cerr << _TXT("error handling transaction in queue: ") << err.what() << "; " << errmsg << std::endl;
			}
			else
			{
				std::cerr << _TXT("error handling transaction in queue: ") << err.what() << std::endl;
			}
		}
	}
}

void CommitQueue::pushTransaction(
	StorageTransactionInterface* transaction,
	const Index& minDocno,
	const Index& nofDocuments,
	const Index& nofDocumentsAllocated)
{
	{
		utils::ScopedLock lock( m_mutex_openTransactions);
		m_openTransactions.insert(
			OpenTransaction( transaction, minDocno, nofDocuments, nofDocumentsAllocated));
		++m_nofOpenTransactions;
	}
	handleWaitingTransactions();
}

Reference<StorageTransactionInterface>
	CommitQueue::getNextTransaction(
		Index& nofDocs, Index& nofDocsAllocated, unsigned int& nofOpenTransactions_)
{
	Reference<StorageTransactionInterface> rt;
	utils::ScopedLock lock( m_mutex_openTransactions);

	std::set<OpenTransaction>::iterator ti = m_openTransactions.begin();
	if (ti != m_openTransactions.end())
	{
		if (testAndGetFirstPromise( ti->minDocno()))
		{
			rt = ti->transaction();
			nofDocs = ti->nofDocuments();
			nofDocsAllocated = ti->nofDocumentsAllocated();
			m_openTransactions.erase( ti++);
			nofOpenTransactions_ = --m_nofOpenTransactions;
		}
	}
	return rt;
}

