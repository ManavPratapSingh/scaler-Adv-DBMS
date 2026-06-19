#pragma once
#include <functional>
#include <string>
#include <unordered_map>

#include "buffer_pool.h"
#include "heap_page.h"
#include "rid.h"

namespace minidb {

// A heap file is a singly-linked-by-page-id sequence of slotted pages
// belonging to one table. Insert always tries the last known page first,
// allocating a fresh one via the buffer pool when it's full.
class HeapFile {
 public:
  HeapFile(BufferPool *bpm, page_id_t first_page_id) : bpm_(bpm), first_page_id_(first_page_id) {}

  page_id_t FirstPageId() const { return first_page_id_; }

  RID InsertTuple(const std::string &data) {
    page_id_t pid = first_page_id_;
    page_id_t last_pid = pid;
    while (pid != INVALID_PAGE_ID) {
      Page *page = bpm_->FetchPage(pid);
      HeapPage hp(page->GetData());
      int slot = hp.InsertTuple(data.data(), static_cast<int32_t>(data.size()));
      if (slot >= 0) {
        bpm_->UnpinPage(pid, true);
        return RID{pid, slot};
      }
      bpm_->UnpinPage(pid, false);
      last_pid = pid;
      pid = NextPageId(last_pid);
    }
    // No room anywhere - allocate a new page and link it.
    page_id_t new_pid;
    Page *new_page = bpm_->NewPage(&new_pid);
    HeapPage new_hp(new_page->GetData());
    new_hp.Init();
    int slot = new_hp.InsertTuple(data.data(), static_cast<int32_t>(data.size()));
    bpm_->UnpinPage(new_pid, true);
    LinkPage(last_pid, new_pid);
    return RID{new_pid, slot};
  }

  bool GetTuple(RID rid, std::string *out) {
    Page *page = bpm_->FetchPage(rid.page_id);
    HeapPage hp(page->GetData());
    bool ok = hp.GetTuple(rid.slot_num, out);
    bpm_->UnpinPage(rid.page_id, false);
    return ok;
  }

  bool DeleteTuple(RID rid) {
    Page *page = bpm_->FetchPage(rid.page_id);
    HeapPage hp(page->GetData());
    bool ok = hp.DeleteTuple(rid.slot_num);
    bpm_->UnpinPage(rid.page_id, true);
    return ok;
  }

  // Calls fn(rid, tuple_bytes) for every live tuple in the file (full scan).
  void Scan(const std::function<void(RID, const std::string &)> &fn) {
    page_id_t pid = first_page_id_;
    while (pid != INVALID_PAGE_ID) {
      Page *page = bpm_->FetchPage(pid);
      HeapPage hp(page->GetData());
      int n = hp.NumTupleSlots();
      for (int i = 0; i < n; i++) {
        std::string tuple;
        if (hp.GetTuple(i, &tuple)) fn(RID{pid, i}, tuple);
      }
      bpm_->UnpinPage(pid, false);
      pid = NextPageId(pid);
    }
  }

 private:
  // Next-page links are stored in a side map for simplicity (kept here
  // rather than reading it off a footer in the page itself).
  page_id_t NextPageId(page_id_t pid) {
    auto it = links_.find(pid);
    return it == links_.end() ? INVALID_PAGE_ID : it->second;
  }
  void LinkPage(page_id_t from, page_id_t to) { links_[from] = to; }

  BufferPool *bpm_;
  page_id_t first_page_id_;
  std::unordered_map<page_id_t, page_id_t> links_;
};

}  // namespace minidb
