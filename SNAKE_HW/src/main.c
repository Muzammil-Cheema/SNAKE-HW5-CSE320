
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include "global.h"
#include "debug.h"

#define PORT_MIN 1
#define PORT_MAX UINT16_MAX
#define SEED_DEFAULT (time(NULL))
#define SEED_MIN 0
#define SEED_MAX UINT_MAX

int main(int argc, char *argv[]) {
	opterr = 0;

	unsigned long port_number = 0;
	unsigned long board_size = 0;
	unsigned int seed = 0;
	unsigned long max_players = 0;
	bool port_number_seen = false;
	bool board_size_seen = false;
	bool seed_seen = false;
	bool max_players_seen = false;
	int option = 0;

	optind = 1;
	while ((option = getopt(argc, argv, "h")) != -1){
		switch (option) {
			case 'h':
				PRINT_USAGE();
				exit(EXIT_SUCCESS);
				break;
		}
	}

	optind = 1;
	while ((option = getopt(argc, argv, "+p:b:s:m:h")) != -1){
		char *end_ptr = NULL;
		errno = 0;
		switch (option) {
			case 'p':
				port_number = strtoul(optarg, &end_ptr, 10);
				if (optarg == end_ptr || *end_ptr != '\0' || errno == ERANGE || port_number > PORT_MAX || port_number < PORT_MIN) {
					ERR_INVALID_PORT(optarg);
					exit(EXIT_FAILURE);
				}
				port_number_seen = true;
				break;
			case 'b':
				board_size = strtoul(optarg, &end_ptr, 10);
				if (optarg == end_ptr || *end_ptr != '\0' || errno == ERANGE || board_size < BOARD_SIZE_MIN || board_size > BOARD_SIZE_MAX) {
					ERR_INVALID_BOARD_SIZE(atoi(optarg));
					exit(EXIT_FAILURE);
				}
				board_size_seen = true;
				break;
			case 's':
				(void) seed;	//Used to avoid the declaration after label interpreter warning
				long temp_seed = strtol(optarg, &end_ptr, 10);
				if (optarg == end_ptr || *end_ptr != '\0' || errno == ERANGE || temp_seed > SEED_MAX || temp_seed < SEED_MIN) {
					ERR_MSG("Invalid seed valued parsed %s", optarg);
					exit(EXIT_FAILURE);
				}
				seed = (unsigned int) temp_seed;
				seed_seen = true;
				break;
			case 'm':
				max_players = strtoul(optarg, &end_ptr, 10);
				if (optarg == end_ptr || *end_ptr != '\0' || errno == ERANGE || max_players < MAX_PLAYERS_MIN || max_players > MAX_PLAYERS_MAX) {
					ERR_INVALID_MAX_PLAYERS(atoi(optarg));
					exit(EXIT_FAILURE);
				}
				max_players_seen = true;
				break;
			case '?':
				ERR_MSG("Unexpected argument or argument missing option during parsing: %c", optopt);
				exit(EXIT_FAILURE);
				break;
		}
	}

	if (optind < argc) {	// Unexpected additional positional arguments after the ones our getopt reads
		ERR_MSG("Unexpected extra argument: %s", argv[optind]);
		exit(EXIT_FAILURE);
	}

	if (!port_number_seen){
		ERR_PORT_REQUIRED();
		exit(EXIT_FAILURE);
	}
	if (!board_size_seen)
		board_size = BOARD_SIZE_DEFAULT;
	if (!seed_seen)
		seed = SEED_DEFAULT;
	if (!max_players_seen)
		max_players = MAX_PLAYERS_DEFAULT;

	(void) seed;	//Some variables are not read yet, so we want to avoid compiler errors
	(void) port_number;
	(void) board_size;
	(void) max_players;



	return 0;
}