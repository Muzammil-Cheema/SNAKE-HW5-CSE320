#include <criterion/criterion.h>
#include <string.h>
#include <unistd.h>

#include "game_board.h"
#include "global.h"
#include "snake.h"

extern int _cell_index(const game_board_t *board, int x, int y);
extern position_t make_position(int x, int y);
extern void print_board_to_stderr(const game_board_t *board, const char *label);

cell_t snake_cell_value(int snake_id) {
	return (cell_t)(CELL_SNAKE_0 + snake_id);
}

void clear_snake_cells(game_board_t *board, int snake_id) {
	cell_t value = snake_cell_value(snake_id);

	for (int i = 0; i < board->size * board->size; i++) {
		if (board->cells[i] == value) {
			board->cells[i] = CELL_EMPTY;
		}
	}
}

void install_snake_body(game_board_t *board, int snake_id, const position_t *segments, int length, direction_t direction) {
	clear_snake_cells(board, snake_id);

	board->snakes[snake_id].id = snake_id;
	board->snakes[snake_id].length = length;
	board->snakes[snake_id].direction = direction;
	board->snakes[snake_id].next_direction = direction;
	board->snakes[snake_id].alive = 1;

	for (int i = 0; i < length; i++) {
		board->snakes[snake_id].body[i] = segments[i];
		board->cells[_cell_index(board, segments[i].x, segments[i].y)] =
		    snake_cell_value(snake_id);
	}
}

// =========================================================================
//  snake_set_direction tests
// =========================================================================

Test(snake_suite, set_direction_rejects_invalid_inputs) {
	snake_t snake = {0};
	snake.direction = DIR_RIGHT;
	snake.next_direction = DIR_UP;
	snake.alive = 1;

	cr_assert_eq(snake_set_direction(NULL, DIR_UP), -1);
	cr_assert_eq(snake_set_direction(&snake, (direction_t)99), -1);

	cr_assert_eq(snake.direction, DIR_RIGHT);
	cr_assert_eq(snake.next_direction, DIR_UP);
	cr_assert_eq(snake.alive, 1);
}

Test(snake_suite, set_direction_updates_buffer_and_ignores_opposite) {
	snake_t snake = {0};
	snake.direction = DIR_RIGHT;
	snake.next_direction = DIR_RIGHT;
	snake.alive = 1;

	cr_assert_eq(snake_set_direction(&snake, DIR_UP), 0);
	cr_assert_eq(snake.direction, DIR_RIGHT);
	cr_assert_eq(snake.next_direction, DIR_UP);

	cr_assert_eq(snake_set_direction(&snake, DIR_LEFT), 0);
	cr_assert_eq(snake.direction, DIR_RIGHT);
	cr_assert_eq(snake.next_direction, DIR_UP);
}

// =====================================================
//  snake_advance tests
// ===================================================

Test(snake_suite, advance_rejects_invalid_inputs) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 4, 42);
	size_t cell_bytes;
	cell_t *cells_before;
	snake_t snake_before;
	int num_snakes_before;

	cr_assert_eq(ret, 0);
	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);

	{
		position_t head = make_position(5, 5);
		install_snake_body(&board, snake_id, &head, 1, DIR_UP);
		print_board_to_stderr(&board, "advance_rejects_invalid_inputs after setup");
	}

	cell_bytes = (size_t)board.size * (size_t)board.size * sizeof(cell_t);
	cells_before = malloc(cell_bytes);
	cr_assert_not_null(cells_before);
	memcpy(cells_before, board.cells, cell_bytes);
	snake_before = board.snakes[snake_id];
	num_snakes_before = board.num_snakes;

	cr_assert_eq(snake_advance(NULL, snake_id), -1);
	cr_assert_eq(snake_advance(&board, -1), -1);
	cr_assert_eq(snake_advance(&board, board.max_snakes), -1);

	cr_assert_eq(memcmp(board.cells, cells_before, cell_bytes), 0);
	cr_assert_eq(memcmp(&board.snakes[snake_id], &snake_before, sizeof(snake_t)), 0);
	cr_assert_eq(board.num_snakes, num_snakes_before);

	free(cells_before);
	board_free(&board);
}

Test(snake_suite, advance_moves_snake_right_on_empty_cell) {
	game_board_t board = {0};
	int snake_id = -1;
	position_t segments[1];
	int ret = board_init(&board, 20, 4, 42);

	cr_assert_eq(ret, 0);
	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);

	segments[0] = make_position(5, 5);
	install_snake_body(&board, snake_id, segments, 1, DIR_RIGHT);
	board.cells[_cell_index(&board, 6, 5)] = CELL_EMPTY;
	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_RIGHT), 0);
	print_board_to_stderr(&board, "advance_moves_snake_right_on_empty_cell before snake_advance");

	ret = snake_advance(&board, snake_id);
	print_board_to_stderr(&board, "advance_moves_snake_right_on_empty_cell after snake_advance");

	cr_assert_eq(ret, 0, "unexpected return value of %d, instead of 0 for snake_advance()", ret);
	cr_assert_eq(board.snakes[snake_id].direction, DIR_RIGHT);
	cr_assert_eq(board.snakes[snake_id].next_direction, DIR_RIGHT);
	cr_assert_eq(board.snakes[snake_id].length, 1);
	cr_assert_eq(board.snakes[snake_id].body[0].x, 6);
	cr_assert_eq(board.snakes[snake_id].body[0].y, 5);
	cr_assert_eq(board.cells[_cell_index(&board, 5, 5)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, 6, 5)], snake_cell_value(snake_id));
	cr_assert_eq(board.num_snakes, 1);

	board_free(&board);
}

Test(snake_suite, advance_grows_snake_when_eating_apple) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 4, 42);
	position_t head;
	position_t apple = board.apple;

	cr_assert_eq(ret, 0);
	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);

	head = make_position(apple.x, apple.y + 1);
	install_snake_body(&board, snake_id, &head, 1, DIR_UP);
	cr_assert_eq(snake_set_direction(&(board.snakes[snake_id]), DIR_UP), 0);

	print_board_to_stderr(&board, "advance_grows_snake_when_eating_apple before snake_advance");
	ret = snake_advance(&board, snake_id);
	print_board_to_stderr(&board, "advance_grows_snake_when_eating_apple after snake_advance");

	cr_assert_eq(ret, 2);
	cr_assert_eq(board.snakes[snake_id].alive, 1);
	cr_assert_eq(board.snakes[snake_id].length, 2);
	cr_assert_eq(board.snakes[snake_id].direction, DIR_UP);
	cr_assert_eq(board.snakes[snake_id].next_direction, DIR_UP);
	cr_assert_eq(board.snakes[snake_id].body[0].x, apple.x);
	cr_assert_eq(board.snakes[snake_id].body[0].y, apple.y);
	cr_assert_eq(board.snakes[snake_id].body[1].x, head.x);
	cr_assert_eq(board.snakes[snake_id].body[1].y, head.y);
	cr_assert_eq(board.cells[_cell_index(&board, head.x, head.y)], snake_cell_value(snake_id));
	cr_assert_eq(board.cells[_cell_index(&board, apple.x, apple.y)], snake_cell_value(snake_id));
	cr_assert_eq(board.cells[_cell_index(&board, board.apple.x, board.apple.y)], CELL_APPLE);
	cr_assert_eq(board.num_snakes, 1);

	board_free(&board);
}

Test(snake_suite, advance_kills_snake_on_wall_collision) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 4, 42);
	position_t head = make_position(1, 1);

	cr_assert_eq(ret, 0);
	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);

	install_snake_body(&board, snake_id, &head, 1, DIR_UP);
	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_UP), 0);
	print_board_to_stderr(&board, "advance_kills_snake_on_wall_collision before snake_advance");

	ret = snake_advance(&board, snake_id);
	print_board_to_stderr(&board, "advance_kills_snake_on_wall_collision after snake_advance");

	cr_assert_eq(ret, 1);
	cr_assert_eq(board.snakes[snake_id].alive, 0);
	cr_assert_eq(board.snakes[snake_id].length, 0);
	cr_assert_eq(board.num_snakes, 0);
	cr_assert_eq(board.cells[_cell_index(&board, head.x, head.y)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, 1, 0)], CELL_WALL);

	board_free(&board);
}

Test(snake_suite, advance_moves_long_snake_normally) {
	game_board_t board = {0};
	int snake_id = -1;
	position_t segments[10];
	position_t expected[10];
	direction_t moves[] = {
		DIR_RIGHT, DIR_RIGHT,
		DIR_DOWN, DIR_RIGHT,
		DIR_DOWN, DIR_RIGHT,
		DIR_DOWN, DIR_RIGHT,
		DIR_DOWN, DIR_RIGHT
	};
	int ret = board_init(&board, 20, 4, 42);

	cr_assert_eq(ret, 0);
	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);

	board.cells[_cell_index(&board, board.apple.x, board.apple.y)] = CELL_EMPTY;
	board.apple = make_position(board.size - 2, board.size - 2);
	board.cells[_cell_index(&board, board.apple.x, board.apple.y)] = CELL_APPLE;

	for (int i = 0; i < 10; i++) {
		segments[i] = make_position(10 - i, 10);
		expected[i] = segments[i];
	}
	install_snake_body(&board, snake_id, segments, 10, DIR_RIGHT);
	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_RIGHT), 0);
	print_board_to_stderr(&board, "advance_moves_long_snake_normally after setup");

	for (int step = 0; step < (int)(sizeof(moves) / sizeof(moves[0])); step++) {
		position_t new_head = expected[0];

		switch (moves[step]) {
			case DIR_RIGHT:
				new_head.x++;
				break;
			case DIR_LEFT:
				new_head.x--;
				break;
			case DIR_UP:
				new_head.y--;
				break;
			case DIR_DOWN:
				new_head.y++;
				break;
		}

		cr_assert_eq(snake_set_direction(&board.snakes[snake_id], moves[step]), 0);
		ret = snake_advance(&board, snake_id);
		print_board_to_stderr(&board, "advance_moves_long_snake_normally after snake_advance");
		cr_assert_eq(ret, 0);
		cr_assert_eq(board.snakes[snake_id].alive, 1);
		cr_assert_eq(board.snakes[snake_id].direction, moves[step]);
		cr_assert_eq(board.snakes[snake_id].next_direction, moves[step]);
		cr_assert_eq(board.snakes[snake_id].length, 10);
		for (int i = 9; i > 0; i--) {
			expected[i] = expected[i - 1];
		}
		expected[0] = new_head;
		cr_assert_eq(board.snakes[snake_id].body[0].x, expected[0].x);
		cr_assert_eq(board.snakes[snake_id].body[0].y, expected[0].y);
		for (int i = 0; i < 10; i++) {
			cr_assert_eq(board.snakes[snake_id].body[i].x, expected[i].x);
			cr_assert_eq(board.snakes[snake_id].body[i].y, expected[i].y);
			cr_assert_eq(board.cells[_cell_index(&board, expected[i].x, expected[i].y)], snake_cell_value(snake_id));
		}
		cr_assert_eq(board.cells[_cell_index(&board, expected[9].x, expected[9].y)], snake_cell_value(snake_id));
	}

	board_free(&board);
}

// ======================================================
//  Full game logic tests
// ======================================================

Test(snake_suite, full_single_snake_runs_into_wall) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 4, 42);
	position_t head;

	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_runs_into_wall after board_init");

	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_runs_into_wall after board_add_snake");

	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_UP), 0);
	print_board_to_stderr(&board, "full_single_snake_runs_into_wall after snake_set_direction");

	while (board.snakes[snake_id].alive) {
		head = board.snakes[snake_id].body[0];
		ret = board_tick(&board);
		print_board_to_stderr(&board, "full_single_snake_runs_into_wall after board_tick");
		cr_assert_eq(ret, 0);

		if (head.y == 1) {
			cr_assert_eq(board.snakes[snake_id].alive, 0);
			break;
		}

		cr_assert_eq(board.snakes[snake_id].alive, 1);
		cr_assert_eq(board.snakes[snake_id].body[0].x, head.x);
		cr_assert_eq(board.snakes[snake_id].body[0].y, head.y - 1);
	}

	cr_assert_eq(board.num_snakes, 0);
	cr_assert_eq(board.cells[_cell_index(&board, head.x, head.y)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, head.x, 0)], CELL_WALL);

	board_free(&board);
}

// ============================
// 			BUG
// ============================
Test(snake_suite, full_single_snake_eats_apple) {
	game_board_t board = {0};
	int snake_id = -1;
	int ret = board_init(&board, 20, 4, 42);
	position_t *head;
	position_t apple;
	direction_t horizontal_dir;
	direction_t vertical_dir;

	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_eats_apple after board_init");

	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_eats_apple after board_add_snake");

	head = &(board.snakes[snake_id].body[0]);
	apple = board.apple;

//	if (head->y == apple.y) {
//		direction_t detour_dir = (head->y > 1) ? DIR_UP : DIR_DOWN;
//		cr_assert_eq(snake_set_direction(&board.snakes[snake_id], detour_dir), 0);
//		ret = snake_advance(&board, snake_id);
//		print_board_to_stderr(&board, "full_single_snake_eats_apple after vertical detour");
//		cr_assert_eq(ret, 0);
//	}

	horizontal_dir = (head->x < apple.x) ? DIR_RIGHT : DIR_LEFT;
	while (head->x != apple.x) {
		cr_assert_eq(snake_set_direction(&board.snakes[snake_id], horizontal_dir), 0);
		ret = snake_advance(&board, snake_id);
		print_board_to_stderr(&board, "full_single_snake_eats_apple after horizontal move");
		usleep(200000);

		if (head->x == apple.x && head->y == apple.y) {
			cr_assert_eq(ret, 2);
			break;
		}

		if (ret == 1) {
			cr_assert_eq(board.snakes[snake_id].alive, 0);
			ret = board_add_snake(&board, &snake_id);
			cr_assert_eq(ret, 0);
			head = &(board.snakes[snake_id].body[0]);
			direction_t respawn_turn = (apple.y > head->y) ? DIR_DOWN :
				((head->y > 1) ? DIR_UP : DIR_DOWN);
			cr_assert_eq(snake_set_direction(&board.snakes[snake_id], respawn_turn), 0);
			ret = snake_advance(&board, snake_id);
			print_board_to_stderr(&board, "full_single_snake_eats_apple after horizontal respawn turn");
			usleep(200000);
			cr_assert_eq(ret, 0);
			horizontal_dir = (horizontal_dir == DIR_RIGHT) ? DIR_LEFT : DIR_RIGHT;
			continue;
		}

		cr_assert_eq(ret, 0);
	}

	vertical_dir = (head->y < apple.y) ? DIR_DOWN : DIR_UP;
	while (head->y != apple.y) {
		cr_assert_eq(snake_set_direction(&board.snakes[snake_id], vertical_dir), 0);
		ret = snake_advance(&board, snake_id);
		print_board_to_stderr(&board, "full_single_snake_eats_apple after vertical move");
		usleep(200000);

		if (head->x == apple.x && head->y == apple.y) {
			cr_assert_eq(ret, 2);
			break;
		}

		if (ret == 1) {
			cr_assert_eq(board.snakes[snake_id].alive, 0);
			ret = board_add_snake(&board, &snake_id);
			cr_assert_eq(ret, 0);
			head = &(board.snakes[snake_id].body[0]);
			vertical_dir = (vertical_dir == DIR_DOWN) ? DIR_UP : DIR_DOWN;
			cr_assert_eq(snake_set_direction(&board.snakes[snake_id], vertical_dir), 0);
			print_board_to_stderr(&board, "full_single_snake_eats_apple after vertical respawn");
			usleep(200000);
			continue;
		}

		cr_assert_eq(ret, 0);
	}

	cr_assert_eq(board.snakes[snake_id].alive, 1);
	cr_assert_eq(board.snakes[snake_id].length, 2);
	cr_assert_eq(board.snakes[snake_id].body[0].x, apple.x);
	cr_assert_eq(board.snakes[snake_id].body[0].y, apple.y);
	cr_assert_eq(board.cells[_cell_index(&board, apple.x, apple.y)], snake_cell_value(snake_id));
	cr_assert_eq(board.cells[_cell_index(&board, board.apple.x, board.apple.y)], CELL_APPLE);
	cr_assert(!(board.apple.x == apple.x && board.apple.y == apple.y));
	cr_assert_eq(board.num_snakes, 1);

	board_free(&board);
}

Test(snake_suite, full_single_snake_collides_with_itself) {
	game_board_t board = {0};
	int snake_id = -1;
	position_t segments[5];
	int ret = board_init(&board, 20, 4, 42);

	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after board_init");

	ret = board_add_snake(&board, &snake_id);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after board_add_snake");

	segments[0] = make_position(5, 5);
	segments[1] = make_position(4, 5);
	segments[2] = make_position(3, 5);
	segments[3] = make_position(3, 6);
	segments[4] = make_position(3, 7);
	install_snake_body(&board, snake_id, segments, 5, DIR_RIGHT);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after install_snake_body");

	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_DOWN), 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after snake_set_direction down");

	ret = board_tick(&board);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after first board_tick");
	cr_assert_eq(board.snakes[snake_id].alive, 1);
	cr_assert_eq(board.snakes[snake_id].body[0].x, 5);
	cr_assert_eq(board.snakes[snake_id].body[0].y, 6);

	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_LEFT), 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after snake_set_direction left");

	ret = board_tick(&board);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after second board_tick");
	cr_assert_eq(board.snakes[snake_id].alive, 1);
	cr_assert_eq(board.snakes[snake_id].body[0].x, 4);
	cr_assert_eq(board.snakes[snake_id].body[0].y, 6);

	cr_assert_eq(snake_set_direction(&board.snakes[snake_id], DIR_UP), 0);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after snake_set_direction up");

	ret = board_tick(&board);
	print_board_to_stderr(&board, "full_single_snake_collides_with_itself after collision board_tick");
	cr_assert_eq(ret, 0);
	cr_assert_eq(board.num_snakes, 0);
	cr_assert_eq(board.snakes[snake_id].alive, 0);
	cr_assert_eq(board.snakes[snake_id].length, 0);
	cr_assert_eq(board.cells[_cell_index(&board, 5, 5)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, 4, 5)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, 3, 5)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, 3, 6)], CELL_EMPTY);
	cr_assert_eq(board.cells[_cell_index(&board, 3, 7)], CELL_EMPTY);

	board_free(&board);
}

Test(snake_suite, full_multi_snake_flow_with_collision_and_wall_death) {
	game_board_t board = {0};
	int snake0 = -1;
	int snake1 = -1;
	position_t snake0_body[3];
	int ret = board_init(&board, 20, 4, 42);
	position_t snake1_head;

	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after board_init");

	ret = board_add_snake(&board, &snake0);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after board_add_snake snake0");

	ret = board_add_snake(&board, &snake1);
	cr_assert_eq(ret, 0);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after board_add_snake snake1");

	snake0_body[0] = make_position(5, 5);
	snake0_body[1] = make_position(5, 4);
	snake0_body[2] = make_position(4, 4);
	install_snake_body(&board, snake0, snake0_body, 3, DIR_UP);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after snake0 placement");

	cr_assert_eq(snake_set_direction(&board.snakes[snake0], DIR_UP), 0);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after snake0 direction");

	cr_assert_eq(snake_set_direction(&board.snakes[snake1], DIR_UP), 0);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after snake1 direction");

	ret = board_tick(&board);
	print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after first board_tick");
	cr_assert_eq(ret, 0);
	cr_assert_eq(board.snakes[snake0].alive, 0);
	cr_assert_eq(board.snakes[snake0].length, 0);
	cr_assert_eq(board.snakes[snake1].alive, 1);

	while (board.snakes[snake1].alive) {
		snake1_head = board.snakes[snake1].body[0];
		ret = board_tick(&board);
		print_board_to_stderr(&board, "full_multi_snake_flow_with_collision_and_wall_death after board_tick");
		cr_assert_eq(ret, 0);

		if (snake1_head.y == 1) {
			cr_assert_eq(board.snakes[snake1].alive, 0);
			break;
		}

		cr_assert_eq(board.snakes[snake1].alive, 1);
		cr_assert_eq(board.snakes[snake1].body[0].x, snake1_head.x);
		cr_assert_eq(board.snakes[snake1].body[0].y, snake1_head.y - 1);
	}

	cr_assert_eq(board.num_snakes, 0);
	board_free(&board);
}
