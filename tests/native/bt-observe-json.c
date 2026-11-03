/* SPDX-License-Identifier: GPL-2.0-only */
#define main observer_production_main
#include "../../tools/connectivity/bt-observe.c"
#undef main
#include <assert.h>
int main(void)
{
 GError *error=NULL;
 GVariant *value=g_variant_parse(G_VARIANT_TYPE_VARDICT,
  "{'ManufacturerData': <{uint16 117: <[byte 0, 1, 76]>}>, 'Counter': <uint64 9223372036854775813>, 'Signed': <int64 -9223372036854775807>, 'Finite': <1.5>, 'Nonfinite': <inf>, 'Alias': <'escaped \"name\"'>}",NULL,NULL,&error);
 assert(value && !error);out=g_string_new(NULL);json(value,0);puts(out->str);
 g_string_free(out,TRUE);g_variant_unref(value);return 0;
}
