/* CRIG firmware entry: one image per unit, selected by Kconfig (build.sh node|remote). */
#include "sdkconfig.h"

void crig_node_main(void);
void crig_remote_main(void);

void app_main(void)
{
#if CONFIG_CRIG_UNIT_NODE
    crig_node_main();
#else
    crig_remote_main();
#endif
}
