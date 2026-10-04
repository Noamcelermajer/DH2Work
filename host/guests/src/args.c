// args.c -- proves the initial stack image: argc/argv are read from the guest's own stack.
#include "guest_syscalls.h"

void dh2_main(const int* stack) {
    const int argc = stack[0];
    char* const* argv = (char* const*)(stack + 1);
    // argv[0] is the runner's own path, so it is deliberately not printed: these outputs have
    // to be identical on every machine.
    dh2_write("argc=");
    dh2_write_int(argc);
    dh2_write(" argv1=");
    dh2_write(argc > 1 && argv[1] != 0 ? argv[1] : "(none)");
    dh2_write(" argv2=");
    dh2_write(argc > 2 && argv[2] != 0 ? argv[2] : "(none)");
    dh2_write("\n");
    dh2_exit(0);
}
