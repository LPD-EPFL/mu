#include "hrd.hpp"

#include <cstdlib>

static const size_t nodes = 4;
static const size_t ib_port_index = 0;
static constexpr size_t kAppBufSize = (8 * 1024 * 1024);

/*
 * A dummy main to check if the hrd library files compile
 */
int main(int argc, char *argv[]) {

    rt_assert(argc == 3);

    const size_t local_gid = atoi(argv[1]);
    const size_t remote_gid = atoi(argv[2]);

    struct hrd_conn_config_t conn_config;
    conn_config.num_qps = nodes;
    conn_config.use_uc = 0;
    conn_config.prealloc_buf = nullptr;
    conn_config.buf_size = kAppBufSize;
    conn_config.buf_shm_key = -1;

    auto* cb = hrd_ctrl_blk_init(local_gid, ib_port_index, kHrdInvalidNUMANode,
    &conn_config, nullptr);

    char srv_name[kHrdQPNameSize];
    sprintf(srv_name, "server-%zu", local_gid);
    char clt_name[kHrdQPNameSize];
    sprintf(clt_name, "server-%zu", remote_gid);

    hrd_publish_conn_qp(cb, 0, srv_name);
    printf("main: Server %s published. Waiting for server %s\n", srv_name,
    clt_name);

    hrd_qp_attr_t* clt_qp = nullptr;
    while (clt_qp == nullptr) {
        clt_qp = hrd_get_published_qp(clt_name);
        if (clt_qp == nullptr)
            usleep(200000);
    }

    printf("main: Server %s found server! Connecting..\n", srv_name);
    hrd_connect_qp(cb, 0, clt_qp);

    // This garbles the server's qp_attr - which is safe
    hrd_publish_ready(srv_name);
    printf("main: Server %s READY\n", srv_name);

    while (true) {
        printf("main: Server %s: %d\n", srv_name, cb->conn_buf[0]);
        sleep(1);
    }


    return 0;
}
