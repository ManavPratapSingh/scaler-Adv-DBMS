#include "executor.h"

#include <stdexcept>

namespace minidb {

QueryResult Executor::Execute(const Statement &stmt) {
  try {
    switch (stmt.kind) {
      case StmtKind::CREATE_TABLE: return ExecCreateTable(stmt.create_table);
      case StmtKind::INSERT: return ExecInsert(stmt.insert);
      case StmtKind::SELECT: return ExecSelect(stmt.select);
      case StmtKind::DELETE: return ExecDelete(stmt.del);
    }
  } catch (const std::exception &e) {
    QueryResult r;
    r.ok = false;
    r.message = e.what();
    return r;
  }
  QueryResult r;
  r.ok = false;
  r.message = "unreachable";
  return r;
}

QueryResult Executor::ExecCreateTable(const CreateTableStmt &s) {
  QueryResult r;
  if (catalog_->TableExists(s.table_name)) {
    r.ok = false;
    r.message = "table already exists: " + s.table_name;
    return r;
  }
  catalog_->CreateTable(s.table_name, s.columns);
  r.message = "CREATE TABLE " + s.table_name;
  return r;
}

QueryResult Executor::ExecInsert(const InsertStmt &s) {
  QueryResult r;
  TableInfo *t = catalog_->GetTable(s.table_name);
  if (!t) { r.ok = false; r.message = "no such table: " + s.table_name; return r; }
  if (s.values.size() != t->schema.NumColumns()) {
    r.ok = false;
    r.message = "expected " + std::to_string(t->schema.NumColumns()) + " values, got " +
                std::to_string(s.values.size());
    return r;
  }
  Tuple tup(s.values);
  RID rid = t->heap->InsertTuple(tup.Serialize());

  int pk_idx = t->schema.PrimaryKeyIndex();
  if (pk_idx >= 0 && t->index) t->index->Insert(s.values[pk_idx].AsInt(), rid);
  catalog_->NotifyRowInserted(s.table_name);

  r.message = "INSERT 1 row into " + s.table_name;
  return r;
}

QueryResult Executor::ExecSelect(const SelectStmt &s) {
  QueryResult r;
  std::string explain;
  auto plan = optimizer_.PlanSelect(s, &explain);
  r.explain = explain;

  for (size_t i = 0; i < plan->OutputSchema().Size(); i++)
    r.column_names.push_back(plan->OutputSchema().UnqualifiedName(i));

  plan->Open();
  while (auto row = plan->Next()) {
    std::vector<std::string> out;
    for (auto &v : row->values) out.push_back(v.ToString());
    r.rows.push_back(out);
  }
  plan->Close();
  r.message = std::to_string(r.rows.size()) + " row(s)";
  return r;
}

QueryResult Executor::ExecDelete(const DeleteStmt &s) {
  QueryResult r;
  TableInfo *t = catalog_->GetTable(s.table_name);
  if (!t) { r.ok = false; r.message = "no such table: " + s.table_name; return r; }

  std::vector<bool> consumed(s.where.size(), false);
  std::string explain;
  auto plan = optimizer_.PlanBaseRelation(t, s.table_name, s.where, &consumed, &explain);
  std::vector<ResolvedPredicate> resolved;
  for (size_t i = 0; i < s.where.size(); i++)
    if (!consumed[i]) {
      auto &c = s.where[i];
      ResolvedPredicate rp;
      rp.left_idx = plan->OutputSchema().Resolve(c.left.column.table, c.left.column.column);
      rp.op = c.op;
      if (c.right.is_column) {
        rp.right_is_col = true;
        rp.right_idx = plan->OutputSchema().Resolve(c.right.column.table, c.right.column.column);
      } else {
        rp.right_is_col = false;
        rp.right_literal = c.right.literal;
      }
      resolved.push_back(rp);
    }

  int pk_idx = t->schema.PrimaryKeyIndex();
  int deleted = 0;
  plan->Open();
  while (auto row = plan->Next()) {
    bool ok = true;
    for (auto &p : resolved) if (!EvalPredicate(p, *row)) { ok = false; break; }
    if (!ok) continue;
    t->heap->DeleteTuple(row->rid);
    if (pk_idx >= 0 && t->index) t->index->Remove(row->values[pk_idx].AsInt());
    catalog_->NotifyRowDeleted(s.table_name);
    deleted++;
  }
  plan->Close();
  r.explain = explain;
  r.message = "DELETE " + std::to_string(deleted) + " row(s)";
  return r;
}

}  // namespace minidb
