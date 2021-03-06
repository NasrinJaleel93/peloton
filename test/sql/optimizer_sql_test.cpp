//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// optimizer_sql_test.cpp
//
// Identification: test/sql/optimizer_sql_test.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "sql/testing_sql_util.h"
#include "catalog/catalog.h"
#include "common/harness.h"
#include "executor/create_executor.h"
#include "optimizer/optimizer.h"
#include "optimizer/simple_optimizer.h"
#include "planner/create_plan.h"
#include "planner/order_by_plan.h"

using std::vector;
using std::unordered_set;
using std::string;
using std::unique_ptr;
using std::shared_ptr;

namespace peloton {
namespace test {

class OptimizerSQLTests : public PelotonTest {
 protected:
  virtual void SetUp() override {
    // Call parent virtual function first
    PelotonTest::SetUp();

    // Create test database
    CreateAndLoadTable();
    optimizer.reset(new optimizer::Optimizer());
  }

  virtual void TearDown() override {
    // Destroy test database
    auto& txn_manager = concurrency::TransactionManagerFactory::GetInstance();
    auto txn = txn_manager.BeginTransaction();
    catalog::Catalog::GetInstance()->DropDatabaseWithName(DEFAULT_DB_NAME, txn);
    txn_manager.CommitTransaction(txn);

    // Call parent virtual function
    PelotonTest::TearDown();
  }

  /*** Helper functions **/
  void CreateAndLoadTable() {
    // Create database
    auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
    auto txn = txn_manager.BeginTransaction();
    catalog::Catalog::GetInstance()->CreateDatabase(DEFAULT_DB_NAME, txn);
    txn_manager.CommitTransaction(txn);

    // Create a table first
    TestingSQLUtil::ExecuteSQLQuery(
        "CREATE TABLE test(a INT PRIMARY KEY, b INT, c INT);");

    // Insert tuples into table
    TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (1, 22, 333);");
    TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (2, 11, 000);");
    TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (3, 33, 444);");
    TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (4, 00, 555);");
  }

  // If the query has OrderBy, the result is deterministic. Specify ordered to
  // be true. Otherwise, specify ordered to be false
  void TestUtil(string query, vector<string> ref_result, bool ordered,
                vector<PlanNodeType> expected_plans = {}) {
    LOG_DEBUG("Running Query \"%s\"", query.c_str());

    // Check Plan Nodes are correct if provided
    if (expected_plans.size() > 0) {
      auto plan = TestingSQLUtil::GeneratePlanWithOptimizer(optimizer, query);
      auto plan_ptr = plan.get();
      vector<PlanNodeType> actual_plans;
      while (true) {
        actual_plans.push_back(plan_ptr->GetPlanNodeType());
        if (plan_ptr->GetChildren().size() == 0) break;
        plan_ptr = plan_ptr->GetChildren()[0].get();
      }
      EXPECT_EQ(expected_plans, actual_plans);
    }

    // Check plan execution results are correct
    TestingSQLUtil::ExecuteSQLQueryWithOptimizer(optimizer, query, result,
                                                 tuple_descriptor, rows_changed,
                                                 error_message);
    vector<string> actual_result;
    for (unsigned i = 0; i < result.size(); i++)
      actual_result.push_back(
          TestingSQLUtil::GetResultValueAsString(result, i));

    EXPECT_EQ(ref_result.size(), result.size());
    if (ordered) {
      // If deterministic, do comparision with expected result in order
      EXPECT_EQ(ref_result, actual_result);
    } else {
      // If non-deterministic, make sure they have the same set of value
      unordered_set<string> ref_set(ref_result.begin(), ref_result.end());
      for (auto& result_str : actual_result) {
        if (ref_set.count(result_str) == 0) {
          // Test Failed. Print both actual results and ref results
          EXPECT_EQ(ref_result, actual_result);
          break;
        }
      }
    }
  }

 protected:
  unique_ptr<optimizer::AbstractOptimizer> optimizer;
  vector<StatementResult> result;
  vector<FieldInfo> tuple_descriptor;
  string error_message;
  int rows_changed;
};

TEST_F(OptimizerSQLTests, SimpleSelectTest) {
  // Testing select star expression
  TestUtil("SELECT * from test", {"333", "22", "1", "2", "11", "0", "3", "33",
                                  "444", "4", "0", "555"}, false);

  // Testing predicate
  TestUtil("SELECT c, b from test where a=1", {"333", "22"}, false);
}

TEST_F(OptimizerSQLTests, SelectOrderByTest) {
  // Testing order by columns different from select columns
  TestUtil("SELECT b from test order by c", {"11", "22", "33", "0"}, true);

  // Testing order by desc
  TestUtil("SELECT a from test order by c desc", {"4", "3", "1", "2"}, true);

  // Testing order by complex expression
  TestUtil(
      "SELECT * from test order by a + c",
      {"2", "11", "0", "1", "22", "333", "3", "33", "444", "4", "0", "555"},
      true);

  // Testing order by * expression
  TestUtil(
      "SELECT * from test order by a",
      {"1", "22", "333", "2", "11", "0", "3", "33", "444", "4", "0", "555"},
      true);
}

TEST_F(OptimizerSQLTests, SelectLimitTest) {
  // Test limit with default offset
  TestUtil("SELECT b FROM test ORDER BY b LIMIT 3", {"0", "11", "22"}, true);

  // Test limit with offset
  TestUtil("SELECT b FROM test ORDER BY b LIMIT 2 OFFSET 2", {"22", "33"},
           true);
}

TEST_F(OptimizerSQLTests, SelectProjectionTest) {
  // Test complex expression projection
  TestUtil("SELECT a * 5 + b, -1 + c from test",
           {"27", "332", "48", "443", "21", "-1", "20", "554"}, false);

  // Test complex expression in select and order by
  TestUtil("SELECT a * 5 + b - c FROM test ORDER BY a * 10 + b",
           {"21", "-306", "-535", "-396"}, true);

  // Test mixing up select simple columns with complex expression
  TestUtil("SELECT a, a + c FROM test ORDER BY a * 3 * b DESC, b + c / 5 ASC",
           {"3", "447", "2", "2", "1", "334", "4", "559"}, true);
}

TEST_F(OptimizerSQLTests, DeleteSqlTest) {
  // TODO: Test for index scan

  // Delete with predicates
  string query = "DELETE FROM test WHERE a = 1 and c = 333";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  EXPECT_EQ(1, rows_changed);
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(optimizer, "SELECT * FROM test",
                                               result, tuple_descriptor,
                                               rows_changed, error_message);
  EXPECT_EQ(9, result.size());

  // Delete with predicates
  query = "DELETE FROM test WHERE b = 33";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  EXPECT_EQ(1, rows_changed);
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(optimizer, "SELECT * FROM test",
                                               result, tuple_descriptor,
                                               rows_changed, error_message);
  EXPECT_EQ(6, result.size());

  // Delete with false predicates
  query = "DELETE FROM test WHERE b = 123";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  EXPECT_EQ(0, rows_changed);
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(optimizer, "SELECT * FROM test",
                                               result, tuple_descriptor,
                                               rows_changed, error_message);
  EXPECT_EQ(6, result.size());

  // Full deletion
  query = "DELETE FROM test";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  EXPECT_EQ(2, rows_changed);
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(optimizer, "SELECT * FROM test",
                                               result, tuple_descriptor,
                                               rows_changed, error_message);
  EXPECT_EQ(0, result.size());
}

TEST_F(OptimizerSQLTests, UpdateSqlTest) {
  // Test Update with complex expression and predicate
  string query = "UPDATE test SET c = b + 1 WHERE a = 1";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  EXPECT_EQ(1, rows_changed);
  TestUtil("SELECT c FROM test WHERE a=1", {"23"}, false);
}

TEST_F(OptimizerSQLTests, InsertSqlTest) {
  string query = "INSERT INTO test VALUES (5, 55, 555);";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  EXPECT_EQ(1, rows_changed);

  // Test the tuple is succesfully inserted
  TestUtil("SELECT * FROM test WHERE a=5", {"5", "55", "555"}, false);
}

TEST_F(OptimizerSQLTests, DDLSqlTest) {
  // Test creating new table
  string query = "CREATE TABLE test2(a INT PRIMARY KEY, b INT, c INT);";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);

  auto table = catalog::Catalog::GetInstance()->GetTableWithName(
      DEFAULT_DB_NAME, "test2");
  EXPECT_NE(nullptr, table);
  auto cols = table->GetSchema()->GetColumns();
  EXPECT_EQ(3, cols.size());
  EXPECT_EQ("a", cols[0].column_name);
  EXPECT_EQ(true, cols[0].is_primary_);
  EXPECT_EQ(type::Type::INTEGER, cols[0].GetType());
  EXPECT_EQ("b", cols[1].column_name);
  EXPECT_EQ(type::Type::INTEGER, cols[1].GetType());
  EXPECT_EQ("c", cols[2].column_name);
  EXPECT_EQ(type::Type::INTEGER, cols[2].GetType());

  // Test dropping existing table
  query = "DROP TABLE test2";
  TestingSQLUtil::ExecuteSQLQueryWithOptimizer(
      optimizer, query, result, tuple_descriptor, rows_changed, error_message);
  try {
    catalog::Catalog::GetInstance()->GetTableWithName(DEFAULT_DB_NAME, "test2");
    EXPECT_TRUE(false);
  } catch (Exception& e) {
    LOG_INFO("Correct! Exception(%s) catched", e.what());
  }
}

TEST_F(OptimizerSQLTests, GroupByTest) {
  // Insert additional tuples to test group by
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (1, 22, 333);");
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (2, 11, 000);");
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (3, 33, 444);");
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (4, 00, 555);");
  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (5, 11, 000);");
  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (6, 22, 333);");

  // Test basic case
  TestUtil("SELECT b FROM test GROUP BY b having b=11 or b=22", {"22", "11"},
           false);

  // Test Aggregate function: COUNT(*)
  TestUtil("SELECT COUNT(*) FROM test GROUP BY b", {"1", "1", "2", "2"}, false);

  // Test Aggregate function: COUNT(a)
  TestUtil("SELECT COUNT(a) FROM test GROUP BY b", {"1", "1", "2", "2"}, false);

  // Test basic case
  TestUtil("SELECT b FROM test GROUP BY b having b=11 or b=22", {"22", "11"},
           false);

  // Test Aggregate function: COUNT(*)
  TestUtil("SELECT COUNT(*) FROM test GROUP BY b", {"1", "1", "2", "2"}, false);

  // Test Aggregate function: COUNT(a)
  TestUtil("SELECT COUNT(a) FROM test GROUP BY b", {"1", "1", "2", "2"}, false);

  // Test group by with having
  TestUtil("SELECT AVG(a), b FROM test GROUP BY b having b=22", {"3.5", "22"},
           false);

  // Test group by combined with ORDER BY
  TestUtil("SELECT b FROM test GROUP BY b ORDER BY b", {"0", "11", "22", "33"},
           true);

  // Test complex expression in aggregation
  TestUtil("SELECT b, MAX(a + c) FROM test GROUP BY b ORDER BY b",
           {"0", "559", "11", "5", "22", "339", "33", "447"}, true);

  // Test complex expression in select list and order by complex expr
  TestUtil("SELECT b + c, SUM(c * a) FROM test GROUP BY b,c ORDER BY b + c",
           {"11", "0", "355", "2331", "477", "1332", "555", "2220"}, true);

  // Test Plain aggregation without group by
  TestUtil("SELECT SUM(c * a) FROM test", {"5883"}, false);

  // Test combining aggregation function
  TestUtil("SELECT SUM(c * a) + MAX(b - 1) * 2 FROM test", {"5947"}, false);

  // Test combining aggregation function with GroupBy
  TestUtil("SELECT MIN(b + c) * SUM(a - 2) FROM test GROUP BY b,c",
           {"1110", "477", "33", "1065"}, false);
  TestUtil("SELECT MIN(c) + b FROM test GROUP BY b",
           {"355", "11", "477", "555"}, false);
  TestUtil("SELECT MIN(b + c) * SUM(a - 2) + b * c FROM test GROUP BY b,c",
           {"1110", "15129", "33", "8391"}, false);

  // Test ORDER BY columns not shown in select list
  TestUtil("SELECT a FROM test GROUP BY a,b ORDER BY a + b",
           {"4", "2", "5", "1", "6", "3"}, true);

  // Test ORDER BY columns contains all group by columns
  // In case of SortGroupBy, no additional sort should be enforced after groupby
  TestUtil("SELECT a FROM test GROUP BY a,b ORDER BY b,a, a+b",
           {"4", "2", "5", "1", "6", "3"}, true);

  // Test ORDER BY columns are a subset of group by columns
  // In case of SortGroupBy, no additional sort should be enforced after groupby
  TestUtil("SELECT a + b FROM test GROUP BY a,b ORDER BY a",
           {"23", "13", "36", "4", "16", "28"}, true);
}

TEST_F(OptimizerSQLTests, SelectDistinctTest) {
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (1, 22, 333);");
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (2, 11, 000);");
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (3, 33, 444);");
  //  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (4, 00, 555);");
  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (5, 00, 555);");
  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (6, 22, 333);");

  // Test DISTINCT and GROUP BY have the same columns. Avoid additional HashPlan
  TestUtil("SELECT DISTINCT b,c FROM test GROUP BY b,c",
           {"0", "555", "33", "444", "11", "0", "22", "333"}, false);

  // Test GROUP BY cannot satisfied DISTINCT
  TestUtil("SELECT DISTINCT b FROM test GROUP BY b,c", {"22", "11", "0", "33"},
           false);

  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (7, 00, 444);");

  // Test distinct with order by
  TestUtil("SELECT DISTINCT b FROM test ORDER BY b", {"0", "11", "22", "33"},
           true);

  // Test distinct with complex order by
  TestUtil("SELECT DISTINCT b, c FROM test ORDER BY 10 * b + c",
           {"11", "0", "0", "444", "22", "333", "0", "555", "33", "444"}, true);

  // Test distinct with limit and star expression
  TestUtil("SELECT DISTINCT * FROM test ORDER BY a + 10 * b + c LIMIT 3",
           {"2", "11", "0", "7", "0", "444", "1", "22", "333"}, true);

  // Insert additional tuples to test distinct with group by
  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (5, 11, 000);");
  TestingSQLUtil::ExecuteSQLQuery("INSERT INTO test VALUES (6, 22, 333);");

  // Test distinct and group by complex expression
//  TestUtil("SELECT DISTINCT b + c FROM test GROUP BY b + c ORDER BY b + c",
//           {"11", "355", "444", "477", "555"}, true);
}

TEST_F(OptimizerSQLTests, SelectConstantTest) {
  // Test single constant
  TestUtil("SELECT 1", {"1"}, true);

  // Test complex arithmetic
  TestUtil("SELECT 1 + 2 * (6 / 4)", {"3"}, true);

  // Test multiple constant
  TestUtil("SELECT 18 / 4, 2 / 3 * 8 - 1", {"4", "-1"}, true);
  TestUtil("SELECT 18 % 4, 2 / 3 * 8 - 1", {"2", "-1"}, true);
  TestUtil("SELECT not 1>3, 1!=1, not 1=1", {"true", "false", "false"}, true);

  // Test combination of constant and column
  TestUtil("SELECT 1, 3 * 7, a from test",
           {"1", "21", "1", "1", "21", "2", "1", "21", "3", "1", "21", "4"}, true);

}

}  // namespace test
}  // namespace peloton
