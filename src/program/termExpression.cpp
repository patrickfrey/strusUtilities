/*
 * Copyright (c) 2017 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "termExpression.hpp"

using namespace strus;

void TermExpression::pushField( const std::string& fieldtype, const std::string& value)
{
	typedef QueryAnalyzerStruct::GroupOperatorList GroupOperatorList;

	++m_fieldno_cnt;
	m_analyzer->putField( m_fieldno_cnt, fieldtype, value);
	m_fieldno_stack.push_back( m_fieldno_cnt);
	const GroupOperatorList& gop = m_analyzerStruct->autoGroupOperators( fieldtype);
	if (!gop.empty())
	{
		std::vector<int> fieldnoList( m_fieldno_stack.end()-1, m_fieldno_stack.end());
	
		GroupOperatorList::const_iterator gi = gop.begin(), ge = gop.end();
		for (; gi != ge; ++gi)
		{
			int groupid = newOperator( gi->opr.name, gi->opr.range, gi->opr.cardinality);
			m_analyzer->groupElements( groupid, fieldnoList, gi->groupBy, gi->groupSingle);
		}
	}
}

void TermExpression::pushExpression( const std::string& op, unsigned int argc, int range, unsigned int cardinality)
{
	if (m_fieldno_stack.size() < argc) throw strus::runtime_error( "%s", _TXT("push expression without all arguments defined"));
	int* fnstart = m_fieldno_stack.data() + m_fieldno_stack.size() - argc;
	int* fnend = fnstart + argc;
	std::vector<int> fieldnoList( fnstart, fnend);

	int groupid = newOperator( op, range, cardinality);
	QueryAnalyzerContextInterface::GroupBy groupBy = QueryAnalyzerContextInterface::GroupAll;
	m_analyzer->groupElements( groupid, fieldnoList, groupBy, true/*groupSingle*/);
	m_fieldno_stack.resize( m_fieldno_stack.size() - argc + 1);
}

void TermExpression::attachVariable( const std::string& name)
{
	if (m_fieldno_stack.empty()) throw strus::runtime_error( "%s", _TXT("attach variable not allowed without any fields defined"));
	std::vector<int> fieldnoList( m_fieldno_stack.end()-1, m_fieldno_stack.end());

	int groupid = newVariable( name);
	QueryAnalyzerContextInterface::GroupBy groupBy = QueryAnalyzerContextInterface::GroupEvery;
	m_analyzer->groupElements( groupid, fieldnoList, groupBy, true/*groupSingle*/);
}

void TermExpression::assignFeature( const std::string& name, double weight)
{
	if (m_fieldno_stack.empty()) throw strus::runtime_error( "%s", _TXT("assign feature not allowed without any fields defined"));
	std::vector<int> fieldnoList( m_fieldno_stack.end()-1, m_fieldno_stack.end());

	int groupid = newFeature( name, weight);
	QueryAnalyzerContextInterface::GroupBy groupBy = QueryAnalyzerContextInterface::GroupEvery;
	m_analyzer->groupElements( groupid, fieldnoList, groupBy, true/*groupSingle*/);
}

void TermExpression::translate( QueryInterface& query, const QueryProcessorInterface* queryproc)
{
	unsigned int nofargs = 0;
	std::vector<analyzer::QueryTermExpression::Instruction>::const_iterator
		ii = m_expr.instructions().begin(), ie = m_expr.instructions().end();
	for (; ii != ie; ++ii)
	{
		switch (ii->opCode())
		{
			case analyzer::QueryTermExpression::Instruction::Term:
			{
				nofargs += 1;
				const analyzer::QueryTerm& term = m_expr.term( ii->idx());
				query.pushTerm( term.type(), term.value(), term.len());
				break;
			}
			case analyzer::QueryTermExpression::Instruction::Operator:
			{
				if (isVariable( ii->idx()))
				{
					query.attachVariable( variableName( ii->idx()));
				}
				else if (isFeature( ii->idx()))
				{
					const Feature& feat = feature( ii->idx());
					query.defineFeature( feat.name, feat.weight);
					nofargs -= 1;
				}
				else if (isOperator( ii->idx()))
				{
					const Operator& op = operatorStruct( ii->idx());
					const PostingJoinOperatorInterface* joinopfunc = queryproc->getPostingJoinOperator( op.name);
					if (!joinopfunc) throw strus::runtime_error(_TXT("posting join operator '%s' not known"), op.name.c_str());
					query.pushExpression( joinopfunc, ii->nofOperands(), op.range, op.cardinality);
					nofargs = nofargs - ii->nofOperands() + 1;
				}
				break;
			}
		}
	}
	if (nofargs)
	{
		throw strus::runtime_error( "%s", _TXT("not all term expressions assigned to features"));
	}
}



