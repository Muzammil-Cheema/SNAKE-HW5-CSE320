#include <criterion/criterion.h>
#include <string.h>

#include "game_board.h"
#include "global.h"

int cell_index(const game_board_t *board, int x, int y) {
	return y * board->size + x;
}

position_t make_position(int x, int y) {
	position_t pos;
	pos.x = x;
	pos.y = y;
	return pos;
}

void assert_border_walls(const game_board_t *board) {
	for (int y = 0; y < board->size; y++) {
		for (int x = 0; x < board->size; x++) {
			cell_t cell = board->cells[cell_index(board, x, y)];
			if (x == 0 || y == 0 || x == board->size - 1 || y == board->size - 1) {
				cr_assert_eq(cell, CELL_WALL, "Border cell (%d, %d) should be a wall", x, y);
			} else {
				cr_assert(cell == CELL_EMPTY || cell == CELL_APPLE, "Interior cell (%d, %d) should be empty or contain the apple", x, y);
			}
		}
	}
}

Test(board_suite, init_sets_expected_state) {
	game_board_t board = {0};
	int ret = board_init(&board, 20, 4, 42);

	cr_assert_eq(ret, 0, "board_init should succeed with valid arguments");
	cr_assert_eq(board.size, 20);
	cr_assert_eq(board.max_snakes, 4);
	cr_assert_eq(board.num_snakes, 0);
	cr_assert_not_null(board.cells);

	assert_border_walls(&board);
	cr_assert_gt(board.apple.x, 0);
	cr_assert_gt(board.apple.y, 0);
	cr_assert_lt(board.apple.x, board.size - 1);
	cr_assert_lt(board.apple.y, board.size - 1);
	cr_assert_eq(board.cells[cell_index(&board, board.apple.x, board.apple.y)], CELL_APPLE);

	int apple_count = 0;
	for (int i = 0; i < board.size * board.size; i++) {
		if (board.cells[i] == CELL_APPLE) {
			apple_count++;
		}
	}
	cr_assert_eq(apple_count, 1, "board_init should place exactly one apple");

	for (int i = 0; i < MAX_PLAYERS; i++) {
		cr_assert_eq(board.snakes[i].alive, 0);
		cr_assert_eq(board.snakes[i].length, 0);
	}

	board_free(&board);
}

Test(board_suite, init_rejects_invalid_arguments) {
	game_board_t board = {0};

	cr_assert_eq(board_init(NULL, 20, 4, 42), -1);
	cr_assert_eq(board_init(&board, BOARD_SIZE_MIN - 1, 4, 42), -1);
	cr_assert_eq(board_init(&board, BOARD_SIZE_MAX + 1, 4, 42), -1);
	cr_assert_eq(board_init(&board, 20, MAX_PLAYERS_MIN - 1, 42), -1);
	cr_assert_eq(board_init(&board, 20, MAX_PLAYERS_MAX + 1, 42), -1);
}

Test(board_suite, place_apple_is_deterministic) {
	game_board_t first = {0};
	game_board_t second = {0};
	int ret1 = board_init(&first, 20, 4, 12345);
	int ret2 = board_init(&second, 20, 4, 12345);

	cr_assert_eq(ret1, 0);
	cr_assert_eq(ret2, 0);
	cr_assert_eq(first.apple.x, second.apple.x);
	cr_assert_eq(first.apple.y, second.apple.y);
	cr_assert_eq(memcmp(first.cells, second.cells, (size_t)first.size * (size_t)first.size * sizeof(cell_t)), 0, "Boards initialized with the same seed should match exactly");

	board_free(&first);
	board_free(&second);
}

Test(board_suite, place_apple_uses_the_only_empty_cell) {
	game_board_t board = {0};
	int ret = board_init(&board, 10, 4, 7);

	cr_assert_eq(ret, 0);

	for (int y = 1; y < board.size - 1; y++) {
		for (int x = 1; x < board.size - 1; x++) {
			board.cells[cell_index(&board, x, y)] = CELL_WALL;
		}
	}

	position_t target = make_position(4, 4);
	board.cells[cell_index(&board, target.x, target.y)] = CELL_EMPTY;

	ret = board_place_apple(&board);
	cr_assert_eq(ret, 0);
	cr_assert_eq(board.apple.x, target.x);
	cr_assert_eq(board.apple.y, target.y);
	cr_assert_eq(board.cells[cell_index(&board, target.x, target.y)], CELL_APPLE);

	for (int y = 1; y < board.size - 1; y++) {
		for (int x = 1; x < board.size - 1; x++) {
			if (x == target.x && y == target.y) {
				continue;
			}
			cr_assert_eq(board.cells[cell_index(&board, x, y)], CELL_WALL);
		}
	}

	board_free(&board);
}

Test(board_suite, add_and_remove_snakes) {
	game_board_t board = {0};
	int ret = board_init(&board, 20, 4, 42);
	int snake_id0 = -1;
	int snake_id1 = -1;
	position_t start0 = make_position(board.size / 4, board.size / 4);
	position_t start1 = make_position((3 * board.size) / 4, board.size / 4);

	cr_assert_eq(ret, 0);
	board.cells[cell_index(&board, start0.x, start0.y)] = CELL_EMPTY;
	board.cells[cell_index(&board, start1.x, start1.y)] = CELL_EMPTY;

	ret = board_add_snake(&board, &snake_id0);
	cr_assert_eq(ret, 0);
	cr_assert_eq(snake_id0, 0);
	cr_assert_eq(board.num_snakes, 1);
	cr_assert_eq(board.snakes[0].id, 0);
	cr_assert_eq(board.snakes[0].length, 1);
	cr_assert_eq(board.snakes[0].alive, 1);
	cr_assert_eq(board.snakes[0].direction, DIR_RIGHT);
	cr_assert_eq(board.snakes[0].next_direction, DIR_RIGHT);
	cr_assert_eq(board.snakes[0].body[0].x, start0.x);
	cr_assert_eq(board.snakes[0].body[0].y, start0.y);
	cr_assert_eq(board.cells[cell_index(&board, start0.x, start0.y)], CELL_SNAKE_0);

	ret = board_add_snake(&board, &snake_id1);
	cr_assert_eq(ret, 0);
	cr_assert_eq(snake_id1, 1);
	cr_assert_eq(board.num_snakes, 2);
	cr_assert_eq(board.snakes[1].id, 1);
	cr_assert_eq(board.snakes[1].length, 1);
	cr_assert_eq(board.snakes[1].alive, 1);
	cr_assert_eq(board.snakes[1].direction, DIR_RIGHT);
	cr_assert_eq(board.snakes[1].next_direction, DIR_RIGHT);
	cr_assert_eq(board.snakes[1].body[0].x, start1.x);
	cr_assert_eq(board.snakes[1].body[0].y, start1.y);
	cr_assert_eq(board.cells[cell_index(&board, start1.x, start1.y)], CELL_SNAKE_1);

	ret = board_remove_snake(&board, 0);
	cr_assert_eq(ret, 0);
	cr_assert_eq(board.num_snakes, 1);
	cr_assert_eq(board.snakes[0].alive, 0);
	cr_assert_eq(board.snakes[0].length, 0);
	cr_assert_eq(board.cells[cell_index(&board, start0.x, start0.y)], CELL_EMPTY);

	ret = board_remove_snake(&board, 1);
	cr_assert_eq(ret, 0);
	cr_assert_eq(board.num_snakes, 0);
	cr_assert_eq(board.snakes[1].alive, 0);
	cr_assert_eq(board.snakes[1].length, 0);
	cr_assert_eq(board.cells[cell_index(&board, start1.x, start1.y)], CELL_EMPTY);

	board_free(&board);
}

Test(board_suite, add_snake_fails_when_full) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 2, 42);
	position_t start0 = make_position(board.size / 4, board.size / 4);
	position_t start1 = make_position((3 * board.size) / 4, board.size / 4);

	cr_assert_eq(ret, 0);
	board.cells[cell_index(&board, start0.x, start0.y)] = CELL_EMPTY;
	board.cells[cell_index(&board, start1.x, start1.y)] = CELL_EMPTY;

	cr_assert_eq(board_add_snake(&board, &snake_id), 0);
	cr_assert_eq(snake_id, 0);
	cr_assert_eq(board_add_snake(&board, &snake_id), 0);
	cr_assert_eq(snake_id, 1);
	cr_assert_eq(board.num_snakes, 2);
	cr_assert_eq(board_add_snake(&board, &snake_id), -1);
	cr_assert_eq(board.num_snakes, 2);

	board_free(&board);
}

Test(board_suite, remove_snake_rejects_invalid_ids) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 4, 42);
	position_t start0 = make_position(board.size / 4, board.size / 4);

	cr_assert_eq(ret, 0);
	cr_assert_eq(board_remove_snake(&board, -1), -1);
	cr_assert_eq(board_remove_snake(&board, MAX_PLAYERS), -1);
	cr_assert_eq(board_remove_snake(&board, 5), -1);

	board.cells[cell_index(&board, start0.x, start0.y)] = CELL_EMPTY;
	cr_assert_eq(board_add_snake(&board, &snake_id), 0);
	cr_assert_eq(board_remove_snake(&board, snake_id), 0);
	cr_assert_eq(board_remove_snake(&board, snake_id), -1);

	board_free(&board);
}
