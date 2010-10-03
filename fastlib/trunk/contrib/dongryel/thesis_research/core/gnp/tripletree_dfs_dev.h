/** @file tripletree_dfs_dev.h
 *
 *  @author Dongryeol Lee (dongryel@cc.gatech.edu)
 */

#ifndef CORE_GNP_TRIPLETREE_DFS_DEV_H
#define CORE_GNP_TRIPLETREE_DFS_DEV_H

#include <armadillo>
#include "tripletree_dfs.h"
#include "core/table/table.h"
#include "core/gnp/triple_distance_sq.h"

template<typename ProblemType>
ProblemType *core::gnp::TripletreeDfs<ProblemType>::problem() {
  return problem_;
}

template<typename ProblemType>
typename ProblemType::TableType
*core::gnp::TripletreeDfs<ProblemType>::table() {
  return table_;
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::ResetStatistic() {
  ResetStatisticRecursion_(table_->get_tree());
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::Init(ProblemType &problem_in) {
  problem_ = &problem_in;
  table_ = problem_->table();
  ResetStatistic();
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::Compute(
  const core::metric_kernels::AbstractMetric &metric,
  typename ProblemType::ResultType *query_results) {

  // Allocate space for storing the final results.
  query_results->Init(table_->n_entries());

  // Call the algorithm computation.
  std::vector< TreeType *> root_nodes(3, table_->get_tree());
  core::gnp::TripleRangeDistanceSq triple_range_distance_sq;
  triple_range_distance_sq.Init(metric, *table_, root_nodes);

  PreProcess_(table_->get_tree());
  TripletreeCanonical_(
    metric,
    triple_range_distance_sq,
    problem_->global().relative_error(),
    1.0 - problem_->global().probability(),
    query_results);
  PostProcess_(metric, table_->get_tree(), query_results);
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::ResetStatisticRecursion_(
  typename ProblemType::TableType::TreeType *node) {
  if(table_->get_node_stat(node) != NULL) {
    delete table_->get_node_stat(node);
  }
  table_->get_node_stat(node) = new typename ProblemType::StatisticType();
  dynamic_cast<typename ProblemType::StatisticType *>(
    table_->get_node_stat(node))->SetZero();
  if(table_->node_is_leaf(node) == false) {
    ResetStatisticRecursion_(table_->get_node_left_child(node));
    ResetStatisticRecursion_(table_->get_node_right_child(node));
  }
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::PreProcess_(
  typename ProblemType::TableType::TreeType *qnode) {

  typename ProblemType::StatisticType &qnode_stat =
    *(dynamic_cast<typename ProblemType::StatisticType *>(
        table_->get_node_stat(qnode)));
  qnode_stat.SetZero();

  if(! table_->node_is_leaf(qnode)) {
    PreProcess_(table_->get_node_left_child(qnode));
    PreProcess_(table_->get_node_right_child(qnode));
  }
}

template<typename ProblemType>
typename core::gnp::TripletreeDfs<ProblemType>::TableType::TreeIterator
core::gnp::TripletreeDfs<ProblemType>::GetNextNodeIterator_(
  const core::gnp::TripleRangeDistanceSq &range_sq_in,
  int node_index,
  const typename TableType::TreeIterator &it_in) {

  if(range_sq_in.node(node_index) != range_sq_in.node(node_index + 1)) {
    return table_->get_node_iterator(range_sq_in.node(node_index + 1));
  }
  else {
    return it_in;
  }
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::TripletreeBase_(
  const core::metric_kernels::AbstractMetric &metric,
  const core::gnp::TripleRangeDistanceSq &range_sq_in,
  typename ProblemType::ResultType *query_results) {

  // Temporary postponed objects to be used within the triple loop.
  std::vector< typename ProblemType::PostponedType > point_postponeds;
  point_postponeds.resize(3);

  // The triple object used for keeping track of the squared
  // distances.
  core::gnp::TripleDistanceSq distance_sq_set;

  // Loop through the first node.
  typename TableType::TreeIterator first_node_it =
    table_->get_node_iterator(range_sq_in.node(0));
  do {
    // Get the point in the first node.
    core::table::DenseConstPoint first_point;
    int first_point_index;
    first_node_it.Next(&first_point, &first_point_index);
    distance_sq_set.ReplaceOnePoint(metric, first_point, 0);

    // Construct the second iterator and start looping.
    typename TableType::TreeIterator second_node_it =
      GetNextNodeIterator_(range_sq_in, 0, first_node_it);
    while(second_node_it.HasNext()) {

      // Get the point in the second node.
      core::table::DenseConstPoint second_point;
      int second_point_index;
      second_node_it.Next(&second_point, &second_point_index);
      distance_sq_set.ReplaceOnePoint(metric, second_point, 1);

      // Loop through the third node.
      typename TableType::TreeIterator third_node_it =
        GetNextNodeIterator_(range_sq_in, 1, second_node_it);
      while(third_node_it.HasNext()) {

        // Get thet point in the third node.
        core::table::DenseConstPoint third_point;
        int third_point_index;
        third_node_it.Next(&third_point, &third_point_index);
        distance_sq_set.ReplaceOnePoint(metric, third_point, 2);

        // Add the contribution due to the triple that has been chosen
        // to each of the query point.
        problem_->global().ApplyContribution(
          distance_sq_set, &point_postponeds);

        // Apply the postponed contribution to each query result.
        query_results->ApplyPostponed(
          first_point_index, point_postponeds[0]);
        query_results->ApplyPostponed(
          second_point_index, point_postponeds[1]);
        query_results->ApplyPostponed(
          third_point_index, point_postponeds[2]);

      } // end of looping over the third node.
    } // end of looping over the second node.
  } // end of looping over the first node.
  while(first_node_it.HasNext());

  for(int node_index = 0; node_index < 3; node_index++) {
    if(node_index == 0 ||
        range_sq_in.node(node_index) != range_sq_in.node(node_index - 1)) {

      // Clear the summary statistics of the current query node so that we
      // can refine it to better bounds.
      typename ProblemType::TableType::TreeType *node =
        range_sq_in.node(node_index);
      typename ProblemType::StatisticType *node_stat =
        dynamic_cast< typename ProblemType::StatisticType *>(
          problem_->table()->get_node_stat(node));
      node_stat->summary_.StartReaccumulate();

      // Get the query node iterator and the reference node iterator.
      typename ProblemType::TableType::TreeIterator node_iterator =
        problem_->table()->get_node_iterator(node);

      // Add the pruned tuples at this base case to the postponed of
      // the current node (which will be all cleared when the function
      // is exited).
      node_stat->postponed_.pruned_ += range_sq_in.num_tuples(node_index);

      // Apply the postponed contribution to the each node.
      while(node_iterator.HasNext()) {

        // Get the query point and its real index.
        core::table::DenseConstPoint q_col;
        int q_index;
        node_iterator.Next(&q_col, &q_index);

        // Incorporate the postponed information.
        query_results->ApplyPostponed(q_index, node_stat->postponed_);

        // Refine min and max summary statistics.
        node_stat->summary_.Accumulate(*query_results, q_index);

      } // end of looping over each query point.

      // Clear postponed information.
      node_stat->postponed_.SetZero();
    }
  } // end of looping over each node.
}

template<typename ProblemType>
bool core::gnp::TripletreeDfs<ProblemType>::CanSummarize_(
  const core::gnp::TripleRangeDistanceSq &triple_range_distance_sq_in,
  const typename ProblemType::DeltaType &delta,
  typename ProblemType::ResultType *query_results) {

  return false;
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::Summarize_(
  const core::gnp::TripleRangeDistanceSq &triple_range_distance_sq,
  const typename ProblemType::DeltaType &delta,
  typename ProblemType::ResultType *query_results) {

}

template<typename ProblemType>
bool core::gnp::TripletreeDfs<ProblemType>::TripletreeCanonical_(
  const core::metric_kernels::AbstractMetric &metric,
  const core::gnp::TripleRangeDistanceSq &triple_range_distance_sq,
  double relative_error,
  double failure_probability,
  typename ProblemType::ResultType *query_results) {

  TripletreeBase_(metric, triple_range_distance_sq, query_results);
  return true;
}

template<typename ProblemType>
void core::gnp::TripletreeDfs<ProblemType>::PostProcess_(
  const core::metric_kernels::AbstractMetric &metric,
  typename ProblemType::TableType::TreeType *qnode,
  typename ProblemType::ResultType *query_results) {

  typename ProblemType::StatisticType *qnode_stat =
    dynamic_cast<typename ProblemType::StatisticType *>(
      table_->get_node_stat(qnode));

  if(table_->node_is_leaf(qnode)) {

    typename ProblemType::TableType::TreeIterator qnode_iterator =
      table_->get_node_iterator(qnode);

    while(qnode_iterator.HasNext()) {
      core::table::DenseConstPoint q_col;
      int q_index;
      qnode_iterator.Next(&q_col, &q_index);
      query_results->ApplyPostponed(q_index, qnode_stat->postponed_);
      query_results->PostProcess(metric, q_index, problem_->global());
    }
    qnode_stat->postponed_.SetZero();
  }
  else {
    typename ProblemType::TableType::TreeType *qnode_left =
      table_->get_node_left_child(qnode);
    typename ProblemType::TableType::TreeType *qnode_right =
      table_->get_node_right_child(qnode);
    typename ProblemType::StatisticType *qnode_left_stat =
      dynamic_cast<typename ProblemType::StatisticType *>(
        table_->get_node_stat(qnode_left));
    typename ProblemType::StatisticType *qnode_right_stat =
      dynamic_cast<typename ProblemType::StatisticType *>(
        table_->get_node_stat(qnode_right));

    qnode_left_stat->postponed_.ApplyPostponed(qnode_stat->postponed_);
    qnode_right_stat->postponed_.ApplyPostponed(qnode_stat->postponed_);
    qnode_stat->postponed_.SetZero();

    PostProcess_(metric, qnode_left,  query_results);
    PostProcess_(metric, qnode_right, query_results);
  }
}

#endif
