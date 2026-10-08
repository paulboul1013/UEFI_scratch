#include "runtime.hpp"


using RuntimeFunction =
    void (*)();


// ============================================================
// Linker-provided constructor / finalizer arrays
// ============================================================

extern "C" {

extern RuntimeFunction __init_array_start[];
extern RuntimeFunction __init_array_end[];


}


// ============================================================
// Minimal atexit support
// ============================================================

static constexpr unsigned long long
    MAX_ATEXIT_FUNCTIONS = 32;


static RuntimeFunction
    g_atexit_functions[MAX_ATEXIT_FUNCTIONS];


static unsigned long long
    g_atexit_count = 0;


// ------------------------------------------------------------
// Register a function to execute during shutdown
// ------------------------------------------------------------

extern "C"
int atexit(
    RuntimeFunction function
)
{
    if (
        g_atexit_count
        >= MAX_ATEXIT_FUNCTIONS
    ) {
        return -1;
    }


    g_atexit_functions[
        g_atexit_count
    ] = function;


    ++g_atexit_count;


    return 0;
}


// ============================================================
// Runtime initialization
// ============================================================

void runtime_init()
{
    RuntimeFunction *fn =
        __init_array_start;


    while (
        fn < __init_array_end
    ) {

        if (*fn != nullptr) {
            (*fn)();
        }


        ++fn;
    }
}


// ============================================================
// Runtime finalization
// ============================================================

void runtime_fini()
{
    while (g_atexit_count > 0) {

        // Pop before calling so callbacks can register more cleanup.
        RuntimeFunction function =
            g_atexit_functions[--g_atexit_count];


        if (function != nullptr) {
            function();
        }
    }
}
