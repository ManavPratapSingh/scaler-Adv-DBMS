#include <cassert>
#include <cstdio>
#include <iostream>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

QueryResult Run(Executor &ex, const std::string &sql) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  Statement stmt = parser.ParseStatement();
  return ex.Execute(stmt);
}

int main() {
  std::remove("test_query.db");
  std::remove("test_query.db.wal");
  std::remove("test_query.meta");

  DiskManager dm("test_query.db");
  BufferPool bpm(64, &dm);
  Catalog cat(&bpm, "test_query.meta");
  Executor ex(&cat);

  assert(Run(ex, "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)").ok);
  assert(Run(ex, "CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, amount INT)").ok);

  for (int i = 1; i <= 10; i++) {
    Run(ex, "INSERT INTO users VALUES (" + std::to_string(i) + ", 'user" + std::to_string(i) + "')");
  }
  for (int i = 1; i <= 5; i++) {
    Run(ex, "INSERT INTO orders VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ", " +
                std::to_string(i * 100) + ")");
  }

  // Equality on PK should use IndexScan.
  {
    auto r = Run(ex, "SELECT id, name FROM users WHERE id = 5");
    assert(r.ok);
    assert(r.rows.size() == 1);
    assert(r.rows[0][1] == "user5");
    assert(r.explain.find("IndexScan") != std::string::npos);
  }

  // No predicate -> SeqScan.
  {
    auto r = Run(ex, "SELECT * FROM users");
    assert(r.ok);
    assert(r.rows.size() == 10);
    assert(r.explain.find("SeqScan") != std::string::npos);
  }

  // Join on orders.user_id = users.id (users.id is PK) -> index nested loop join.
  {
    auto r = Run(ex, "SELECT o.id, u.name, o.amount FROM orders o JOIN users u ON o.user_id = u.id WHERE o.amount > 200");
    assert(r.ok);
    assert(r.rows.size() == 3);  // amounts 300,400,500
    assert(r.explain.find("IndexNestedLoopJoin") != std::string::npos);
  }

  // DELETE via index path.
  {
    auto r = Run(ex, "DELETE FROM users WHERE id = 3");
    assert(r.ok);
    auto check = Run(ex, "SELECT * FROM users WHERE id = 3");
    assert(check.rows.empty());
  }

  std::remove("test_query.db");
  std::remove("test_query.db.wal");
  std::remove("test_query.meta");
  std::cout << "query_test PASSED\n";
  return 0;
}
