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
#include "common/logger.h"

namespace peloton {
namespace test {

class UDFTests : public PelotonTest {};


// UDF Registering Tests

TEST_F(UDFTests, CUDFTest) {
  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  catalog::Catalog::GetInstance()->CreateDatabase(DEFAULT_DB_NAME, txn);
  txn_manager.CommitTransaction(txn);
  TestingSQLUtil::ExecuteSQLQuery("CREATE FUNCTION c_overpaid(integer, integer) RETURNS boolean AS 'DIRECTORY/funcs', 'c_overpaid' LANGUAGE C STRICT;");
}

TEST_F(UDFTests, PLPGSQLTest) {
  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  catalog::Catalog::GetInstance()->CreateDatabase(DEFAULT_DB_NAME, txn);
  txn_manager.CommitTransaction(txn);

  std::vector<StatementResult> result;
  std::vector<FieldInfo> tuple_descriptor;
  std::string error_message;
  int rows_affected;

  TestingSQLUtil::ExecuteSQLQuery("CREATE OR REPLACE FUNCTION increment(i integer) RETURNS integer AS $$ BEGIN RETURN i + 1; END; $$ LANGUAGE plpgsql;");

  auto status = TestingSQLUtil::ExecuteSQLQuery("SELECT * from pg_catalog.pg_proc", result,
                                  tuple_descriptor, rows_affected,
                                  error_message);

  //TestingSQLUtil::ShowTable("pg_catalog","pg_proc");
  LOG_DEBUG("Statement executed. Result: %s",
           ResultTypeToString(status).c_str());
 
}

TEST_F(UDFTests, PLPGSQLInvocationTest){
  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  catalog::Catalog::GetInstance()->CreateDatabase(DEFAULT_DB_NAME, txn);
  txn_manager.CommitTransaction(txn);
  
  std::vector<StatementResult> result;
  std::vector<FieldInfo> tuple_descriptor;
  std::string error_message;
  int rows_affected;

  TestingSQLUtil::ExecuteSQLQuery("SELECT increment(5);", result, tuple_descriptor,rows_affected, error_message);
  

}


}  // namespace test
}  // namespace peloton