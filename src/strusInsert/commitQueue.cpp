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
			::printf( "\rinserted %u documents (total %u)          ",
					nofDocsInserted, totalNofDocuments);
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

