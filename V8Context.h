#ifndef _V8Context_h_
#define _V8Context_h_

#include <EXTERN.h>
#include <perl.h>
#undef New
#undef Null
#include <v8.h>

class V8Context {
public:
  v8::Persistent<v8::Context> context;

  void register_method_by_name(const char* code);

  V8Context() {
        context = v8::Context::New();
  }
  SV* eval(const char* code);

private:
};

#endif

