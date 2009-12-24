#ifndef _V8Context_h_
#define _V8Context_h_

#include <string>
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"

class V8Context {
public:
  V8Context() {}
  ~V8Context() {}

  SV* eval(const char* code);

private:
};

#endif

