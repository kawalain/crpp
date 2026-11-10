#include "crpp/win/com_ptr.hpp"
#include "crpp/win/own.hpp"

#include <utility>

#include "check.hpp"

namespace {

struct Thing {
  int value = 0;
};
int freed = 0;
int free_thing(Thing* thing) {
  ++freed;
  thing->value = -1;
  return 1;  // as BOOL-returning frees do
}

struct Unknown {
  int refs = 1;
  unsigned long AddRef() { return static_cast<unsigned long>(++refs); }
  unsigned long Release() { return static_cast<unsigned long>(--refs); }
};

using OwnThing = crpp::win::Own<Thing*, free_thing>;

void own_frees_once_on_every_path() {
  Thing a, b;
  freed = 0;
  { const OwnThing owned(&a); }
  CHECK(freed == 1);
  CHECK(a.value == -1);
  {
    OwnThing first(&b);
    OwnThing second = std::move(first);
    CHECK(!first);
    CHECK(second.get() == &b);
  }
  CHECK(freed == 2);
  { const OwnThing none; }
  CHECK(freed == 2);  // null is never freed
  Thing c;
  OwnThing kept(&c);
  Thing* raw = kept.release();
  CHECK(raw == &c);
  CHECK(freed == 2);
}

void com_ptr_counts_references() {
  Unknown object;
  {
    crpp::win::ComPtr<Unknown> p;
    *p.put() = &object;  // as an out-parameter hands over a reference
    crpp::win::ComPtr<Unknown> copy = p;
    CHECK(object.refs == 2);
    crpp::win::ComPtr<Unknown> moved = std::move(copy);
    CHECK(object.refs == 2);
    CHECK(!copy);
  }
  CHECK(object.refs == 0);
}

}  // namespace

int main() {
  own_frees_once_on_every_path();
  com_ptr_counts_references();
  return crpp_test_result();
}
