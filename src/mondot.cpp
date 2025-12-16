#include <iostream>
#include "util.h"
#include "runtime/host.h"
#include "runtime/host_core_funcs.h"
#include "run_controller.h"

using namespace std;

int main(int argc, char **argv)
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    enable_terminal_colors();

    if(argc < 2)
    {
        cout << "Usage: mondot <scripts-dir> [--test|--benchmark|--production]";
        return 1;
    }

    mondot_host::register_core_host_functions(GLOBAL_HOST);
    mondot_host::register_extra_host_functions(GLOBAL_HOST);

    VM vm(GLOBAL_HOST);
    string scripts_dir = argv[1];

    RunController controller(vm, scripts_dir, argc, argv);
    return controller.run();
}
