/*
 * Copyright (c) 2017 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_BINDING_IMPL_METADATA_EXPRESSION_HPP_INCLUDED
#define _STRUS_BINDING_IMPL_METADATA_EXPRESSION_HPP_INCLUDED
#include "strus/analyzer/queryTermExpression.hpp"
#include "strus/metaDataRestrictionInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "private/internationalization.hpp"
#include <vector>
#include <string>

namespace strus {

/// \brief Analyzed term expression structure
class MetaDataExpression
{
public:
	enum {BooleanOpOfs=1<<31};

	/// \brief Constructor
	MetaDataExpression( const QueryAnalyzerInterface* analyzer_, ErrorBufferInterface* errorhnd_)
		:m_errorhnd(errorhnd_),m_analyzer(analyzer_->createContext()),m_fieldno_stack(),m_fieldno_cnt(0),m_expr()
	{
		if (!m_analyzer) throw strus::runtime_error(_TXT("failed to create analyzer context: %s"), m_errorhnd->fetchError());
	}
	~MetaDataExpression()
	{
		delete m_analyzer;
	}

	enum BooleanOp {OperatorOR,OperatorAND};

	/// \brief Reference to query structure
	const analyzer::QueryTermExpression& expression() const
	{
		return m_expr;
	}
	static bool isBooleanOp( unsigned int groupidx)
	{
		return groupidx >= (unsigned int)BooleanOpOfs;
	}
	static bool isCompareOp( unsigned int groupidx)
	{
		return groupidx && groupidx < (unsigned int)BooleanOpOfs;
	}
	static BooleanOp booleanOp( unsigned int groupidx)
	{
		return (BooleanOp)( groupidx-BooleanOpOfs-1);
	}
	static MetaDataRestrictionInterface::CompareOperator compareOp( unsigned int groupidx)
	{
		return (MetaDataRestrictionInterface::CompareOperator)( groupidx-1);
	}
	static unsigned int getCompareOp( const MetaDataRestrictionInterface::CompareOperator& op)
	{
		return op+1;
	}
	static unsigned int getBooleanOp( const BooleanOp& op)
	{
		return op+BooleanOpOfs+1;
	}

	void pushCompare( MetaDataRestrictionInterface::CompareOperator op, const std::string& fieldtype, const std::string& value);

	void pushOperator( const BooleanOp& op, unsigned int argc);

	void analyze()
	{
		m_expr = m_analyzer->analyze();
		if (m_errorhnd->hasError()) throw strus::runtime_error(_TXT("failed to analyze metadata expression: %s"), m_errorhnd->fetchError());
	}

	void translate( QueryInterface& query);

private:
	MetaDataExpression( const MetaDataExpression&){}	//... non copyable
	void operator=( const MetaDataExpression&){}		//... non copyable

private:
	ErrorBufferInterface* m_errorhnd;
	QueryAnalyzerContextInterface* m_analyzer;
	std::vector<unsigned int> m_fieldno_stack;
	unsigned int m_fieldno_cnt;
	analyzer::QueryTermExpression m_expr;
};

}//namespace
#endif

