#include "debug.h"
#include "snake.h"
#include "game_board.h"
#include "server.h"

int _snake_grow(struct game_board * board, int snake_id, position_t *new_head) {
	// Input vaidation
	if (!board || !board->cells || !new_head){
		debug("NULL pointer passed to snake/_snake_grow()");
		return -1;
	}
	if (snake_id < 0 || snake_id > board->max_snakes){
		debug("Invalid snake_id passed into snake/_snake_grow()");
		return -1;
	}

//	debug("Input validation passed");

	snake_t *snake = &board->snakes[snake_id];
	if (snake->length == MAX_SNAKE_LENGTH){
		debug("Snake is already max length in _snake_grow()");
		return 1;
	}

//	debug("Before body shift");
	//Body shift
	for (int i = snake->length-1; i >= 0; i--){
		snake->body[i+1] = snake->body[i];
	}
//	debug("After body shift");
	snake->body[0] = *new_head;
	snake->length++;
	board->cells[(new_head->y) * (board->size) + (new_head->x)] = CELL_SNAKE_0 + snake_id;
	return 0;
}

int _snake_move(struct game_board *board, int snake_id, position_t *new_head) {
	if (!board || !board->cells || !new_head){
		debug("NULL pointer passed to snake/_snake_move()");
		return -1;
	}
	if (snake_id < 0 || snake_id > board->max_snakes){
		debug("Invalid snake_id passed into snake/_snake_move()");
		return -1;
	}
//	debug("Input validation passed for _snake_move()");

	//Set board cells
	snake_t *snake = &board->snakes[snake_id];
	int tail_cell_index = snake->body[snake->length-1].y * board->size + snake->body[snake->length-1].x;
	board->cells[tail_cell_index] = CELL_EMPTY;
	int head_cell_index = new_head->y * board->size + new_head->x;
	board->cells[head_cell_index] = CELL_SNAKE_0 + snake_id;

//	for (int i = 0; i < snake->length; i++) {
//		debug("Snake body positions: (%d, %d)", snake->body[i].x, snake->body[i].y);
//	}

	//Set snake body
	for (int i = snake->length-1; i > 0; i--) {
		snake->body[i] = snake->body[i-1];
	}
	snake->body[0] = *new_head;
//	debug("=============================");
//	for (int i = 0; i < snake->length; i++) {
//		debug("Snake body positions: (%d, %d)", snake->body[i].x, snake->body[i].y);
//	}
//	debug("=============================");


	return 0;
}

int snake_set_direction(snake_t *snake, direction_t dir) {
	//Input validation
	if (!snake){
		debug("NULL pointer to snake in snake/snake_set_direction()");
		return -1;
	}
	if (dir != DIR_UP && dir != DIR_DOWN && dir != DIR_LEFT && dir != DIR_RIGHT) {
		debug("Invalid direction argument in snake/snake_set_direction()");
		return -1;
	}

	// Ignore opposite direction
	if ((snake->direction == DIR_UP && dir == DIR_DOWN) ||
		(snake->direction == DIR_DOWN && dir == DIR_UP) ||
		(snake->direction == DIR_LEFT && dir == DIR_RIGHT) ||
		(snake->direction == DIR_RIGHT && dir == DIR_LEFT)) {
		return 0;
	}

	snake->next_direction = dir;

	return 0;
}

int snake_advance(struct game_board *board, int snake_id) {
	//Input validation
	if (!board || !board->cells){
		debug("NULL pointer to board in snake/snake_advance()");
		return -1;
	}
	if (snake_id < 0 || snake_id >= board->max_snakes){
		debug("Invalid snake id in snake/snake_advance()");
		return -1;
	}

	//Find new head position
	snake_t *snake = &board->snakes[snake_id];
	snake->direction = snake->next_direction;
	position_t new_head_position;
	switch (snake->direction) {
		case DIR_UP:
			new_head_position.x = snake->body[0].x;
			new_head_position.y = snake->body[0].y - 1;
			break;
		case DIR_DOWN:
			new_head_position.x = snake->body[0].x;
			new_head_position.y = snake->body[0].y + 1;
			break;
		case DIR_LEFT:
			new_head_position.x = snake->body[0].x - 1;
			new_head_position.y = snake->body[0].y;
			break;
		case DIR_RIGHT:
			new_head_position.x = snake->body[0].x + 1;
			new_head_position.y = snake->body[0].y;
			break;
		default:
			debug("Invalid direction caught in snake/snake_advance()");
			return -1;
	}

	int new_head_cell_index = new_head_position.y * board->size + new_head_position.x;
//	debug("new_head_cell_index: %d", new_head_cell_index);
	switch (board->cells[new_head_cell_index]) {
		case CELL_EMPTY:
			if (_snake_move(board, snake_id, &new_head_position) == -1){
				debug("error thrown from snake/_snake_move() to snake/snake_advance()");
				return -1;
			}
			return 0;

		case CELL_APPLE:
			if (snake->length < MAX_SNAKE_LENGTH){
//				debug("Run _snake_grow()");
				if (_snake_grow(board, snake_id, &new_head_position) == -1){
					debug("error thrown from snake/_snake_grow() to snake_advance()");
					return -1;
				}
			}
			else {
//				debug("Run _snake_move()");
				if (_snake_move(board, snake_id, &new_head_position) == -1){
					debug("error thrown from snake/_snake_move() to snake_advance()");
					return -1;
				}
			}
			board_place_apple(board);
			return 2;

		case CELL_WALL:	// Death case, either CELL_WALL or CELL_SNAKE_*
		case CELL_SNAKE_0:
		case CELL_SNAKE_1:
		case CELL_SNAKE_2:
		case CELL_SNAKE_3:
		case CELL_SNAKE_4:
		case CELL_SNAKE_5:
		case CELL_SNAKE_6:
		case CELL_SNAKE_7:
			if (board_remove_snake(board, snake_id) == -1){
				debug("error thrown from game_board/board_remove_snake() to snake_advance()");
				return -1;
			}
			return 1;
	}

	debug("Reached end unexpectedly in snake/snake_advance()");
	return -1;
}