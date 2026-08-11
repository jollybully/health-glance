#include <pebble.h>

int main(void) {
  Window *w = window_create();
  window_stack_push(w, true);

#ifdef PBL_DEBUG
  ModdableCreationRecord cr = {
    .recordSize = sizeof(cr),
    .flags = kModdableCreationFlagDebug,
  };
  moddable_createMachine(&cr);
#else
  /* Default XS heaps — custom sizes fought Poco for Emery's 128KB. */
  moddable_createMachine(NULL);
#endif

  window_destroy(w);
}
