#pragma once
#include <string>
#include <vector>

#include "../catalog/catalog.h"
#include "optimizer.h"

namespace minidb {

struct QueryResult {
  bool ok = true;
  std::string message;
  std::vector<std::string> column_names;
  std::vector<std::vector<std::string>> rows;  // stringified for display
  std::string explain;
};

// Ties parsing -> optimizer -> plan execution -> catalog/storage mutation
// together. This has no transaction awareness itself; TransactionContext
// (txn module) wraps calls into here with lock acquisition for DML.
class Executor {
 public:
  explicit Executor(Catalog *catalog) : catalog_(catalog), optimizer_(catalog) {}

  QueryResult Execute(const Statement &stmt);

 private:
  QueryResult ExecCreateTable(const CreateTableStmt &s);
  QueryResult ExecInsert(const InsertStmt &s);
  QueryResult ExecSelect(const SelectStmt &s);
  QueryResult ExecDelete(const DeleteStmt &s);

  Catalog *catalog_;
  Optimizer optimizer_;
};

}  // namespace minidb
