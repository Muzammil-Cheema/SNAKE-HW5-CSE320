#include <arpa/inet.h>
#include <criterion/criterion.h>
#include <string.h>

#include "game_board.h"
#include "global.h"
#include "protocol.h"

void _init_buffer(uint8_t *buf, size_t len) {
	memset(buf, 0xAA, len);
}

void assert_game_state_buffer_matches_board(const uint8_t *buf, const game_board_t *board) {
	size_t off = 0;

	cr_assert_eq(buf[off++], MSG_GAME_STATE);
	cr_assert_eq(buf[off++], board->num_snakes);

	uint16_t apple_x_net = 0;
	uint16_t apple_y_net = 0;
	memcpy(&apple_x_net, &buf[off], sizeof(apple_x_net));
	off += sizeof(apple_x_net);
	memcpy(&apple_y_net, &buf[off], sizeof(apple_y_net));
	off += sizeof(apple_y_net);
	cr_assert_eq(ntohs(apple_x_net), board->apple.x);
	cr_assert_eq(ntohs(apple_y_net), board->apple.y);

	for (int i = 0; i < board->max_snakes; i++) {
		if (!board->snakes[i].alive) {
			continue;
		}

		cr_assert_eq(buf[off++], (uint8_t)board->snakes[i].id);

		uint16_t length_net = 0;
		memcpy(&length_net, &buf[off], sizeof(length_net));
		off += sizeof(length_net);
		cr_assert_eq(ntohs(length_net), board->snakes[i].length);

		cr_assert_eq(buf[off++], (uint8_t)board->snakes[i].direction);

		for (int j = 0; j < board->snakes[i].length; j++) {
			uint16_t x_net = 0;
			uint16_t y_net = 0;
			memcpy(&x_net, &buf[off], sizeof(x_net));
			off += sizeof(x_net);
			memcpy(&y_net, &buf[off], sizeof(y_net));
			off += sizeof(y_net);
			cr_assert_eq(ntohs(x_net), board->snakes[i].body[j].x);
			cr_assert_eq(ntohs(y_net), board->snakes[i].body[j].y);
		}
	}
}

void process_join_request(game_board_t *board, int *snake_id, uint8_t *welcome_buf) {
	uint8_t join_msg[2] = { MSG_JOIN, 0x00 };
	uint8_t type = 0;
	uint8_t payload = 0;
	int ret = protocol_deserialize_client_msg(join_msg, sizeof(join_msg), &type, &payload);

	cr_assert_eq(ret, 0);
	cr_assert_eq(type, MSG_JOIN);
	cr_assert_eq(payload, 0x00);

	ret = board_add_snake(board, snake_id);
	cr_assert_eq(ret, 0);

	ret = protocol_serialize_welcome(welcome_buf, 4, *snake_id, board->size, board->max_snakes);
	cr_assert_eq(ret, 4);
	cr_assert_eq(welcome_buf[0], MSG_WELCOME);
	cr_assert_eq(welcome_buf[1], (uint8_t)*snake_id);
	cr_assert_eq(welcome_buf[2], (uint8_t)board->size);
	cr_assert_eq(welcome_buf[3], (uint8_t)board->max_snakes);
}

void apply_direction_request(game_board_t *board, int snake_id, direction_t dir) {
	uint8_t msg[2] = { MSG_DIRECTION, (uint8_t)dir };
	uint8_t type = 0;
	uint8_t payload = 0;
	int ret = protocol_deserialize_client_msg(msg, sizeof(msg), &type, &payload);

	cr_assert_eq(ret, 0);
	cr_assert_eq(type, MSG_DIRECTION);
	cr_assert_eq(payload, (uint8_t)dir);

	ret = snake_set_direction(&board->snakes[snake_id], (direction_t)payload);
	cr_assert_eq(ret, 0);
}

// ==================================================
//  protocol unit tests
// ================================================

Test(protocol_suite, welcome_rejects_invalid_inputs) {
	uint8_t buf[4];
	uint8_t snapshot[4];

	_init_buffer(buf, sizeof(buf));
	memcpy(snapshot, buf, sizeof(buf));

	cr_assert_eq(protocol_serialize_welcome(NULL, sizeof(buf), 2, 20, 4), -1);

	cr_assert_eq(protocol_serialize_welcome(buf, 3, 2, 20, 4), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_welcome(buf, sizeof(buf), -1, 20, 4), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_welcome(buf, sizeof(buf), 2, BOARD_SIZE_MIN - 1, 4), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_welcome(buf, sizeof(buf), 2, 20, MAX_PLAYERS_MAX + 1), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);
}

Test(protocol_suite, welcome_serializes_expected_bytes) {
	uint8_t buf[4];
	int ret = protocol_serialize_welcome(buf, sizeof(buf), 2, 20, 4);

	cr_assert_eq(ret, 4, "WELCOME should serialize to 4 bytes");
	cr_assert_eq(buf[0], MSG_WELCOME);
	cr_assert_eq(buf[1], 2);
	cr_assert_eq(buf[2], 20);
	cr_assert_eq(buf[3], 4);
}

Test(protocol_suite, game_state_rejects_invalid_inputs) {
	game_board_t board = {0};
	uint8_t buf[64];
	uint8_t snapshot[64];
	int ret;

	_init_buffer(buf, sizeof(buf));
	memcpy(snapshot, buf, sizeof(buf));

	cr_assert_eq(protocol_serialize_game_state(NULL, sizeof(buf), &board), -1);
	cr_assert_eq(protocol_serialize_game_state(buf, sizeof(buf), NULL), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	ret = board_init(&board, 20, 4, 42);
	cr_assert_eq(ret, 0);

	cr_assert_eq(protocol_serialize_game_state(buf, 5, &board), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	board_free(&board);
}

Test(protocol_suite, game_state_serializes_expected_bytes) {
	game_board_t board = {0};
	uint8_t buf[GAME_STATE_BUF_SIZE];
	uint8_t expected[GAME_STATE_BUF_SIZE];
	int snake0 = -1;
	int snake1 = -1;
	int ret;
	size_t expected_len = 22;

	ret = board_init(&board, 20, 4, 42);
	cr_assert_eq(ret, 0);
	cr_assert_eq(board.apple.x, 2);
	cr_assert_eq(board.apple.y, 17);

	ret = board_add_snake(&board, &snake0);
	cr_assert_eq(ret, 0);
	ret = board_add_snake(&board, &snake1);
	cr_assert_eq(ret, 0);

	_init_buffer(buf, sizeof(buf));
	_init_buffer(expected, sizeof(expected));

	expected[0] = MSG_GAME_STATE;
	expected[1] = 2;
	expected[2] = 0x00;
	expected[3] = 0x02;
	expected[4] = 0x00;
	expected[5] = 0x11;
	expected[6] = 0x00;
	expected[7] = 0x00;
	expected[8] = 0x01;
	expected[9] = DIR_RIGHT;
	expected[10] = 0x00;
	expected[11] = 0x05;
	expected[12] = 0x00;
	expected[13] = 0x05;
	expected[14] = 0x01;
	expected[15] = 0x00;
	expected[16] = 0x01;
	expected[17] = DIR_RIGHT;
	expected[18] = 0x00;
	expected[19] = 0x0F;
	expected[20] = 0x00;
	expected[21] = 0x05;

	ret = protocol_serialize_game_state(buf, sizeof(buf), &board);
	cr_assert_gt(ret, 0);
	cr_assert_eq(ret, (int)expected_len);
	cr_assert_eq(memcmp(buf, expected, expected_len), 0);

	board_free(&board);
}

Test(protocol_suite, dead_rejects_invalid_inputs) {
	uint8_t buf[2];
	uint8_t snapshot[2];

	_init_buffer(buf, sizeof(buf));
	memcpy(snapshot, buf, sizeof(buf));

	cr_assert_eq(protocol_serialize_dead(NULL, sizeof(buf), 1), -1);
	cr_assert_eq(protocol_serialize_dead(buf, 1, 1), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_dead(buf, sizeof(buf), -1), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_dead(buf, sizeof(buf), MAX_PLAYERS_MAX), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);
}

Test(protocol_suite, dead_serializes_expected_bytes) {
	uint8_t buf[2];
	int ret = protocol_serialize_dead(buf, sizeof(buf), 3);

	cr_assert_eq(ret, 2, "PLAYER_DEAD should serialize to 2 bytes");
	cr_assert_eq(buf[0], MSG_PLAYER_DEAD);
	cr_assert_eq(buf[1], 3);
}

Test(protocol_suite, game_over_rejects_invalid_inputs) {
	uint8_t buf[2];
	uint8_t snapshot[2];

	_init_buffer(buf, sizeof(buf));
	memcpy(snapshot, buf, sizeof(buf));

	cr_assert_eq(protocol_serialize_game_over(NULL, sizeof(buf), 1), -1);
	cr_assert_eq(protocol_serialize_game_over(buf, 1, 1), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_game_over(buf, sizeof(buf), -1), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_game_over(buf, sizeof(buf), MAX_PLAYERS_MAX), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);
}

Test(protocol_suite, game_over_serializes_expected_bytes) {
	uint8_t buf[2];
	int valid_winners[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xFF};

	for (size_t i = 0; i < 8; i++) {
		int ret = protocol_serialize_game_over(buf, sizeof(buf), valid_winners[i]);

		cr_assert_eq(ret, 2, "GAME_OVER should serialize to 2 bytes for winner %d", valid_winners[i]);
		cr_assert_eq(buf[0], MSG_GAME_OVER);
		cr_assert_eq(buf[1], valid_winners[i]);
	}
}

Test(protocol_suite, error_rejects_invalid_inputs) {
	uint8_t buf[2];
	uint8_t snapshot[2];

	_init_buffer(buf, sizeof(buf));
	memcpy(snapshot, buf, sizeof(buf));

	cr_assert_eq(protocol_serialize_error(NULL, sizeof(buf), ERR_INVALID_MSG), -1);
	cr_assert_eq(protocol_serialize_error(buf, 1, ERR_INVALID_MSG), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_error(buf, sizeof(buf), 0x00), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);

	cr_assert_eq(protocol_serialize_error(buf, sizeof(buf), 0x04), -1);
	cr_assert_eq(memcmp(buf, snapshot, sizeof(buf)), 0);
}

Test(protocol_suite, error_serializes_expected_bytes) {
	uint8_t buf[2];
	int ret = protocol_serialize_error(buf, sizeof(buf), ERR_INVALID_MSG);

	cr_assert_eq(ret, 2, "ERROR should serialize to 2 bytes");
	cr_assert_eq(buf[0], MSG_ERROR);
	cr_assert_eq(buf[1], ERR_INVALID_MSG);
}

Test(protocol_suite, deserialize_rejects_invalid_inputs) {
	uint8_t buf[2] = { MSG_JOIN, 0x00 };
	uint8_t out_type = 0xAA;
	uint8_t out_payload = 0x55;

	cr_assert_eq(protocol_deserialize_client_msg(NULL, sizeof(buf), &out_type, &out_payload), -1);
	cr_assert_eq(protocol_deserialize_client_msg(buf, 1, &out_type, &out_payload), -1);
	cr_assert_eq(protocol_deserialize_client_msg(buf, sizeof(buf), NULL, &out_payload), -1);
	cr_assert_eq(protocol_deserialize_client_msg(buf, sizeof(buf), &out_type, NULL), -1);

	buf[0] = 0x99;
	buf[1] = 0x00;
	cr_assert_eq(protocol_deserialize_client_msg(buf, sizeof(buf), &out_type, &out_payload), -1);
	cr_assert_eq(out_type, 0xAA);
	cr_assert_eq(out_payload, 0x55);

	buf[0] = MSG_JOIN;
	buf[1] = 0x01;
	cr_assert_eq(protocol_deserialize_client_msg(buf, sizeof(buf), &out_type, &out_payload), -1);
	cr_assert_eq(out_type, 0xAA);
	cr_assert_eq(out_payload, 0x55);
}

Test(protocol_suite, deserialize_parses_valid_messages) {
	uint8_t out_type = 0;
	uint8_t out_payload = 0;
	uint8_t join_msg[2] = { MSG_JOIN, 0x00 };
	uint8_t dir_msg[2] = { MSG_DIRECTION, DIR_LEFT };
	uint8_t leave_msg[2] = { MSG_LEAVE, 0x00 };

	cr_assert_eq(protocol_deserialize_client_msg(join_msg, sizeof(join_msg), &out_type, &out_payload), 0);
	cr_assert_eq(out_type, MSG_JOIN);
	cr_assert_eq(out_payload, 0x00);

	out_type = 0;
	out_payload = 0;
	cr_assert_eq(protocol_deserialize_client_msg(dir_msg, sizeof(dir_msg), &out_type, &out_payload), 0);
	cr_assert_eq(out_type, MSG_DIRECTION);
	cr_assert_eq(out_payload, DIR_LEFT);

	out_type = 0;
	out_payload = 0;
	cr_assert_eq(protocol_deserialize_client_msg(leave_msg, sizeof(leave_msg), &out_type, &out_payload), 0);
	cr_assert_eq(out_type, MSG_LEAVE);
	cr_assert_eq(out_payload, 0x00);
}

Test(protocol_suite, integration_three_client_server_flow) {
	game_board_t board = {0};
	int snake_ids[3] = { -1, -1, -1 };
	int client_connected[3] = { 0, 0, 0 };
	uint8_t welcome_buf[4];
	uint8_t state_buf[GAME_STATE_BUF_SIZE];
	uint8_t dead_buf[2];
	uint8_t over_buf[2];
	int ret;
	int winner_id = -1;

	ret = board_init(&board, 20, 4, 42);
	cr_assert_eq(ret, 0);

	process_join_request(&board, &snake_ids[0], welcome_buf);
	client_connected[0] = 1;
	cr_assert_eq(snake_ids[0], 0);
	cr_assert_eq(board.snakes[snake_ids[0]].body[0].x, 5);
	cr_assert_eq(board.snakes[snake_ids[0]].body[0].y, 5);

	ret = board_tick(&board);
	cr_assert_eq(ret, 0);
	ret = protocol_serialize_game_state(state_buf, sizeof(state_buf), &board);
	cr_assert_gt(ret, 0);
	assert_game_state_buffer_matches_board(state_buf, &board);

	process_join_request(&board, &snake_ids[1], welcome_buf);
	client_connected[1] = 1;
	cr_assert_eq(snake_ids[1], 1);
	cr_assert_eq(board.snakes[snake_ids[1]].body[0].x, 15);
	cr_assert_eq(board.snakes[snake_ids[1]].body[0].y, 5);

	apply_direction_request(&board, snake_ids[0], DIR_RIGHT);
	apply_direction_request(&board, snake_ids[1], DIR_UP);
	ret = board_tick(&board);
	cr_assert_eq(ret, 0);
	ret = protocol_serialize_game_state(state_buf, sizeof(state_buf), &board);
	cr_assert_gt(ret, 0);
	assert_game_state_buffer_matches_board(state_buf, &board);

	process_join_request(&board, &snake_ids[2], welcome_buf);
	client_connected[2] = 1;
	cr_assert_eq(snake_ids[2], 2);
	cr_assert_eq(board.snakes[snake_ids[2]].body[0].x, 5);
	cr_assert_eq(board.snakes[snake_ids[2]].body[0].y, 15);

	/* The remaining loop models a simple server tick:
	 * - every connected client sends a direction message
	 * - the board advances one tick
	 * - dead clients receive PLAYER_DEAD
	 * - the full game state is broadcast to all connected clients
	 * - the game ends whne there is one connected client remaining
	 */
	for (int tick = 0; tick < 24 && board.num_snakes > 1; tick++) {
		int alive_before[3];

		for (int i = 0; i < 3; i++) {
			alive_before[i] = board.snakes[i].alive;
		}

		if (client_connected[0] && board.snakes[snake_ids[0]].alive) {
			apply_direction_request(&board, snake_ids[0], DIR_RIGHT);
		}
		if (client_connected[1] && board.snakes[snake_ids[1]].alive) {
			apply_direction_request(&board, snake_ids[1], DIR_UP);
		}
		if (client_connected[2] && board.snakes[snake_ids[2]].alive) {
			const direction_t snake2_path[] = {DIR_RIGHT, DIR_RIGHT, DIR_UP, DIR_UP, DIR_LEFT, DIR_LEFT, DIR_DOWN, DIR_DOWN};
			apply_direction_request(&board, snake_ids[2], snake2_path[tick % 8]);
		}

		ret = board_tick(&board);
		cr_assert_eq(ret, 0);

		for (int i = 0; i < 3; i++) {
			if (alive_before[i] && !board.snakes[i].alive) {
				client_connected[i] = 0;
				ret = protocol_serialize_dead(dead_buf, sizeof(dead_buf), i);
				cr_assert_eq(ret, 2);
				cr_assert_eq(dead_buf[0], MSG_PLAYER_DEAD);
				cr_assert_eq(dead_buf[1], (uint8_t)i);
			}
		}

		ret = protocol_serialize_game_state(state_buf, sizeof(state_buf), &board);
		cr_assert_gt(ret, 0);
		assert_game_state_buffer_matches_board(state_buf, &board);
	}

	cr_assert_eq(board.num_snakes, 1);
	for (int i = 0; i < 3; i++) {
		if (board.snakes[i].alive) {
			winner_id = i;
		}
	}
	cr_assert_eq(winner_id, 2);

	ret = protocol_serialize_game_over(over_buf, sizeof(over_buf), winner_id);
	cr_assert_eq(ret, 2);
	cr_assert_eq(over_buf[0], MSG_GAME_OVER);
	cr_assert_eq(over_buf[1], (uint8_t)winner_id);

	board_free(&board);
}
