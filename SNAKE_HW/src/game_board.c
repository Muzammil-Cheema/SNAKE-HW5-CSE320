#include <stdbool.h>
#include "game_board.h"
#include "debug.h"
#include "global.h"

int board_init(game_board_t *board, int size, int max_snakes, unsigned int seed) {
	// Input validation
	if (!board){
		debug("board pointer is NULL in game_board/board_init()");
		return -1;
	}
	if (size < BOARD_SIZE_MIN || size > BOARD_SIZE_MAX) {
		debug("board size is invalid in game_board/board_init()");
		return -1;
	}
	if (max_snakes < MAX_PLAYERS_MIN || max_snakes > MAX_PLAYERS_MAX) {
		debug("max_snakes is invalid in game_board/board_init()");
		return -1;
	}

	//Initialize cells
	board->cells = calloc(size * size, sizeof(cell_t));	//Use calloc to set all cells empty
	if (!board->cells) {
		debug("malloc failed for cells array in game_board/board_init()");
		return -1;
	}
	for (int i = 0; i < size; i++)	//top row
		board->cells[i] = CELL_WALL;
	for (int i = 0; i < size * size; i+=size)	//Left column
		board->cells[i] = CELL_WALL;
	for (int i = ((size*size) - size); i < size * size; i++)	//bottom row
		board->cells[i] = CELL_WALL;
	for (int i = size-1; i < size * size; i+=size)	//right column
		board->cells[i] = CELL_WALL;

	//Initialize additonal values
	for (int i = 0; i < MAX_PLAYERS; i++) {
		board->snakes[i].alive = 0;
		board->snakes[i].length = 0;
		board->snakes[i].id = i;
	}
	board->size = size;
	board->num_snakes = 0;
	board->max_snakes = max_snakes;
	board->rng_state = seed;

	if (board_place_apple(board) != 0){
		debug("error thrown from game_board/board_place_apple() to game_board/board_init()");
	}
	return 0;
}

void board_free(game_board_t *board) {
	if (!board){
		debug("board pointer is NULL in game_board/board_free()");
		return;
	}

	if (board->cells) free(board->cells);
	board->cells = NULL;
	board->num_snakes = 0;
	board->size = 0;
}

unsigned int board_random(game_board_t *board) {
	if (!board){
		debug("board pointer is NULL in game_board/board_init()");
		return 50000;
	}

	board->rng_state = board->rng_state * 1103515245 + 12345;
	return (board->rng_state / 65536) % 32768;
}

int board_place_apple(game_board_t *board) {
	if (!board){
		debug("board pointer is NULL in game_board/board_init()");
		return -1;
	}

	unsigned int empty_count = 0;
	for (int i = 0; i < board->size * board->size; i++) {
//		debug("checking for empty cell");
		if (board->cells[i] == CELL_EMPTY){
			empty_count++;
//			debug("empty cell found");
		}
	}
	if (empty_count == 0){
		debug("No empty cells left to place apple in game_board/board_place_apple()");
		return -1;
	}

	unsigned int random = board_random(board);
	if (random == 50000){
		debug("error thrown from game_board/board_random() to game_board/board_place_apple()");
		return -1;
	}

	unsigned int target = random % empty_count;
	unsigned int index = 0;
	for (int i = 0; i < board->size * board->size; i++) {
		if (board->cells[i] == CELL_EMPTY){
			if (index == target){
				board->cells[i] = CELL_APPLE;
				position_t apple_position;
				apple_position.x = i % board->size;
				apple_position.y = i / board->size;
				board->apple = apple_position;
				break;
			}
			else {
				index++;
			}
		}
	}

	return 0;
}

int board_add_snake(game_board_t *board, int *out_id) {
	if (!board || !out_id || !board->cells){
		debug("NULL argument passed into game_board/board_add_snake()");
		return -1;
	}

	bool found_snake = false;
	for (int i = 0; i < board->max_snakes; i++) {
		if (board->snakes[i].alive == 0) {
			*out_id = board->snakes[i].id;
			found_snake = true;
			break;
		}
	}
	if (!found_snake) {
		debug("max snakes reached in game_board/board_add_snake()");
		return -1;
	}

	int i = (*out_id) % 4;
	int s = board->size;
	switch (i) {
		case 0:
			i = (s * s/4) + s/4;	//top left starting point
			break;
		case 1:
			i = (s * s/4) + 3*s/4;	//top right
			break;
		case 2:
			i = (s * 3*s/4) + s/4;	//bottom left
			break;
		case 3:
			i = (s * 3*s/4) + 3*s/4;	//bottom right
			break;
	}

	bool empty_cell_exists = false;
	for (; i < board->size * board->size; i++) {
		if (board->cells[i] == CELL_EMPTY){		//update snake attributes
			board->snakes[(*out_id)].id = (*out_id);
			board->snakes[(*out_id)].length = 1;
			board->snakes[(*out_id)].alive = 1;
			board->snakes[(*out_id)].direction = DIR_RIGHT;
			board->snakes[(*out_id)].next_direction = DIR_RIGHT;

			position_t head_position;			//update snake head position
			head_position.x = i % board->size;
			head_position.y = i / board->size;
			board->snakes[(*out_id)].body[0] = head_position;
			empty_cell_exists = true;

			board->cells[i] = CELL_SNAKE_0 + (*out_id);	//update other board data
			board->num_snakes++;
			break;
		}
	}

	if (!empty_cell_exists) {
		debug("board is full error in game_board/board_add_snake()");
		return -1;
	}

	return 0;
}

int board_remove_snake(game_board_t *board, int snake_id) {
	if (!board || snake_id >= board->num_snakes || snake_id < 0 || !board->cells){
		debug("invalid input in game_board/board_remove_snake()");
		return -1;
	}

	//Clear board cells where snake existed
	for (int i = 0; i < board->size * board->size; i++) {
		if (board->cells[i] == (cell_t) (CELL_SNAKE_0 + snake_id))
			board->cells[i] = CELL_EMPTY;
	}
	board->snakes[snake_id].alive = 0;
	board->snakes[snake_id].length = 0;
	board->num_snakes--;

	return 0;
}

int board_tick(game_board_t *board) {
	if (!board){
		debug("board pointer is NULL in game_board/board_init()");
		return -1;
	}

	for (int i = 0; i < board->max_snakes; i++) {
		if (board->snakes[i].alive){
			int ret = -2;
			if ((ret = snake_advance(board, i)) == -1){
				debug("error thrown from snake/snake_advance() to game_board/board_tick()");
				return -1;
			}
			else {
				debug("snake %d %s", board->snakes[i].id, ret == 0 ? " is alive and well" : (ret == 1 ? "snake just disintegrated bruz" : "snake has big belly"));
			}
		}
	}

	return 0;
}
