//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// operator_to_plan_transformer.cpp
//
// Identification: src/optimizer/operator_to_plan_transformer.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "expression/expression_util.h"

#include "optimizer/operator_to_plan_transformer.h"

#include "../include/optimizer/operator_expression.h"
#include "planner/hash_join_plan.h"
#include "planner/nested_loop_join_plan.h"
#include "planner/projection_plan.h"
#include "planner/seq_scan_plan.h"

namespace peloton {
namespace optimizer {

OperatorToPlanTransformer::OperatorToPlanTransformer() {}

planner::AbstractPlan *OperatorToPlanTransformer::ConvertOpExpression(
    std::shared_ptr<OperatorExpression> plan) {
  VisitOpExpression(plan);
  return output_plan.get();
}

void OperatorToPlanTransformer::Visit(const PhysicalScan *op) {
  auto children = current_children;
  std::vector<oid_t> column_ids;
  for (Column *col : op->columns) {
    TableColumn *table_col = dynamic_cast<TableColumn *>(col);
    assert(table_col != nullptr);
    column_ids.push_back(table_col->ColumnIndexOid());
  }
  left_columns = op->columns;
  output_columns = op->columns;

  expression::AbstractExpression *predicate =
      ConvertToAbstractExpression(children[1]);

  output_plan.reset(new planner::SeqScanPlan(op->table, predicate, column_ids));
}

void OperatorToPlanTransformer::Visit(const PhysicalProject *) {}

void OperatorToPlanTransformer::Visit(const PhysicalComputeExprs *) {
  auto children = current_children;
  assert(children.size() == 2);

  VisitOpExpression(children[0]);
  std::unique_ptr<planner::AbstractPlan> child_plan = std::move(output_plan);
  left_columns = output_columns;

  std::vector<Column *> proj_columns;
  std::vector<expression::AbstractExpression *> exprs;
  {
    for (std::shared_ptr<OperatorExpression> op_expr :
         children[1]->Children()) {
      /*
      assert(op_expr->Op().type() == OpType::ProjectColumn);
      assert(op_expr->Children().size() == 1);

      const ExprProjectColumn *proj_col =
          op_expr->Op().as<ExprProjectColumn>();
      proj_columns.push_back(proj_col->column_);
      */

      exprs.push_back(ConvertToAbstractExpression(op_expr->Children()[0]));
    }
  }

  std::shared_ptr<const catalog::Schema> projection_schema(
      BuildSchemaFromColumns(proj_columns));

  // Build projection info from target list
  std::unique_ptr<planner::ProjectInfo> project_info(
      BuildProjectInfoFromExprs(exprs));

  output_columns = proj_columns;
  output_plan.reset(
      new planner::ProjectionPlan(std::move(project_info), projection_schema));
  output_plan->AddChild(std::move(child_plan));
}

void OperatorToPlanTransformer::Visit(const PhysicalFilter *) {
  auto children = current_children;
  assert(children.size() == 2);

  VisitOpExpression(children[0]);
  std::unique_ptr<planner::AbstractPlan> child_plan = std::move(output_plan);
  left_columns = output_columns;

  expression::AbstractExpression *predicate =
      ConvertToAbstractExpression(children[1]);

  output_plan.reset(new planner::SeqScanPlan(nullptr, predicate, {}));
  output_plan->AddChild(std::move(child_plan));
}

void OperatorToPlanTransformer::Visit(const PhysicalInnerNLJoin *) {
  auto children = current_children;
  assert(children.size() == 3);

  /* TODO: Fix ownership
  VisitOpExpression(children[0]);
  //planner::AbstractPlan *left_child = output_plan;
  left_columns = output_columns;

  VisitOpExpression(children[1]);
  //planner::AbstractPlan *right_child = output_plan;
  right_columns = output_columns;

  expression::AbstractExpression *predicate =
    ConvertToAbstractExpression(children[2]);

  output_columns = ConcatLeftAndRightColumns();

  catalog::Schema *project_schema = BuildSchemaFromColumns(output_columns);
  planner::ProjectInfo *proj_info =
    BuildProjectInfoFromColumns(output_columns);

  output_plan = new planner::NestedLoopJoinPlan(
    JOIN_TYPE_INNER, predicate, proj_info, project_schema, nullptr);
  output_plan->AddChild(left_child);
  output_plan->AddChild(right_child);
  */
}

void OperatorToPlanTransformer::Visit(const PhysicalLeftNLJoin *) {}

void OperatorToPlanTransformer::Visit(const PhysicalRightNLJoin *) {}

void OperatorToPlanTransformer::Visit(const PhysicalOuterNLJoin *) {}

void OperatorToPlanTransformer::Visit(const PhysicalInnerHashJoin *) {
  // auto children = current_children;
  // assert(children.size() == 2);

  // VisitOpExpression(children[0]);
  // planner::AbstractPlan *lef_child = output_plan;
  // left_columns = output_columns;

  // VisitOpExpression(children[1]);
  // planner::AbstractPlan *right_child = output_plan;
  // right_columns = output_columns;

  // planner::AbstractPlan *right_hash =
  //   new planner::HashPlan();
}

void OperatorToPlanTransformer::Visit(const PhysicalLeftHashJoin *) {}

void OperatorToPlanTransformer::Visit(const PhysicalRightHashJoin *) {}

void OperatorToPlanTransformer::Visit(const PhysicalOuterHashJoin *) {}

void OperatorToPlanTransformer::VisitOpExpression(
    std::shared_ptr<OperatorExpression> op) {
  // LM: I don't understand why we're copying prev information here and then
  // copy them back. It seems not used anywhere.
  std::vector<std::shared_ptr<OperatorExpression>> prev_children =
      current_children;
  auto prev_left_cols = left_columns;
  auto prev_right_cols = right_columns;
  current_children = op->Children();
  op->Op().Accept(this);
  current_children = prev_children;
  left_columns = prev_left_cols;
  right_columns = prev_right_cols;
}

expression::AbstractExpression *
OperatorToPlanTransformer::ConvertToAbstractExpression(
    UNUSED_ATTRIBUTE std::shared_ptr<OperatorExpression> op) {
  // FIXME: LM: fix this later.
  return nullptr;
}

catalog::Schema *OperatorToPlanTransformer::BuildSchemaFromColumns(
    std::vector<Column *> columns) {
  std::vector<catalog::Column> schema_columns;
  for (Column *column : columns) {
    schema_columns.push_back(GetSchemaColumnFromOptimizerColumn(column));
  }
  return new catalog::Schema(schema_columns);
}

planner::ProjectInfo *OperatorToPlanTransformer::BuildProjectInfoFromExprs(
    std::vector<expression::AbstractExpression *> exprs) {
  TargetList target_list;
  for (size_t col_id = 0; col_id < exprs.size(); ++col_id) {
    target_list.push_back({col_id, exprs[col_id]});
  }
  return new planner::ProjectInfo(std::move(target_list), {});
}

std::vector<Column *> OperatorToPlanTransformer::ConcatLeftAndRightColumns() {
  std::vector<Column *> columns = left_columns;
  columns.insert(columns.end(), right_columns.begin(), right_columns.end());
  return columns;
}

} /* namespace optimizer */
} /* namespace peloton */
