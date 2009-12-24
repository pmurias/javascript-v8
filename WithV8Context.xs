#ifdef __cplusplus
extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}
#endif

/* include your class headers here */
#include "V8Context.h"

/* We need one MODULE... line to start the actual XS section of the file.
 * The XS++ preprocessor will output its own MODULE and PACKAGE lines */
MODULE = Javascript::V8		PACKAGE = Javascript::V8

## The include line executes xspp with the supplied typemap and the
## xsp interface code for our class.
## It will include the output of the xsubplusplus run.

INCLUDE: xspp --typemap=typemap.xsp Javascript-V8-Context.xsp |


