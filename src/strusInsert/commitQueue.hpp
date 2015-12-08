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
#ifndef _STRUS_INSERTER_COMMIT_QUEUE_HPP_INCLUDED
#define _STRUS_INSERTER_COMMIT_QUEUE_HPP_INCLUDED
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/docnoRangeAllocatorInterface.hpp"
#include "strus/index.hpp"
#include "strus/reference.hpp"
#include "private/utils.hpp"
#include <vector>
#include <string>
#include <set>

namespace strus {
/// \brief Forward declaration
class ErrorBufferInterface;

class CommitQueue
{
public:
	CommitQueue(
			StorageClientInterface* storage_,
			ErrorBufferInterface* errorhnd_)
		:m_storage(storage_),m_nofDocuments(0),m_nofOpenTransactions(0),m_errorhnd(errorhnd_)
	{
		m_nofDocuments = m_storage->nofDocumentsInserted();
	}

	void pushTransaction(
		StorageTransactionInterface* transaction,
		const Index& minDocno,
		const Index& nofDocuments,
		const Index& nofDocumentsAllocated);

	void pushTransactionPromise( const Index& mindocno);
	void breachTransactionPromise( const Index& mindocno);

private:
	class OpenTransaction
	{
	public:
		OpenTransaction( StorageTransactionInterface* t, Index d, Index n, Index a)
			:m_transaction(t),m_minDocno(d),m_nofDocuments(n),m_nofDocumentsAllocated(a){}
		OpenTransaction( const OpenTransaction& o)
			:m_transaction(o.m_transaction)
			,m_minDocno(o.m_minDocno)
			,m_nofDocuments(o.m_nofDocuments)
			,m_nofDocumentsAllocated(o.m_nofDocumentsAllocated){}

		Reference<StorageTransactionInterface> transaction() const
		{
			return m_transaction;
		}

		Index minDocno() const
		{
			return m_minDocno;
		}

		Index nofDocuments() const
		{
			return m_nofDocuments;
		}

		Index nofDocumentsAllocated() const
		{
			return m_nofDocumentsAllocated;
		}

		bool operator<( const OpenTransaction& o) const
		{
			return m_minDocno < o.m_minDocno;
		}

	private:
		Reference<StorageTransactionInterface> m_transaction;
		Index m_minDocno;
		Index m_nofDocuments;
		Index m_nofDocumentsAllocated;
	};

	void handleWaitingTransactions();
	Reference<StorageTransactionInterface>
		getNextTransaction(
			Index& nofDocs, Index& nofDocsAllocated, unsigned int& nofOpenTransactions_);
	bool testAndGetFirstPromise( const Index& mindocno);

private:
	StorageClientInterface* m_storage;
	Index m_nofDocuments;
	unsigned int m_nofOpenTransactions;
	std::set<OpenTransaction> m_openTransactions;
	utils::Mutex m_mutex_openTransactions;
	std::set<Index> m_promisedTransactions;
	utils::Mutex m_mutex_promisedTransactions;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif
