#ifndef TETRIS_NETWORK_H
#define TETRIS_NETWORK_H

#define SERVER_IP "10.0.2.15"  // Change to your server's IP
#define SERVER_PORT 8080

typedef struct {
    char name[32];
    int score;
} leaderboard_entry;

// Function declarations
int connect_to_server();
int submit_score(const char* player_name, int score);
int fetch_leaderboard();
void parse_leaderboard_response(const char* response);
void display_leaderboard();
int update_leaderboard_nonblocking();

// Global variables (extern declarations)
extern leaderboard_entry top_scores[10];
extern int score_count;
extern int last_leaderboard_update;

#endif
