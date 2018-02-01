#include "cado.h"
#include "bwc_config.h"
#include "matmul_facade.h"

void MATMUL_NAME(rebind)(matmul_ptr mm)
{
    REBIND_ALL(mm);
}

#ifdef  BUILD_DYNAMICALLY_LINKABLE_BWC
void matmul_solib_do_rebinding(matmul_ptr mm)
{
    REBIND_ALL(mm);
}
#endif

