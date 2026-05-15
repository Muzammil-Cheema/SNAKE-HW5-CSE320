#include <criterion/criterion.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "game_board.h"
#include "global.h"
#include "protocol.h"
#include "server.h"

extern int _client_fd_from_snake_id(server_t *server, int snake_id);

void init_server_slots(server_t *server) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        server->client_fds[i] = -1;
        server->client_snake_ids[i] = -1;
    }
}

void close_if_open(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

Test(server_suite, init_rejects_invalid_inputs, .timeout = 4) {
    cr_assert_eq(server_init(NULL, 0, 20, 4, 42), -1);
}

Test(server_suite, init_sets_expected_server_state, .timeout = 4) {
    server_t server = {0};

    cr_assert_eq(server_init(&server, 0, 20, 4, 42), 0);
    cr_assert_geq(server.listen_fd, 0);
    cr_assert_not_null(server.board.cells);
    cr_assert_eq(server.board.size, 20);
    cr_assert_eq(server.board.max_snakes, 4);
    cr_assert_eq(server.running, 1);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        cr_assert_eq(server.client_fds[i], -1);
        cr_assert_eq(server.client_snake_ids[i], -1);
    }

    cr_assert_eq(pthread_mutex_lock(&server.board_mutex), 0);
    cr_assert_eq(pthread_mutex_unlock(&server.board_mutex), 0);

    server_cleanup(&server);
}

Test(server_suite, _client_fd_from_snake_id_rejects_invalid_inputs, .timeout = 4) {
    server_t server = {0};
    init_server_slots(&server);

    cr_assert_eq(_client_fd_from_snake_id(NULL, 0), -1);
    cr_assert_eq(_client_fd_from_snake_id(&server, -1), -1);
    cr_assert_eq(_client_fd_from_snake_id(&server, MAX_PLAYERS), -1);
    cr_assert_eq(_client_fd_from_snake_id(&server, 0), -1);
}

Test(server_suite, _client_fd_from_snake_id_finds_arbitrary_client_slot, .timeout = 4) {
    server_t server = {0};
    init_server_slots(&server);

    server.client_fds[3] = 77;
    server.client_snake_ids[3] = 1;
    server.client_fds[6] = 88;
    server.client_snake_ids[6] = 4;

    cr_assert_eq(_client_fd_from_snake_id(&server, 1), 77);
    cr_assert_eq(_client_fd_from_snake_id(&server, 4), 88);
    cr_assert_eq(_client_fd_from_snake_id(&server, 2), -1);
}

Test(server_suite, cleanup_accepts_null_server, .timeout = 4) {
    server_cleanup(NULL);
}

Test(server_suite, cleanup_closes_fds_and_frees_board_state, .timeout = 4) {
    server_t server = {0};
    int pair[2] = {-1, -1};

    cr_assert_eq(server_init(&server, 0, 20, 4, 42), 0);
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);

    server.client_fds[0] = pair[0];
    server.client_snake_ids[0] = 2;
    server.client_fds[1] = pair[1];
    server.client_snake_ids[1] = 3;

    server_cleanup(&server);

    cr_assert_eq(server.running, 0);
    cr_assert_eq(server.listen_fd, -1);
    cr_assert_eq(server.client_fds[0], -1);
    cr_assert_eq(server.client_fds[1], -1);
    cr_assert_null(server.board.cells);

    pair[0] = -1;
    pair[1] = -1;
}

Test(server_suite, start_rejects_null_server, .timeout = 4) {
    cr_assert_eq(server_start(NULL), -1);
}

Test(server_suite, start_returns_success_for_initialized_server, .timeout = 4) {
    server_t server = {0};

    cr_assert_eq(server_init(&server, 0, 20, 4, 42), 0);
    server.running = 0;
    cr_assert_eq(server_start(&server), 0);

    server_cleanup(&server);
}

Test(server_suite, game_loop_rejects_null_argument, .timeout = 4) {
    cr_assert_null(server_game_loop(NULL));
}

Test(server_suite, game_loop_exits_when_server_not_running, .timeout = 4) {
    server_t server = {0};

    cr_assert_eq(server_init(&server, 0, 20, 4, 42), 0);
    server.running = 0;

    cr_assert_null(server_game_loop(&server));

    server_cleanup(&server);
}

Test(server_suite, recv_exact_rejects_invalid_inputs, .timeout = 4) {
    uint8_t buf[2] = {0};

    cr_assert_eq(recv_exact(-1, buf, sizeof(buf)), -1);
    cr_assert_eq(recv_exact(0, NULL, sizeof(buf)), -1);
    cr_assert_eq(recv_exact(0, buf, 0), -1);
}

Test(server_suite, recv_exact_reads_full_message, .timeout = 4) {
    int pair[2] = {-1, -1};
    uint8_t input[5] = {1, 2, 3, 4, 5};
    uint8_t output[5] = {0};

    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    cr_assert_eq(send(pair[0], input, sizeof(input), 0), (ssize_t)sizeof(input));

    cr_assert_eq(recv_exact(pair[1], output, sizeof(output)), 0);
    cr_assert_eq(memcmp(input, output, sizeof(input)), 0);

    close_if_open(&pair[0]);
    close_if_open(&pair[1]);
}

Test(server_suite, recv_exact_reports_disconnect, .timeout = 4) {
    int pair[2] = {-1, -1};
    uint8_t output[2] = {0};

    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    close_if_open(&pair[0]);

    cr_assert_eq(recv_exact(pair[1], output, sizeof(output)), -1);

    close_if_open(&pair[1]);
}

Test(server_suite, send_all_rejects_invalid_inputs, .timeout = 4) {
    uint8_t buf[2] = {0};

    cr_assert_eq(send_all(-1, buf, sizeof(buf)), -1);
    cr_assert_eq(send_all(0, NULL, sizeof(buf)), -1);
    cr_assert_eq(send_all(0, buf, 0), -1);
}

Test(server_suite, send_all_writes_full_message, .timeout = 4) {
    int pair[2] = {-1, -1};
    uint8_t input[5] = {9, 8, 7, 6, 5};
    uint8_t output[5] = {0};

    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);

    cr_assert_eq(send_all(pair[0], input, sizeof(input)), 0);
    cr_assert_eq(recv(pair[1], output, sizeof(output), 0), (ssize_t)sizeof(output));
    cr_assert_eq(memcmp(input, output, sizeof(input)), 0);

    close_if_open(&pair[0]);
    close_if_open(&pair[1]);
}

Test(server_suite, send_all_reports_invalid_closed_fd, .timeout = 4) {
    int pair[2] = {-1, -1};
    uint8_t input[2] = {1, 2};
    int closed_fd = -1;

    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    closed_fd = pair[0];
    close(pair[0]);
    pair[0] = -1;

    cr_assert_eq(send_all(closed_fd, input, sizeof(input)), -1);

    close_if_open(&pair[1]);
}

typedef struct {
    server_t *server;
    int ret;
} server_start_thread_arg_t;

void *run_server_start_for_test(void *arg) {
    server_start_thread_arg_t *start_arg = arg;

    start_arg->ret = server_start(start_arg->server);
    return NULL;
}

int bound_port(server_t *server) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    memset(&addr, 0, sizeof(addr));
    cr_assert_eq(getsockname(server->listen_fd, (struct sockaddr *)&addr, &addr_len), 0);
    return ntohs(addr.sin_port);
}

void set_socket_timeout(int fd) {
    struct timeval timeout = {
        .tv_sec = 3,
        .tv_usec = 0,
    };

    cr_assert_eq(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);
    cr_assert_eq(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)), 0);
}

int connect_test_client(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;

    cr_assert_geq(fd, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    cr_assert_eq(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);
    cr_assert_eq(connect(fd, (struct sockaddr *)&addr, sizeof(addr)), 0);
    set_socket_timeout(fd);

    return fd;
}

void start_server_for_integration_test(server_t *server, pthread_t *tid,
                                       server_start_thread_arg_t *start_arg) {
    start_arg->server = server;
    start_arg->ret = -999;

    cr_assert_eq(pthread_create(tid, NULL, run_server_start_for_test, start_arg), 0);
}

void stop_server_for_integration_test(server_t *server, pthread_t tid,
                                      server_start_thread_arg_t *start_arg,
                                      int port) {
    int unblock_fd = -1;

    pthread_mutex_lock(&server->board_mutex);
    server->running = 0;
    pthread_mutex_unlock(&server->board_mutex);

    unblock_fd = connect_test_client(port);
    close_if_open(&unblock_fd);

    cr_assert_eq(pthread_join(tid, NULL), 0);
    cr_assert_eq(start_arg->ret, 0);
}

void client_send_join(int fd) {
    uint8_t join_msg[CLIENT_MSG_SIZE] = {MSG_JOIN, 0x00};

    cr_assert_eq(send_all(fd, join_msg, sizeof(join_msg)), 0);
}

void client_send_leave(int fd) {
    uint8_t leave_msg[CLIENT_MSG_SIZE] = {MSG_LEAVE, 0x00};

    cr_assert_eq(send_all(fd, leave_msg, sizeof(leave_msg)), 0);
}

void client_send_direction(int fd, uint8_t direction) {
    uint8_t direction_msg[CLIENT_MSG_SIZE] = {MSG_DIRECTION, direction};

    cr_assert_eq(send_all(fd, direction_msg, sizeof(direction_msg)), 0);
}

void client_expect_welcome(int fd, uint8_t expected_player_id,
                           uint8_t expected_board_size,
                           uint8_t expected_max_players) {
    uint8_t welcome[4] = {0};

    cr_assert_eq(recv_exact(fd, welcome, sizeof(welcome)), 0);
    cr_assert_eq(welcome[0], MSG_WELCOME);
    cr_assert_eq(welcome[1], expected_player_id);
    cr_assert_eq(welcome[2], expected_board_size);
    cr_assert_eq(welcome[3], expected_max_players);
}

void client_expect_error(int fd, uint8_t expected_error) {
    uint8_t error_msg[2] = {0};

    cr_assert_eq(recv_exact(fd, error_msg, sizeof(error_msg)), 0);
    cr_assert_eq(error_msg[0], MSG_ERROR);
    cr_assert_eq(error_msg[1], expected_error);
}

void client_expect_game_state_header(int fd, uint8_t expected_alive_snakes) {
    uint8_t header[6] = {0};
    uint16_t apple_x = 0;
    uint16_t apple_y = 0;

    cr_assert_eq(recv_exact(fd, header, sizeof(header)), 0);
    cr_assert_eq(header[0], MSG_GAME_STATE);
    cr_assert_eq(header[1], expected_alive_snakes);

    memcpy(&apple_x, &header[2], sizeof(apple_x));
    memcpy(&apple_y, &header[4], sizeof(apple_y));
    apple_x = ntohs(apple_x);
    apple_y = ntohs(apple_y);

    cr_assert_gt(apple_x, 0);
    cr_assert_gt(apple_y, 0);
}

void client_discard_snake_records(int fd, int alive_snakes) {
    for (int i = 0; i < alive_snakes; i++) {
        uint8_t snake_header[4] = {0};
        uint16_t snake_len = 0;
        uint8_t body_buf[MAX_SNAKE_LENGTH * 4] = {0};

        cr_assert_eq(recv_exact(fd, snake_header, sizeof(snake_header)), 0);
        memcpy(&snake_len, &snake_header[1], sizeof(snake_len));
        snake_len = ntohs(snake_len);

        cr_assert_gt(snake_len, 0);
        cr_assert_leq(snake_len, MAX_SNAKE_LENGTH);
        cr_assert_eq(recv_exact(fd, body_buf, snake_len * 4), 0);
    }
}

void client_expect_game_state(int fd, uint8_t expected_alive_snakes) {
    client_expect_game_state_header(fd, expected_alive_snakes);
    client_discard_snake_records(fd, expected_alive_snakes);
}

Test(server_integration_suite, start_accepts_join_and_sends_welcome, .timeout = 8) {
    server_t server = {0};
    pthread_t tid;
    server_start_thread_arg_t start_arg;
    int port = 0;
    int client_fd = -1;

    cr_assert_eq(server_init(&server, 0, 20, 3, 42), 0);
    port = bound_port(&server);
    start_server_for_integration_test(&server, &tid, &start_arg);

    client_fd = connect_test_client(port);
    client_send_join(client_fd);
    client_expect_welcome(client_fd, 0, 20, 3);

    client_send_leave(client_fd);
    close_if_open(&client_fd);
    stop_server_for_integration_test(&server, tid, &start_arg, port);
    server_cleanup(&server);
}

Test(server_integration_suite, rejects_non_join_as_first_client_message, .timeout = 8) {
    server_t server = {0};
    pthread_t tid;
    server_start_thread_arg_t start_arg;
    uint8_t direction_msg[CLIENT_MSG_SIZE] = {MSG_DIRECTION, DIR_RIGHT};
    int port = 0;
    int client_fd = -1;

    cr_assert_eq(server_init(&server, 0, 20, 3, 42), 0);
    port = bound_port(&server);
    start_server_for_integration_test(&server, &tid, &start_arg);

    client_fd = connect_test_client(port);
    cr_assert_eq(send_all(client_fd, direction_msg, sizeof(direction_msg)), 0);
    client_expect_error(client_fd, ERR_INVALID_MSG);

    close_if_open(&client_fd);
    stop_server_for_integration_test(&server, tid, &start_arg, port);
    server_cleanup(&server);
}

Test(server_integration_suite, rejects_extra_client_when_game_is_full, .timeout = 8) {
    server_t server = {0};
    pthread_t tid;
    server_start_thread_arg_t start_arg;
    int port = 0;
    int client0_fd = -1;
    int client1_fd = -1;

    cr_assert_eq(server_init(&server, 0, 20, 1, 42), 0);
    port = bound_port(&server);
    start_server_for_integration_test(&server, &tid, &start_arg);

    client0_fd = connect_test_client(port);
    client_send_join(client0_fd);
    client_expect_welcome(client0_fd, 0, 20, 1);

    client1_fd = connect_test_client(port);
    client_send_join(client1_fd);
    client_expect_error(client1_fd, ERR_GAME_FULL);

    client_send_leave(client0_fd);
    close_if_open(&client0_fd);
    close_if_open(&client1_fd);
    stop_server_for_integration_test(&server, tid, &start_arg, port);
    server_cleanup(&server);
}

Test(server_integration_suite, multiple_clients_receive_welcome_and_game_state, .timeout = 10) {
    server_t server = {0};
    pthread_t tid;
    server_start_thread_arg_t start_arg;
    int port = 0;
    int client0_fd = -1;
    int client1_fd = -1;

    cr_assert_eq(server_init(&server, 0, 20, 3, 42), 0);
    port = bound_port(&server);
    start_server_for_integration_test(&server, &tid, &start_arg);

    client0_fd = connect_test_client(port);
    client_send_join(client0_fd);
    client_expect_welcome(client0_fd, 0, 20, 3);

    client1_fd = connect_test_client(port);
    client_send_join(client1_fd);
    client_expect_welcome(client1_fd, 1, 20, 3);

    client_send_direction(client0_fd, DIR_RIGHT);
    client_send_direction(client1_fd, DIR_DOWN);

    client_expect_game_state(client0_fd, 2);
    client_expect_game_state(client1_fd, 2);

    client_send_leave(client0_fd);
    client_send_leave(client1_fd);
    close_if_open(&client0_fd);
    close_if_open(&client1_fd);
    stop_server_for_integration_test(&server, tid, &start_arg, port);
    server_cleanup(&server);
}

Test(server_integration_suite, leave_message_removes_client_from_server_state, .timeout = 8) {
    server_t server = {0};
    pthread_t tid;
    server_start_thread_arg_t start_arg;
    int port = 0;
    int client_fd = -1;

    cr_assert_eq(server_init(&server, 0, 20, 3, 42), 0);
    port = bound_port(&server);
    start_server_for_integration_test(&server, &tid, &start_arg);

    client_fd = connect_test_client(port);
    client_send_join(client_fd);
    client_expect_welcome(client_fd, 0, 20, 3);
    client_send_leave(client_fd);
    close_if_open(&client_fd);

    usleep(100 * 1000);

    pthread_mutex_lock(&server.board_mutex);
    cr_assert_eq(server.client_fds[0], -1);
    cr_assert_eq(server.client_snake_ids[0], -1);
    cr_assert_eq(server.board.snakes[0].alive, 0);
    pthread_mutex_unlock(&server.board_mutex);

    stop_server_for_integration_test(&server, tid, &start_arg, port);
    server_cleanup(&server);
}
