//#include <sys/types.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include "server.h"
#include "debug.h"
#include "global.h"
#include "protocol.h"

static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t signal_listen_fd = -1;

static void handle_sigint(int signal_number) {
	int saved_errno = errno;
	(void)signal_number;

	shutdown_requested = 1;
	if (signal_listen_fd >= 0)
		close((int)signal_listen_fd);

	errno = saved_errno;
}

int server_init(server_t *server, int port, int board_size, int max_snakes, unsigned int seed) {
	if (!server) {
		debug("server argument is NULL in server/server_init()");
		return -1;
	}

	int welcoming_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (welcoming_socket_fd < 0) {
		debug("socket() failed in server.c/server_init(): %s\n", strerror(errno));
		return -1;
	}

	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(port);

	int yes = 1;
	if (setsockopt(welcoming_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0){
		debug("setsockopt() failed in server.c/server_init(): %s\n", strerror(errno));
		close(welcoming_socket_fd);
		return -1;
	}

	if (bind(welcoming_socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
		debug("bind() failed in server.c/server_init(): %s\n", strerror(errno));
		close(welcoming_socket_fd);
		return -1;
	}

	if (listen(welcoming_socket_fd, SOMAXCONN) < 0) {
		debug("listen() failed in server.c/server_init(): %s\n", strerror(errno));
		close(welcoming_socket_fd);
		return -1;
	}

	if (board_init(&(server->board), board_size, max_snakes, seed) < 0){
		debug("board_init() failed in server.c/server_init()\n");
		close(welcoming_socket_fd);
		return -1;
	}

	if (pthread_mutex_init(&(server->board_mutex), NULL) != 0){
		debug("pthread_mutex_init() failed in server.c/server_init()\n");
		close(welcoming_socket_fd);
		board_free(&(server->board));
		return -1;
	}

	server->listen_fd = welcoming_socket_fd;
	for (int i = 0; i < MAX_PLAYERS; i++){
		server->client_fds[i] = -1;
		server->client_snake_ids[i] = -1;
	}
	server->running = 1;

	return 0;
}

int _client_fd_from_snake_id(server_t *server, int snake_id) {
	if (!server || snake_id < 0 || snake_id >= MAX_PLAYERS) {
		return -1;
	}

	for (int i = 0; i < MAX_PLAYERS; i++){
		if (server->client_snake_ids[i] == snake_id){
			return server->client_fds[i];
		}
	}

	return -1;
}

void *server_game_loop(void *arg) {
	if (!arg){
		debug("NULL pointer in server/server_game_loop()");
		return NULL;
	}
	server_t *server = (server_t*)arg;

	while (1) {
		usleep(TICK_INTERVAL_MS * 1000);

//========================= Mutex lock ======================================
		pthread_mutex_lock(&(server->board_mutex));
		if (!server->running){
			pthread_mutex_unlock(&(server->board_mutex));
			break;
		}
		int fd = -1;
		int was_alive[MAX_PLAYERS] = {-1};
		int dead_fds[MAX_PLAYERS] = {-1};
		int broadcast_fds[MAX_PLAYERS] = {-1} ;
		for (int i = 0; i < MAX_PLAYERS; i++) {
			was_alive[i] = -1;
			dead_fds[i] = -1;
			broadcast_fds[i] = -1;
		}

		for (int i = 0; i < server->board.max_snakes; i++){
			was_alive[i] = server->board.snakes[i].alive;
		}

		if (board_tick(&(server->board)) < 0){
			debug("Error thrown from board_tick() to server/server_game_loop()\n");
			server->running = 0;
			pthread_mutex_unlock(&(server->board_mutex));
			return NULL;
		}

		//Set client fds for just died and still alive to send after mutex unlock
		for (int i = 0; i < server->board.max_snakes; i++){
			if (was_alive[i] && server->board.snakes[i].alive == 0){
				fd = _client_fd_from_snake_id(server, i);
				if (fd == -1){
					debug("client_fd_from_snake_id() failed in server.game_loop()\n");
					server->running = 0;
					pthread_mutex_unlock(&(server->board_mutex));
					return NULL;
				}
				dead_fds[i] = fd;
			}

			if (server->client_fds[i] >= 0){
				broadcast_fds[i] = server->client_fds[i];
			}
		}

		size_t dead_buf_len = 2 * MAX_PLAYERS;
		uint8_t dead_buf[dead_buf_len];
		int game_state_buf_len = 6;

		//Serialize dead message
		for (int i = 0; i < server->board.max_snakes; i++){
			if (dead_fds[i] >= 0){
				if (protocol_serialize_dead(dead_buf + (i*2), dead_buf_len - (i*2), server->board.snakes[i].id) == -1){
					debug("Error thrown from protocol_serialize_dead() to server/server_game_loop()\n");
					server->running = 0;
					pthread_mutex_unlock(&(server->board_mutex));
					return NULL;
				}
			}
			if (server->board.snakes[i].alive == 1){
				game_state_buf_len += 4 + (4 * server->board.snakes[i].length);
			}
		}

		// Serialize game state message
		uint8_t game_state_buf[game_state_buf_len];
		if (protocol_serialize_game_state(game_state_buf, game_state_buf_len, &(server->board)) == -1){
			debug("Error thrown from protocol_serialize_game_state() to server/server_game_loop()\n");
			server->running = 0;
			pthread_mutex_unlock(&(server->board_mutex));
			return NULL;
		}
		pthread_mutex_unlock(&(server->board_mutex));
// ====================== Mutex unlock =======================================


		//Send dead messages
		for (int i = 0; i < MAX_PLAYERS; i++){
			if (dead_fds[i] < 0)
				continue;
			if (send_all(dead_fds[i], dead_buf + (i*2), 2) == -1){
				debug("send_all() failed for client %d in server/server_game_loop()\n", dead_fds[i]);
				//TODO determine how to cleanly handle closed clients
				continue;
			}
		}

		//Send game state messages
		for (int i = 0; i < MAX_PLAYERS; i++){
			if (broadcast_fds[i] < 0)
				continue;
			if (send_all(broadcast_fds[i], game_state_buf, game_state_buf_len) == -1){
				debug("send_all() failed for client %d in server/server_game_loop()\n", broadcast_fds[i]);
				//TODO determine how to cleanly handle closed clients
				continue;
			}
		}
	}

	server->running = 0;
	return NULL;
}

void *server_client_handler(void *arg) {
	server_t *server = NULL;
	int client_fd = -1;
	int snake_out_id = -1;
	int slot = -1;
	uint8_t err = ERR_INVALID_MSG;
	bool cleanup = false;
	size_t buf_len = CLIENT_MSG_SIZE;
	uint8_t buf[buf_len];
	uint8_t welcome_buf_len = 4;
	uint8_t welcome_buf[welcome_buf_len];

	if (!arg) {
	debug("NULL argument to server/server_client_handler()");
	return NULL;
}

	server = ((client_handler_arg_t*)arg)->server;
	client_fd = ((client_handler_arg_t*)arg)->client_fd;
	if (!server || client_fd < 0){
		debug("NULL server or client passed into server/server_client_handler()");
		goto cleanup;
	}


	if (recv_exact(client_fd, buf, buf_len) == -1){
		debug("recv_exact() failed for client %d in server/server_client_handler()", client_fd);
		goto cleanup;
	}

	uint8_t out_type;
	uint8_t out_payload;
	if (protocol_deserialize_client_msg(buf, buf_len, &out_type, &out_payload) == -1){
		debug("deserialize_client_msg() failed for client %d in server/server_client_handler()", client_fd);
		err = ERR_INVALID_MSG;
		cleanup = true;
		goto error;
	}

	if (out_type != MSG_JOIN){
		debug("JOIN message expected from client %d in server/server_client_handler()", client_fd);
		err = ERR_INVALID_MSG;
		cleanup = true;
		goto error;
	}

// ========================================================
// =============== Mutex LOCK ==========================
// ========================================================
	pthread_mutex_lock(&(server->board_mutex));


	if (board_add_snake(&(server->board), &snake_out_id) == -1){
		err = ERR_GAME_FULL;
		pthread_mutex_unlock(&(server->board_mutex));
		cleanup = true;
		goto error;
	}

	for (int i = 0; i < server->board.max_snakes; i++){
		if (server->client_fds[i] == -1 && server->client_snake_ids[i] == -1){
			slot = i;
			break;
		}
	}
	if (slot == -1){
		debug("Snake slot unexpectedly not found in server/server_client_handler()");
		board_remove_snake(&(server->board), snake_out_id);
		snake_out_id = -1;
		pthread_mutex_unlock(&(server->board_mutex));
		err = ERR_GAME_FULL;
		cleanup = true;
		goto error;
	}

	int welcome_ret = protocol_serialize_welcome(welcome_buf, welcome_buf_len, snake_out_id, server->board.size, server->board.max_snakes);
	pthread_mutex_unlock(&(server->board_mutex));
// ========================================================
// =============== Mutex UNLOCK ==========================
// ========================================================

	if (welcome_ret != 4){
		debug("invalid welcome protocol serialization in server/server_client_handler()");
		err = ERR_INVALID_MSG;
		goto cleanup_board;
	}
	if (send_all(client_fd, welcome_buf, welcome_buf_len) == -1){
		err = ERR_INVALID_MSG;
		goto cleanup_board;
	}
	pthread_mutex_lock(&(server->board_mutex));
	server->client_fds[slot] = client_fd;
	server->client_snake_ids[slot] = snake_out_id;
	pthread_mutex_unlock(&(server->board_mutex));


	loop_start:
	while(1){
		if (recv_exact(client_fd, buf, buf_len) == -1){
			debug("recv_exact() failed for client %d in server/server_client_handler()", client_fd);
			err = ERR_INVALID_MSG;
			cleanup = true;
			goto cleanup_board;
		}

		if (protocol_deserialize_client_msg(buf, buf_len, &out_type, &out_payload) == -1){
			debug("deserialize_client_msg() failed for client %d in server/server_client_handler()", client_fd);
			err = ERR_INVALID_MSG;
			cleanup = false;
			goto error;
		}

		if (out_type == MSG_JOIN){
			debug("JOIN sent when client %d already joined in server/server_client_handler()", client_fd);
			err = ERR_ALREADY_JOINED;
			cleanup = false;
			goto error;
		}

		if (out_type == MSG_DIRECTION){
// ========================================================
// =============== Mutex LOCK ==========================
// ========================================================
			pthread_mutex_lock(&(server->board_mutex));
			if (server->board.snakes[snake_out_id].alive == 1)
				snake_set_direction(&(server->board.snakes[snake_out_id]), out_payload);
			pthread_mutex_unlock(&(server->board_mutex));
// ========================================================
// =============== Mutex UNLOCK ==========================
// ========================================================
		}
		if (out_type == MSG_LEAVE){
			break;
		}
	}

	goto cleanup_board;

	error:
		if (protocol_serialize_error(buf, buf_len, err) == 2)
			send_all(client_fd, buf, buf_len);
		if (cleanup) goto cleanup;
		else goto loop_start;

	cleanup_board:
// ========================================================
// =============== Mutex LOCK ==========================
// ========================================================
			pthread_mutex_lock(&(server->board_mutex));
			if (snake_out_id >= 0 && server->board.snakes[snake_out_id].alive == 1)
				board_remove_snake(&(server->board), snake_out_id);
			if (slot >= 0) {
				server->client_fds[slot] = -1;
				server->client_snake_ids[slot] = -1;
			}
			pthread_mutex_unlock(&(server->board_mutex));
// ========================================================
// =============== Mutex UNLOCK ==========================
// ========================================================

	cleanup:
		if (client_fd >= 0) close(client_fd);
		free(arg);
	return NULL;
}

int server_start(server_t *server) {
	if (!server){
		debug("server argument is NULL in server/server_start()");
		return -1;
	}

	int client_fd = -1;
	int running = -1;
	struct sigaction sigint_action;

	shutdown_requested = 0;
	signal_listen_fd = server->listen_fd;
	memset(&sigint_action, 0, sizeof(sigint_action));
	sigint_action.sa_handler = handle_sigint;
	sigemptyset(&sigint_action.sa_mask);
	if (sigaction(SIGINT, &sigint_action, NULL) < 0){
		debug("sigaction failed in server/server_start(): %s\n", strerror(errno));
		signal_listen_fd = -1;
		return -1;
	}

	//Create game loop thread
	pthread_t game_loop_tid;
	if (pthread_create(&game_loop_tid, NULL, server_game_loop, server) != 0){
		debug("Thread creation failed for game loop thread in server/server_start()");
		signal_listen_fd = -1;
		return -1;
	}

	while (1){
		//Always check if running
		pthread_mutex_lock(&(server->board_mutex));
		running = server->running;
		pthread_mutex_unlock(&(server->board_mutex));
		if (!running) break;

		//Accept clients
		if ((client_fd = accept(server->listen_fd, NULL, NULL)) < 0){
			int accept_errno = errno;

			if (shutdown_requested){
				pthread_mutex_lock(&(server->board_mutex));
				running = 0;
				server->running = 0;
				server->listen_fd = -1;
				pthread_mutex_unlock(&(server->board_mutex));
				break;
			}

			if (accept_errno == EINTR || accept_errno == ECONNABORTED){
				continue;
			}

			debug("accept failed in server/server_start(): %s\n", strerror(accept_errno));
			pthread_mutex_lock(&(server->board_mutex));
			running = 0;
			server->running = 0;
			pthread_mutex_unlock(&(server->board_mutex));
			pthread_join(game_loop_tid, NULL);
			signal_listen_fd = -1;
			return -1;
		}

		//Check running status
		pthread_mutex_lock(&(server->board_mutex));
		running = server->running;
		pthread_mutex_unlock(&(server->board_mutex));
		if (!running) {
			close(client_fd);
			break;
		}

		//Create server_client_handler() argument for client thread
		client_handler_arg_t *client_handler_arg = malloc(sizeof(client_handler_arg_t));
		if (client_handler_arg == NULL){
			debug("client_handler_arg allocation failed in server/server_start()");
			close(client_fd);
			continue;
		}
		pthread_mutex_lock(&(server->board_mutex));
		client_handler_arg->server = server;
		client_handler_arg->client_fd = client_fd;
		pthread_mutex_unlock(&(server->board_mutex));

		//Create detached client thread
		pthread_t tid;
		if (pthread_create(&tid, NULL, server_client_handler, client_handler_arg) != 0){
			debug("pthread_create failed in server/server_start()");
			close(client_fd);
			free(client_handler_arg);
			continue;
		}
		pthread_detach(tid);
	}

	pthread_join(game_loop_tid, NULL);
	signal_listen_fd = -1;
	return 0;
}

void server_cleanup(server_t *server) { 
	if (!server){
		debug("server argument is NULL in server/server_cleanup()");
		return;
	}

// ========================================================
// =============== Mutex LOCK ==========================
// ========================================================
	pthread_mutex_lock(&(server->board_mutex));
	server->running = 0;
	for (int i = 0; i < server->board.max_snakes; i++) {
		if (server->client_fds[i] >= 0) {
			close(server->client_fds[i]);
			server->client_fds[i] = -1;
			server->client_snake_ids[i] = -1;
		}
	}
	if (server->listen_fd >= 0){
		close(server->listen_fd);
		server->listen_fd = -1;
	}
	board_free(&(server->board));
	pthread_mutex_unlock(&(server->board_mutex));
// ========================================================
// =============== Mutex UNLOCK ==========================
// ========================================================

	if (pthread_mutex_destroy(&(server->board_mutex)) != 0){
		debug("pthread_mutex_destroy() failed in server.c/server_cleanup()\n");
		return;
	}
}

int recv_exact(int fd, uint8_t *buf, size_t len) {
	if (!buf || !len || fd < 0){
		debug("Invalid input in recv_exact()");
		return -1;
	}

	size_t received = 0;
	ssize_t n = 0;
	while (received < len){
		n = recv(fd, buf + received, len - received, 0);
		if (n > 0){
			received += n;
		}
		else if (n == -1){
			debug("recv() failed in recv_exact(): %s\n", strerror(errno));
			return -1;
		}
		else if (n == 0){
			debug("Disconnection in recv_exact()\n");
			return -1;
		}
	}

	return 0;
}

int send_all(int fd, const uint8_t *buf, size_t len) {
	if (!buf || !len || fd < 0){
		debug("Invalid input in send_all()");
		return -1;
	}

	size_t sent = 0;
	ssize_t n = 0;
	while (sent < len){
		n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
		if (n > 0){
			sent += n;
		}
		else if (n == -1){
			debug("send() failed in send_all(): %s\n", strerror(errno));
			return -1;
		}
		else if (n == 0){
			debug("Disconnection in send_all()\n");
			return -1;
		}
	}

	return 0;
}