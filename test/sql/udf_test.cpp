//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// order_by_sql_test.cpp
//
// Identification: test/sql/order_by_sql_test.cpp
//
// Copyright (c) 2015-17, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "sql/testing_sql_util.h"
#include "catalog/catalog.h"
#include "common/harness.h"
#include "executor/create_executor.h"
#include "optimizer/simple_optimizer.h"
#include "planner/create_plan.h"

namespace peloton {
namespace test {

class UDFTests : public PelotonTest {};

TEST_F(UDFTests, ExecuteTest) {
  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  catalog::Catalog::GetInstance()->CreateDatabase(DEFAULT_DB_NAME, txn);
  txn_manager.CommitTransaction(txn);
  TestingSQLUtil::ExecuteSQLQuery("CREATE FUNCTION c_overpaid(integer, integer) RETURNS boolean AS 'DIRECTORY/funcs', 'c_overpaid' LANGUAGE C STRICT;");
}

}  // namespace test
}  // namespace peloton