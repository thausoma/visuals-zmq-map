#include <thread>
#include "Database.h"
#include "Network.h"
#include "UI.h"

int main(int argc, char *argv[]) {
    init_database();

    std::thread network_thread(zmq_server);
    network_thread.detach();

    ui_loop();
    return 0;
}