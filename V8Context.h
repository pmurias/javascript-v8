#ifndef _V8Context_h_
#define _V8Context_h_

#include <string>

class V8Context {
public:
  V8Context() {}
  ~V8Context() {}

  void eval(const char* code);

private:
};

#endif

