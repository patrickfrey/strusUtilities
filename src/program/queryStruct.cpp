/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Query structure used by program loader
/// \file queryStruct.cpp
#include "queryStruct.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "private/internationalization.hpp"

using namespace strus;

QueryStruct::QueryStruct( const QueryStruct& o)
	:m_expressions(o.m_expressions),m_features(o.m_features)
	,m_metadata(o.m_metadata),m_variables(o.m_variables)
	,m_groups(o.m_groups),m_selectionFeatures(o.m_selectionFeatures)
	,m_analyzer(o.m_analyzer)
	,m_fieldNoStack(o.m_fieldNoStack),m_fieldNo(o.m_fieldNo)
{}

QueryStruct::QueryStruct( const QueryAnalyzerInterface* qai)
	:m_expressions(),m_features(),m_metadata(),m_variables(),m_groups(),m_selectionFeatures()
	,m_analyzer(qai->createContext()),m_fieldNoStack(),m_fieldNo(0)
{}

void QueryStruct::defineVariable( const std::string& name)
{
	std::vector<unsigned int> fieldNoList;
	fieldNoList.push_back( m_fieldNoStack.back());
	m_analyzer->groupElements( m_groups.size(), fieldNoList, QueryAnalyzerContextInterface::GroupEvery, true/*groupSingle*/);
	m_groups.push_back( QueryGroupStruct( QueryGroupStruct::QueryVariableDef, m_variables.size()));
	m_variables.push_back( name);
}

void QueryStruct::defineFeature( const std::string& featureSet, float weight)
{
	std::vector<unsigned int> fieldNoList;
	fieldNoList.push_back( m_fieldNoStack.back());
	m_analyzer->groupElements( m_groups.size(), fieldNoList, QueryAnalyzerContextInterface::GroupEvery, true/*groupSingle*/);
	m_groups.push_back( QueryGroupStruct( QueryGroupStruct::QueryFeatureStructType, m_features.size()));
	m_features.push_back( QueryFeatureStruct( featureSet, weight));
}

void QueryStruct::defineImplicitSelection( const std::string& fieldType, const std::string& fieldContent)
{
	m_selectionFeatures.push_back( std::pair<std::string,std::string>( fieldType, fieldContent));
}

void QueryStruct::defineField( const std::string& fieldType, const std::string& fieldContent)
{
	m_fieldNoStack.push_back( m_fieldNo);
	m_analyzer->putField( m_fieldNo++, fieldType, fieldContent);
}

void QueryStruct::defineMetaDataRestriction( const std::string& metaDataName, const MetaDataRestrictionInterface::CompareOperator& cmp, const std::string& fieldType, const std::string& fieldContent, bool newGroup)
{
	m_analyzer->putField( m_fieldNo, fieldType, fieldContent);
	std::vector<unsigned int> fieldNoList;
	fieldNoList.push_back( m_fieldNo++);
	m_analyzer->groupElements( m_groups.size(), fieldNoList, QueryAnalyzerContextInterface::GroupEvery, true/*groupSingle*/);
	m_groups.push_back( QueryGroupStruct( QueryGroupStruct::QueryMetaDataStructType, m_metadata.size()));
	m_metadata.push_back( QueryMetaDataStruct( metaDataName, cmp, newGroup));
}

void QueryStruct::defineExpression( const PostingJoinOperatorInterface* function, unsigned int arg, int range, unsigned int cardinality)
{
	if (arg > m_fieldNoStack.size()) throw strus::runtime_error(_TXT("too many arguments selected for function"));
	if (!arg) throw strus::runtime_error(_TXT("no arguments passed to posting join operator"));
	std::vector<unsigned int> fieldNoList( m_fieldNoStack.begin() + m_fieldNoStack.size() - arg, m_fieldNoStack.end());
	m_analyzer->groupElements( m_groups.size(), fieldNoList, QueryAnalyzerContextInterface::GroupAll, false/*groupSingle*/);
	m_groups.push_back( QueryGroupStruct( QueryGroupStruct::QueryExpressionStructType, m_expressions.size()));
	m_expressions.push_back( QueryExpressionStruct( function, arg, range, cardinality));
	m_fieldNoStack.resize( m_fieldNoStack.size() - arg);
	m_fieldNoStack.push_back( fieldNoList[0]);
}

void QueryStruct::defineSelectionFeatures( const QueryProcessorInterface* queryproc, const QueryDescriptors& qdescr)
{
	std::vector<std::pair<std::string,std::string> >::const_iterator si = m_selectionFeatures.begin(), se = m_selectionFeatures.end();
	for (; si != se; ++si)
	{
		m_fieldNoStack.push_back( m_fieldNo);
		m_analyzer->putField( m_fieldNo++, si->first, si->second);
	}
	unsigned int cardinality_calculated = qdescr.defaultSelectionTermPart * m_selectionFeatures.size() + 1;
	unsigned int cardinality = std::min( cardinality_calculated, (unsigned int)m_selectionFeatures.size());
	const PostingJoinOperatorInterface* join = queryproc->getPostingJoinOperator( qdescr.defaultSelectionJoin);
	defineExpression( join, (unsigned int)m_selectionFeatures.size(), 0, cardinality);
	defineFeature( qdescr.selectionFeatureSet, 1.0);
}

void QueryStruct::translate( QueryInterface& query, const QueryProcessorInterface* queryproc, ErrorBufferInterface* errorhnd)
{
	analyzer::Query queryana = m_analyzer->analyze();
	if (errorhnd->hasError())
	{
		throw strus::runtime_error( _TXT("failed to analyze query: %s"), errorhnd->fetchError());
	}
	std::vector<analyzer::Query::Instruction>::const_iterator
		ii = queryana.instructions().begin(), ie = queryana.instructions().end();
	for (; ii != ie; ++ii)
	{
		switch (ii->opCode())
		{
			case analyzer::Query::Instruction::MetaData:
			{
				const analyzer::MetaData& elem = queryana.metadata( ii->idx());
				if (++ii == ie)
				{
					throw strus::runtime_error(_TXT("internal: unexpected end of serialization after MetaData"));;
				}
				if (ii->opCode() != analyzer::Query::Instruction::Operator)
				{
					throw strus::runtime_error(_TXT("internal: unexpected operation after MetaData"));
				}
				const QueryGroupStruct& group = m_groups[ ii->idx()];
				if (group.type != QueryGroupStruct::QueryMetaDataStructType)
				{
					throw strus::runtime_error(_TXT("internal: group in argument of operation after MetaData"));
				}
				const QueryMetaDataStruct& mt = m_metadata[ group.idx];
				if (mt.name != elem.name()) 
				{
					throw strus::runtime_error(_TXT("internal: meta data element name does not match"));
				}
				query.addMetaDataRestrictionCondition( mt.cmp, mt.name, elem.value(), mt.newGroup);
				break;
			}
			case analyzer::Query::Instruction::Term:
			{
				const analyzer::Term& term = queryana.term( ii->idx());
				query.pushTerm( term.type(), term.value(), term.len());
				break;
			}
			case analyzer::Query::Instruction::Operator:
			{
				const QueryGroupStruct& group = m_groups[ ii->idx()];
				switch (group.type)
				{
					case QueryGroupStruct::QueryMetaDataStructType:
						throw strus::runtime_error(_TXT("internal: unexpected grouping operation"));
					case QueryGroupStruct::QueryExpressionStructType:
					{
						const QueryExpressionStruct& opr = m_expressions[ group.idx];
						query.pushExpression( opr.function, opr.arg, opr.range, opr.cardinality);
						break;
					}
					case QueryGroupStruct::QueryFeatureStructType:
					{
						const QueryFeatureStruct& feat = m_features[ group.idx];
						query.defineFeature( feat.featureSet, feat.weight);
						break;
					}
					case QueryGroupStruct::QueryVariableDef:
					{
						const std::string& varname = m_variables[ group.idx];
						query.attachVariable( varname);
						break;
					}
				}
				break;
			}
		}
	}
}

