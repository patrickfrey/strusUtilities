/*
 * Copyright (c) 2017 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "metadataExpression.hpp"

using namespace strus;

void MetaDataExpression::pushCompare( MetaDataRestrictionInterface::CompareOperator op, const std::string& fieldtype, const std::string& value)
{
	++m_fieldno_cnt;
	m_analyzer->putField( m_fieldno_cnt, fieldtype, value);
	m_fieldno_stack.push_back( m_fieldno_cnt);
	std::vector<int> fieldnoList( m_fieldno_stack.end()-1, m_fieldno_stack.end());
	int groupid = getCompareOp( op);
	m_analyzer->groupElements( groupid, fieldnoList, QueryAnalyzerContextInterface::GroupUnique, true/*groupSingle*/);
}

void MetaDataExpression::pushOperator( const BooleanOp& op, unsigned int argc)
{
	if (m_fieldno_stack.size() < argc) throw strus::runtime_error( "%s", _TXT("push metadata operator without all arguments defined"));
	int* fnstart = m_fieldno_stack.data() + m_fieldno_stack.size() - argc;
	int* fnend = fnstart + argc;
	std::vector<int> fieldnoList( fnstart, fnend);

	int groupid = getBooleanOp( op);
	QueryAnalyzerContextInterface::GroupBy groupBy = QueryAnalyzerContextInterface::GroupAll;
	m_analyzer->groupElements( groupid, fieldnoList, groupBy, true/*groupSingle*/);
	m_fieldno_stack.resize( m_fieldno_stack.size() - argc + 1);
}

struct MetaDataComparison
{
	MetaDataRestrictionInterface::CompareOperator cmpop;
	const analyzer::QueryTerm* term;
	bool newGroup;

	MetaDataComparison( MetaDataRestrictionInterface::CompareOperator cmpop_, const analyzer::QueryTerm* term_, bool newGroup_)
		:cmpop(cmpop_),term(term_),newGroup(newGroup_){}
	MetaDataComparison( const MetaDataComparison& o)
		:cmpop(o.cmpop),term(o.term),newGroup(o.newGroup){}

	void translate( QueryInterface& query) const
	{
		NumericVariant numval;
		if (!numval.initFromString( term->value().c_str())) throw strus::runtime_error( "%s", _TXT("metadata value not convertible to numeric value"));
		query.addMetaDataRestrictionCondition( cmpop, term->type(), numval, newGroup);
	}
};

void MetaDataExpression::translate( QueryInterface& query)
{
	const analyzer::QueryTermExpression& expr = expression();
	int termc = 0;

	// Build a simpler data structure of a CNF (conjunctive normal form):
	std::vector<MetaDataComparison> cmplist;
	std::vector<analyzer::QueryTermExpression::Instruction>::const_iterator
		ii = expr.instructions().begin(), ie = expr.instructions().end();
	for (; ii != ie; ++ii)
	{
		switch (ii->opCode())
		{
			case analyzer::QueryTermExpression::Instruction::Term:
			{
				const analyzer::QueryTerm* term = &expr.term( ii->idx());
				++termc;
				++ii;
				if (ii != ie
					&& ii->opCode() == analyzer::QueryTermExpression::Instruction::Operator
					&& isCompareOp( ii->idx()))
				{
					MetaDataRestrictionInterface::CompareOperator
						opr = MetaDataExpression::compareOp( ii->idx());
					cmplist.push_back( MetaDataComparison( opr, term, true));
				}
				else
				{
					throw strus::runtime_error( "%s", _TXT("internal: metadata compare operator got lost"));
				}
				break;
			}
			case analyzer::QueryTermExpression::Instruction::Operator:
			{
				if (isBooleanOp( ii->idx()))
				{
					MetaDataExpression::BooleanOp bop = booleanOp( ii->idx());
					if (bop == MetaDataExpression::OperatorOR)
					{
						if (termc > 1)
						{
							if (termc > ii->nofOperands())
							{
								throw strus::runtime_error( "%s", _TXT("CNF (conjunctive normal form) expected for meta data expression"));
							}
							// We check that all operands of an OR are atomic terms (CNF):
							std::vector<MetaDataComparison>::iterator
								ci = cmplist.end() - termc + 1, ce = cmplist.end();
							for (; ci != ce; ++ci)
							{
								ci->newGroup = false;
							}
						}
					}
					termc = 0;
				}
				else
				{
					throw strus::runtime_error( "%s", _TXT("internal: encountered illegal meta data operator, boolean operator expected as join of comparisons"));
				}
				break;
			}
		}
	}
	// Translate the result into query restrictions:
	std::vector<MetaDataComparison>::const_iterator ci = cmplist.begin(), ce = cmplist.end();
	for (; ci != ce; ++ci)
	{
		ci->translate( query);
	}
}

