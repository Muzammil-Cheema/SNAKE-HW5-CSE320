#include <string.h>
#include "global.h"
#include "protocol.h"
#include "debug.h"

int protocol_serialize_welcome(uint8_t *buf, size_t buf_len, int player_id, int board_size, int max_players) {
	if (!buf || buf_len < 4 ||
	  player_id < 0 || player_id >= MAX_PLAYERS_MAX ||
	  board_size < BOARD_SIZE_MIN || board_size > BOARD_SIZE_MAX ||
	  max_players < MAX_PLAYERS_MIN || max_players > MAX_PLAYERS_MAX) {
		debug("input validation failed for protocol/protocol_serialize_welcome()");
		return -1;
	}

	buf[0] = 0x10;
	buf[1] = (uint8_t)player_id;
	buf[2] = (uint8_t)board_size;
	buf[3] = (uint8_t)max_players;
	return 4;
}

int protocol_serialize_game_state(uint8_t *buf, size_t buf_len, const game_board_t *board) {
	if (!buf || !board || !board->cells){
		debug("NULL pointers passed as input for protocol/protocol_serialize_game_state()");
		return -1;
	}

	size_t buf_len_needed = 6;
	uint8_t alive_snakes = 0;
	for (int i = 0; i < board->max_snakes; i++){
		if (board->snakes[i].alive == 1){
			buf_len_needed += 4 + (board->snakes[i].length * 4);
			alive_snakes++;
		}
	}

	if (buf_len < buf_len_needed){
		debug("insufficient buffer size for protocol/protocol_serialize_game_state()");
		return -1;
	}

	size_t buf_off = 0;
	buf[buf_off++] = 0x20;
	buf[buf_off++] = alive_snakes;

	uint16_t apple_x = htons(board->apple.x);
	uint16_t apple_y = htons(board->apple.y);
	memcpy(&buf[buf_off], &apple_x, sizeof(apple_x));
	buf_off += sizeof(apple_x);
	memcpy(&buf[buf_off], &apple_y, sizeof(apple_y));
	buf_off += sizeof(apple_y);

	for (int i = 0; i < board->max_snakes; i++){
		if (board->snakes[i].alive == 1){
			buf[buf_off++] = (uint8_t) (board->snakes[i].id);
			uint16_t snake_length = htons(board->snakes[i].length);
			memcpy(&buf[buf_off], &snake_length, sizeof(snake_length));
			buf_off += sizeof(snake_length);
			buf[buf_off++] = (uint8_t) (board->snakes[i].direction);

			for (int j = 0; j < board->snakes[i].length; j++){
				uint16_t snake_body_x = htons(board->snakes[i].body[j].x);
				uint16_t snake_body_y = htons(board->snakes[i].body[j].y);
				memcpy(&buf[buf_off], &snake_body_x, sizeof(snake_body_x));
				buf_off += sizeof(snake_body_x);
				memcpy(&buf[buf_off], &snake_body_y, sizeof(snake_body_y));
				buf_off += sizeof(snake_body_y);
			}
		}
	}

	return (int)buf_off;
}

int protocol_serialize_dead(uint8_t *buf, size_t buf_len, int player_id) {
	if (!buf || buf_len < 2 || player_id < 0 || player_id >= MAX_PLAYERS_MAX) {
		debug("input validation failed for protocol/protocol_serialize_dead()");
		return -1;
	}

	buf[0] = 0x30;
	buf[1] = (uint8_t)player_id;
	return 2;
}

int protocol_serialize_game_over(uint8_t *buf, size_t buf_len, int winner_id) {
	if (!buf || buf_len < 2 || winner_id < 0 || (winner_id >= MAX_PLAYERS_MAX && winner_id != 0xFF)) {
		debug("input validation failed for protocol/protocol_serialize_game_over()");
		return -1;
	}

	buf[0] = 0x40;
	buf[1] = (uint8_t)winner_id;
	return 2;
}

int protocol_serialize_error(uint8_t *buf, size_t buf_len, uint8_t error_code) {
	if (!buf || buf_len < 2 || (error_code != 0x01 && error_code != 0x02 && error_code != 0x03)) {
		debug("input validation failed for protocol/protocol_serialize_error()");
		return -1;
	}

	buf[0] = 0xF0;
	buf[1] = error_code;
	return 2;
}

int protocol_deserialize_client_msg(const uint8_t *buf, size_t buf_len, uint8_t *out_type, uint8_t *out_payload) {
	if (!buf || buf_len < 2 || !out_type || !out_payload) {
		debug("input validation failed for protocol/protocol_deserialize_client_msg()");
		return -1;
	}

	uint8_t type = buf[0];
	uint8_t payload = buf[1];
	if ((type == 0x01 && payload == 0x00) ||
		(type == 0x03 && payload == 0x00) ||
		(type == 0x02 && (payload >= 0x00 && payload <= 0x03)) ){
		*out_type = type;
		*out_payload = payload;
		return 0;
	}

	debug("unexpected message for protocol/protocol_deserialize_client_msg()");
	return -1;
}
