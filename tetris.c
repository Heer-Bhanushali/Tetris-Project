// tetris_name_fix.patch
// Fixes player names not being saved and displayed properly

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <ncurses.h>
#include <dirent.h>
#include "tetris_network.h"
#include <sys/select.h>

// Game constants
#define WIDTH 10
#define HEIGHT 20
#define EMPTY '.'
#define BLOCK '#'
#define MAX_PLAYERS 2

// Menu options
typedef enum {
    MENU_START,
    MENU_PLAYERS,
    MENU_KEYBINDS,
    MENU_VOLUME,
    MENU_BACKGROUND,
    MENU_LEADERBOARD,
    MENU_QUIT,
    MENU_TOTAL
} MenuOption;

// Player structures
typedef struct {
    int x, y;
} Point;

typedef struct {
    Point shape[4];
    int x, y;
    int color;
    int type;
} Tetromino;

typedef struct {
    char grid[HEIGHT][WIDTH];
    int color_grid[HEIGHT][WIDTH];
    int score;
    int level;
    int lines_cleared;
    int game_over;
    char player_name[50];
    pthread_mutex_t mutex;
    Tetromino current_piece;
    int drop_speed;
} PlayerState;

// Thread parameter structure
typedef struct {
    int player_id;
} ThreadParam;

// Global variables
PlayerState players[MAX_PLAYERS];
int num_players = 1;
int current_menu = MENU_START;
int volume = 50;
int background_color = 0;
int music_enabled = 1;
int leaderboard_updated = 0;
// Network leaderboard globals
//int last_leaderboard_update = 0;
int game_time = 0;
int global_leaderboard_enabled = 1; // Set to 0 to disable network features

// Terminal dimensions
int term_rows = 30;
int term_cols = 80;

// Key bindings for multiple players
int player_keys[MAX_PLAYERS][4] = {
    {'a', 'd', 's', 'w'},      // Player 1: WASD
    {KEY_LEFT, KEY_RIGHT, KEY_DOWN, KEY_UP} // Player 2: Arrow keys
};

// Tetromino definitions
Point tetrominoes[7][4] = {
    {{0,0}, {1,0}, {2,0}, {3,0}}, // I
    {{0,0}, {1,0}, {0,1}, {1,1}}, // O
    {{0,0}, {1,0}, {2,0}, {1,1}}, // T
    {{0,0}, {1,0}, {1,1}, {2,1}}, // S
    {{1,0}, {2,0}, {0,1}, {1,1}}, // Z
    {{0,0}, {0,1}, {1,1}, {2,1}}, // L
    {{2,0}, {0,1}, {1,1}, {2,1}}  // J
};

// Color pairs
int color_pairs[][2] = {
    {COLOR_RED, COLOR_BLACK},
    {COLOR_GREEN, COLOR_BLACK},
    {COLOR_YELLOW, COLOR_BLACK},
    {COLOR_BLUE, COLOR_BLACK},
    {COLOR_MAGENTA, COLOR_BLACK},
    {COLOR_CYAN, COLOR_BLACK},
    {COLOR_WHITE, COLOR_BLACK}
};

// Background colors
char* bg_color_names[] = {"Black", "Blue", "Green", "Red", "Magenta", "Cyan", "White"};
int bg_colors[] = {COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_RED, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE};

// Leaderboard
typedef struct {
    char name[50];
    int score;
    time_t date;
} LeaderboardEntry;

LeaderboardEntry leaderboard[10];
int leaderboard_size = 0;

// Thread management
pthread_t drop_threads[MAX_PLAYERS];
pthread_t input_threads[MAX_PLAYERS];
pthread_mutex_t console_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t shutdown_requested = 0;
volatile sig_atomic_t return_to_menu = 0;

// Signal handler
void signal_handler(int sig) {
    shutdown_requested = 1;
}

// Get terminal dimensions
void get_terminal_dimensions() {
    getmaxyx(stdscr, term_rows, term_cols);
}

// Center a window based on terminal size
WINDOW* create_centered_window(int height, int width) {
    int start_y = (term_rows - height) / 2;
    int start_x = (term_cols - width) / 2;
    return newwin(height, width, start_y, start_x);
}

// Center text in a window
void center_text(WINDOW* win, int y, const char* text) {
    int width = getmaxx(win);
    int x = (width - strlen(text)) / 2;
    mvwprintw(win, y, x, "%s", text);
}

// Initialize player state with default names
void init_player_state(int player_id) {
    pthread_mutex_init(&players[player_id].mutex, NULL);
   
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            players[player_id].grid[i][j] = EMPTY;
            players[player_id].color_grid[i][j] = 0;
        }
    }
   
    players[player_id].score = 0;
    players[player_id].level = 1;
    players[player_id].lines_cleared = 0;
    players[player_id].game_over = 0;
    players[player_id].drop_speed = 500;
    // Set default name - this will be overwritten if user enters a custom name
    snprintf(players[player_id].player_name, 50, "Player %d", player_id + 1);
}

// Load leaderboard from file
void load_leaderboard() {
    FILE* file = fopen("leaderboard.txt", "r");
    if (!file) return;
   
    leaderboard_size = 0;
    while (leaderboard_size < 10 && fscanf(file, "%49s %d %ld",
           leaderboard[leaderboard_size].name,
           &leaderboard[leaderboard_size].score,
           &leaderboard[leaderboard_size].date) == 3) {
        leaderboard_size++;
    }
    fclose(file);
}

// Save leaderboard to file
void save_leaderboard() {
    FILE* file = fopen("leaderboard.txt", "w");
    if (!file) return;
   
    for (int i = 0; i < leaderboard_size; i++) {
        fprintf(file, "%s %d %ld\n", leaderboard[i].name, leaderboard[i].score, leaderboard[i].date);
    }
    fclose(file);
}

// Add score to leaderboard (FIXED: Only add if score > 0)
void add_to_leaderboard(const char* name, int score) {
    // Only add to leaderboard if player actually scored
    if (score <= 0) {
        return;
    }
    
    LeaderboardEntry new_entry;
    strncpy(new_entry.name, name, 49);
    new_entry.name[49] = '\0';
    new_entry.score = score;
    new_entry.date = time(NULL); // Always use current time
   
    // Find position to insert
    int pos = leaderboard_size;
    for (int i = 0; i < leaderboard_size; i++) {
        if (score > leaderboard[i].score) {
            pos = i;
            break;
        }
    }
   
    // Shift entries down
    for (int i = leaderboard_size; i > pos && i < 10; i--) {
        leaderboard[i] = leaderboard[i-1];
    }
   
    // Insert new entry
    if (pos < 10) {
        leaderboard[pos] = new_entry;
        if (leaderboard_size < 10) leaderboard_size++;
    }
   
    save_leaderboard();
    leaderboard_updated = 1;
}

// NEW FUNCTION: Display full leaderboard screen
// NEW FUNCTION: Display full leaderboard screen
void display_full_leaderboard(WINDOW* win) {
    werase(win);
    
    // Fetch latest global leaderboard
    if (global_leaderboard_enabled) {
        fetch_leaderboard();
    }
    
    center_text(win, 2, "LEADERBOARD");
    
    // Show connection status
    if (global_leaderboard_enabled) {
        if (score_count > 0) {
            center_text(win, 4, "=== GLOBAL ONLINE LEADERBOARD ===");
        } else {
            center_text(win, 4, "=== OFFLINE (Local Scores Only) ===");
        }
    } else {
        center_text(win, 4, "=== LOCAL LEADERBOARD ===");
    }
    
    if (leaderboard_size == 0 && score_count == 0) {
        center_text(win, 8, "No scores yet!");
        center_text(win, 10, "Play a game to see your scores here!");
    } else {
        // Display header
        wattron(win, A_BOLD);
        mvwprintw(win, 6, 10, "RANK");
        mvwprintw(win, 6, 20, "NAME");
        mvwprintw(win, 6, 45, "SCORE");
        mvwprintw(win, 6, 55, "TYPE");
        wattroff(win, A_BOLD);
        
        int display_row = 8;
        int max_display = 10;
        
        // Display global scores first (if available)
        if (score_count > 0) {
            for (int i = 0; i < score_count && i < max_display; i++) {
                // Highlight top 3 scores
                if (i < 3) {
                    wattron(win, COLOR_PAIR(i + 1));
                }
                
                mvwprintw(win, display_row, 10, "%d.", i + 1);
                mvwprintw(win, display_row, 20, "%s", top_scores[i].name);
                mvwprintw(win, display_row, 45, "%d", top_scores[i].score);
                mvwprintw(win, display_row, 55, "GLOBAL");
                
                if (i < 3) {
                    wattroff(win, COLOR_PAIR(i + 1));
                }
                display_row++;
            }
        }
        
        // Display local scores
        for (int i = 0; i < leaderboard_size && i < max_display; i++) {
            char date_str[20];
            if (leaderboard[i].date > 0) {
                strftime(date_str, 20, "%Y-%m-%d", localtime(&leaderboard[i].date));
            } else {
                strcpy(date_str, "Unknown");
            }
            
            mvwprintw(win, display_row, 10, "%d.", i + 1);
            mvwprintw(win, display_row, 20, "%s", leaderboard[i].name);
            mvwprintw(win, display_row, 45, "%d", leaderboard[i].score);
            mvwprintw(win, display_row, 55, "LOCAL");
            
            display_row++;
            if (display_row >= 20) break;
        }
    }
    
    center_text(win, 22, "Press any key to return to menu...");
    wrefresh(win);
    
    nodelay(win, FALSE);
    wgetch(win);
    nodelay(win, TRUE);
}
// FIXED: Increase buffer size to prevent truncation warning
void get_player_names() {
    WINDOW* name_win = create_centered_window(15, 50);
    wbkgd(name_win, COLOR_PAIR(background_color + 10));
    
    for (int i = 0; i < num_players; i++) {
        werase(name_win);
        
        wattron(name_win, A_BOLD | COLOR_PAIR(3));
        center_text(name_win, 2, "ENTER PLAYER NAMES");
        wattroff(name_win, A_BOLD | COLOR_PAIR(3));
        
        char prompt[50];
        snprintf(prompt, 50, "Enter name for Player %d:", i + 1);
        center_text(name_win, 5, prompt);
        
        center_text(name_win, 7, "(Max 15 characters, press ENTER when done)");
        
        // FIX: Increase buffer size from 20 to 60 to prevent truncation
        char current_name_display[60];
        snprintf(current_name_display, 60, "Current: %s", players[i].player_name);
        center_text(name_win, 9, current_name_display);
        
        wrefresh(name_win);
        
        // Get input
        echo();
        curs_set(1);
        
        char input[16] = ""; // 15 chars + null terminator
        int input_x = (getmaxx(name_win) - 15) / 2;
        mvwgetnstr(name_win, 11, input_x, input, 15);
        
        noecho();
        curs_set(0);
        
        // Update player name if input is not empty
        if (strlen(input) > 0) {
            strncpy(players[i].player_name, input, 49);
            players[i].player_name[49] = '\0';
        }
    }
    
    delwin(name_win);
}

// Check collision for a player
int check_collision(int player_id, int dx, int dy) {
    pthread_mutex_lock(&players[player_id].mutex);
    Tetromino* piece = &players[player_id].current_piece;
   
    for (int i = 0; i < 4; i++) {
        int new_x = piece->x + piece->shape[i].x + dx;
        int new_y = piece->y + piece->shape[i].y + dy;
       
        if (new_x < 0 || new_x >= WIDTH || new_y >= HEIGHT ||
            (new_y >= 0 && players[player_id].grid[new_y][new_x] == BLOCK)) {
            pthread_mutex_unlock(&players[player_id].mutex);
            return 1;
        }
    }
   
    pthread_mutex_unlock(&players[player_id].mutex);
    return 0;
}

// Lock piece for a player
void lock_piece(int player_id) {
    pthread_mutex_lock(&players[player_id].mutex);
    Tetromino* piece = &players[player_id].current_piece;
   
    for (int i = 0; i < 4; i++) {
        int x = piece->x + piece->shape[i].x;
        int y = piece->y + piece->shape[i].y;
       
        if (y >= 0) {
            players[player_id].grid[y][x] = BLOCK;
            players[player_id].color_grid[y][x] = piece->color;
        }
    }
    pthread_mutex_unlock(&players[player_id].mutex);
}

// Clear full rows for a player
void clear_full_rows(int player_id) {
    pthread_mutex_lock(&players[player_id].mutex);
   
    int rows_cleared = 0;
    for (int i = HEIGHT - 1; i >= 0; i--) {
        int full_row = 1;
        for (int j = 0; j < WIDTH; j++) {
            if (players[player_id].grid[i][j] == EMPTY) {
                full_row = 0;
                break;
            }
        }
       
        if (full_row) {
            rows_cleared++;
            // Shift rows down
            for (int k = i; k > 0; k--) {
                for (int j = 0; j < WIDTH; j++) {
                    players[player_id].grid[k][j] = players[player_id].grid[k-1][j];
                    players[player_id].color_grid[k][j] = players[player_id].color_grid[k-1][j];
                }
            }
           
            // Clear top row
            for (int j = 0; j < WIDTH; j++) {
                players[player_id].grid[0][j] = EMPTY;
                players[player_id].color_grid[0][j] = 0;
            }
            i++; // Check the same row again after shifting
        }
    }
   
    if (rows_cleared > 0) {
        players[player_id].score += rows_cleared * 100;
        players[player_id].lines_cleared += rows_cleared;
        players[player_id].level = players[player_id].lines_cleared / 10 + 1;
    }
   
    pthread_mutex_unlock(&players[player_id].mutex);
}

// Spawn new piece for a player
void spawn_piece(int player_id) {
    int type = rand() % 7;
    players[player_id].current_piece.type = type;
    players[player_id].current_piece.color = type + 1;
    players[player_id].current_piece.x = WIDTH / 2 - 1;
    players[player_id].current_piece.y = 0;
   
    for (int i = 0; i < 4; i++) {
        players[player_id].current_piece.shape[i] = tetrominoes[type][i];
    }
   
    // Check for game over
    if (check_collision(player_id, 0, 0)) {
        players[player_id].game_over = 1;
    }
}

// Rotate piece for a player
void rotate_piece(int player_id) {
    pthread_mutex_lock(&players[player_id].mutex);
    Tetromino* piece = &players[player_id].current_piece;
   
    Tetromino rotated = *piece;
    for (int i = 0; i < 4; i++) {
        rotated.shape[i].x = -piece->shape[i].y;
        rotated.shape[i].y = piece->shape[i].x;
    }
   
    // Check if rotation is valid
    int valid = 1;
    for (int i = 0; i < 4; i++) {
        int new_x = rotated.x + rotated.shape[i].x;
        int new_y = rotated.y + rotated.shape[i].y;
       
        if (new_x < 0 || new_x >= WIDTH || new_y >= HEIGHT ||
            (new_y >= 0 && players[player_id].grid[new_y][new_x] == BLOCK)) {
            valid = 0;
            break;
        }
    }
   
    if (valid) {
        *piece = rotated;
    }
   
    pthread_mutex_unlock(&players[player_id].mutex);
}

// Move piece for a player
void move_piece(int player_id, int dx, int dy) {
    if (!check_collision(player_id, dx, dy)) {
        players[player_id].current_piece.x += dx;
        players[player_id].current_piece.y += dy;
    } else if (dy > 0) {
        lock_piece(player_id);
        clear_full_rows(player_id);
        spawn_piece(player_id);
    }
}

// Drop thread for each player
void* drop_thread(void* arg) {
    int player_id = *((int*)arg);
    free(arg); // Free the allocated memory
   
    while (!shutdown_requested && !return_to_menu && !players[player_id].game_over) {
        usleep(players[player_id].drop_speed * 1000);
        pthread_mutex_lock(&console_mutex);
        move_piece(player_id, 0, 1);
        pthread_mutex_unlock(&console_mutex);
       
        // Adaptive difficulty
        players[player_id].drop_speed = 500 - (players[player_id].level * 50);
        if (players[player_id].drop_speed < 100) players[player_id].drop_speed = 100;
    }
   
    return NULL;
}

// Input thread for each player (FIXED: 'q' returns to menu instead of quitting)
void* input_thread(void* arg) {
    int player_id = *((int*)arg);
    free(arg); // Free the allocated memory
   
    while (!shutdown_requested && !return_to_menu && !players[player_id].game_over) {
        int ch = getch();
       
        if (ch == 'q' || ch == 'Q') {
            return_to_menu = 1; // Set flag to return to menu instead of quitting
            break;
        }
       
        pthread_mutex_lock(&console_mutex);
       
        // Player 1 controls (WASD)
        if (player_id == 0) {
            switch (ch) {
                case 'a':
                    move_piece(player_id, -1, 0);
                    break;
                case 'd':
                    move_piece(player_id, 1, 0);
                    break;
                case 's':
                    move_piece(player_id, 0, 1);
                    break;
                case 'w':
                    rotate_piece(player_id);
                    break;
            }
        }
        // Player 2 controls (Arrow keys)
        else if (player_id == 1) {
            switch (ch) {
                case KEY_LEFT:
                    move_piece(player_id, -1, 0);
                    break;
                case KEY_RIGHT:
                    move_piece(player_id, 1, 0);
                    break;
                case KEY_DOWN:
                    move_piece(player_id, 0, 1);
                    break;
                case KEY_UP:
                    rotate_piece(player_id);
                    break;
            }
        }
       
        // Hard drop for both players
        if (ch == ' ') {
            while (!check_collision(player_id, 0, 1)) {
                players[player_id].current_piece.y++;
            }
            lock_piece(player_id);
            clear_full_rows(player_id);
            spawn_piece(player_id);
        }
       
        pthread_mutex_unlock(&console_mutex);
        usleep(10000);
    }
   
    return NULL;
}

// NEW FUNCTION: Render keybinds menu
void render_keybinds_menu(WINDOW* win) {
    werase(win);
    
    center_text(win, 2, "KEY BINDINGS");
    
    mvwprintw(win, 6, 20, "PLAYER 1 (WASD):");
    mvwprintw(win, 7, 25, "A - Move Left");
    mvwprintw(win, 8, 25, "D - Move Right");
    mvwprintw(win, 9, 25, "S - Move Down");
    mvwprintw(win, 10, 25, "W - Rotate");
    
    mvwprintw(win, 13, 20, "PLAYER 2 (ARROWS):");
    mvwprintw(win, 14, 25, "LEFT  - Move Left");
    mvwprintw(win, 15, 25, "RIGHT - Move Right");
    mvwprintw(win, 16, 25, "DOWN  - Move Down");
    mvwprintw(win, 17, 25, "UP    - Rotate");
    
    mvwprintw(win, 20, 20, "COMMON CONTROLS:");
    mvwprintw(win, 21, 25, "SPACE - Hard Drop");
    mvwprintw(win, 22, 25, "Q     - Return to Menu");
    
    center_text(win, 25, "Press any key to return...");
    wrefresh(win);
    
    nodelay(win, FALSE);
    wgetch(win);
    nodelay(win, TRUE);
}

// NEW FUNCTION: Render volume menu
void render_volume_menu(WINDOW* win) {
    werase(win);
    
    center_text(win, 2, "VOLUME SETTINGS");
    
    // Volume bar
    center_text(win, 8, "Volume: [");
    int vol_x = (getmaxx(win) - 20) / 2;
    wmove(win, 8, vol_x + 9);
    for (int i = 0; i < 10; i++) {
        if (i < volume / 10) {
            wattron(win, COLOR_PAIR(2));
            waddch(win, '|');
            wattroff(win, COLOR_PAIR(2));
        } else {
            waddch(win, ' ');
        }
    }
    wprintw(win, "] %d%%", volume);
    
    center_text(win, 12, "Use LEFT/RIGHT to adjust volume");
    center_text(win, 14, "Press ENTER to confirm");
    center_text(win, 16, "Press Q to return without changes");
    
    wrefresh(win);
    
    // Volume adjustment loop
    int ch;
    while (!shutdown_requested) {
        ch = getch();
        
        if (ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == '\n') {
            break;
        } else if (ch == KEY_LEFT) {
            volume = (volume - 10 + 110) % 110;
        } else if (ch == KEY_RIGHT) {
            volume = (volume + 10) % 110;
        }
        
        // Update volume display
        wmove(win, 8, vol_x + 9);
        for (int i = 0; i < 10; i++) {
            if (i < volume / 10) {
                wattron(win, COLOR_PAIR(2));
                waddch(win, '|');
                wattroff(win, COLOR_PAIR(2));
            } else {
                waddch(win, ' ');
            }
        }
        wprintw(win, "] %d%%", volume);
        wrefresh(win);
        
        usleep(100000);
    }
}

// Render the main menu (UPDATED: removed leaderboard and game info)
void render_main_menu(WINDOW* win) {
    werase(win);
   
    // Title (centered)
    wattron(win, A_BOLD | COLOR_PAIR(3));
    center_text(win, 2, "TETRIS MULTIPLAYER");
    wattroff(win, A_BOLD | COLOR_PAIR(3));
   
    // Menu options (centered)
    char* menu_options[MENU_TOTAL] = {
        "START GAME",
        "NUMBER OF PLAYERS",
        "KEY BINDS",
        "VOLUME",
        "BACKGROUND COLOR",
        "LEADERBOARD",
        "QUIT"
    };
   
    int menu_start_y = (term_rows - MENU_TOTAL * 2) / 2;
    int menu_width = 40;
    int menu_start_x = (term_cols - menu_width) / 2;
   
    for (int i = 0; i < MENU_TOTAL; i++) {
        int y = menu_start_y + i * 2;
        
        if (i == current_menu) {
            wattron(win, A_REVERSE | COLOR_PAIR(2));
        }
       
        mvwprintw(win, y, menu_start_x, "%s", menu_options[i]);
       
        if (i == current_menu) {
            wattroff(win, A_REVERSE | COLOR_PAIR(2));
        }
       
        // Show current values (right aligned)
        int value_x = menu_start_x + menu_width - 20;
        switch (i) {
            case MENU_PLAYERS:
                mvwprintw(win, y, value_x, "[%d Player%s]", num_players, num_players > 1 ? "s" : "");
                break;
            case MENU_KEYBINDS:
                mvwprintw(win, y, value_x, "[View Controls]");
                break;
            case MENU_VOLUME:
                mvwprintw(win, y, value_x, "[%d%%]", volume);
                break;
            case MENU_BACKGROUND:
                mvwprintw(win, y, value_x, "[%s]", bg_color_names[background_color]);
                break;
            case MENU_LEADERBOARD:
                mvwprintw(win, y, value_x, "[View High Scores]");
                break;
        }
    }
   
    // Instructions (centered at bottom)
    wattron(win, A_BOLD);
    center_text(win, term_rows - 2, "CONTROLS: ↑↓ Navigate | ENTER Select | Q Quit");
    wattroff(win, A_BOLD);
   
    wrefresh(win);
}

// Render game screen (UPDATED: responsive to terminal size)
void render_game_screen(WINDOW* win) {
    werase(win);
   
    // Calculate window positions for each player based on terminal size
    int player_width = 25;
    int start_x = (term_cols - (num_players * player_width)) / 2;
    int board_start_y = 2;
   
    for (int p = 0; p < num_players; p++) {
        int player_x = start_x + p * player_width;
       
        // Player header - NOW SHOWS CUSTOM NAMES
        wattron(win, A_BOLD | COLOR_PAIR(p + 1));
        mvwprintw(win, board_start_y, player_x, "%s", players[p].player_name);
        mvwprintw(win, board_start_y + 1, player_x, "Score: %d", players[p].score);
        mvwprintw(win, board_start_y + 2, player_x, "Level: %d", players[p].level);
        mvwprintw(win, board_start_y + 3, player_x, "Lines: %d", players[p].lines_cleared);
        wattroff(win, A_BOLD | COLOR_PAIR(p + 1));
       
        // Draw game board
        for (int i = 0; i < HEIGHT; i++) {
            for (int j = 0; j < WIDTH; j++) {
                char cell = players[p].grid[i][j];
                if (cell == BLOCK) {
                    wattron(win, COLOR_PAIR(players[p].color_grid[i][j]));
                }
                mvwprintw(win, board_start_y + 5 + i, player_x + j * 2, "%c", cell);
                if (cell == BLOCK) {
                    wattroff(win, COLOR_PAIR(players[p].color_grid[i][j]));
                }
            }
        }
       
        // Draw current piece
        Tetromino* piece = &players[p].current_piece;
        for (int i = 0; i < 4; i++) {
            int x = player_x + (piece->x + piece->shape[i].x) * 2;
            int y = board_start_y + 5 + piece->y + piece->shape[i].y;
            if (y >= board_start_y + 5 && piece->y + piece->shape[i].y >= 0) {
                wattron(win, COLOR_PAIR(piece->color));
                mvwprintw(win, y, x, "%c", BLOCK);
                wattroff(win, COLOR_PAIR(piece->color));
            }
        }
       
        // Game over message
        if (players[p].game_over) {
            wattron(win, A_BOLD | COLOR_PAIR(1));
            mvwprintw(win, board_start_y + 5 + HEIGHT/2, player_x + 5, "GAME OVER");
            wattroff(win, A_BOLD | COLOR_PAIR(1));
        }
    }
   
    // NEW: Display global leaderboard on the right side if there's space
    if (term_cols > 80 && global_leaderboard_enabled && score_count > 0) {
        int leaderboard_x = start_x + (num_players * player_width) + 5;
        if (leaderboard_x < term_cols - 25) {
            wattron(win, A_BOLD | COLOR_PAIR(3));
            mvwprintw(win, board_start_y, leaderboard_x, "GLOBAL LEADERBOARD");
            wattroff(win, A_BOLD | COLOR_PAIR(3));
            
            for (int i = 0; i < score_count && i < 5; i++) {
                mvwprintw(win, board_start_y + 2 + i, leaderboard_x, "%d. %s", i + 1, top_scores[i].name);
                mvwprintw(win, board_start_y + 2 + i, leaderboard_x + 15, "%d", top_scores[i].score);
            }
        }
    }
   
    // Controls reminder (centered at bottom)
    wattron(win, A_BOLD);
    center_text(win, term_rows - 2, 
        "Player1: WASD | Player2: Arrows | Space: Hard Drop | Q: Menu");
    wattroff(win, A_BOLD);
   
    wrefresh(win);
}
// Find winner
int find_winner() {
    int winner = 0;
    int max_score = players[0].score;
   
    for (int i = 1; i < num_players; i++) {
        if (players[i].score > max_score) {
            max_score = players[i].score;
            winner = i;
        }
    }
    return winner;
}

// Reset game state completely
// Reset game state completely
void reset_game_state() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        // Reset game state but KEEP player names
        char saved_name[50];
        strncpy(saved_name, players[i].player_name, 49);
        saved_name[49] = '\0';
        
        init_player_state(i);
        
        // Restore the player name
        strncpy(players[i].player_name, saved_name, 49);
        players[i].player_name[49] = '\0';
    }
}

// Check if all players are done (MISSING FUNCTION)
int all_players_done() {
    for (int i = 0; i < num_players; i++) {
        if (!players[i].game_over) return 0;
    }
    return 1;
}

// Main game function (UPDATED: includes player name input and menu return)
void start_game() {
    // Reset return to menu flag
    return_to_menu = 0;
    
    // NEW: Initialize network leaderboard
    if (global_leaderboard_enabled) {
        fetch_leaderboard(); // Get initial leaderboard
        last_leaderboard_update = 0;
        game_time = 0;
    }
    
    // Get player names before starting - THIS NOW PROPERLY UPDATES NAMES
    get_player_names();
    
    // Reset all players before starting new game
    reset_game_state();
    
    // Initialize all players
    for (int i = 0; i < num_players; i++) {
        spawn_piece(i);
    }
   
    // Create threads for each player
    for (int i = 0; i < num_players; i++) {
        // Create drop thread
        int* drop_player_id = malloc(sizeof(int));
        if (drop_player_id == NULL) continue;
        *drop_player_id = i;
        if (pthread_create(&drop_threads[i], NULL, drop_thread, drop_player_id) != 0) {
            free(drop_player_id);
        }
       
        // Create input thread  
        int* input_player_id = malloc(sizeof(int));
        if (input_player_id == NULL) continue;
        *input_player_id = i;
        if (pthread_create(&input_threads[i], NULL, input_thread, input_player_id) != 0) {
            free(input_player_id);
        }
    }
   
    // Main game loop
    WINDOW* game_win = create_centered_window(term_rows, term_cols);
    wbkgd(game_win, COLOR_PAIR(background_color + 10));
   
    while (!shutdown_requested && !return_to_menu && !all_players_done()) {
        render_game_screen(game_win);
        
        // NEW: Update global leaderboard periodically
        if (global_leaderboard_enabled) {
            game_time++;
            if (game_time - last_leaderboard_update > 180) { // Update every 3 seconds at 60 FPS
                if (update_leaderboard_nonblocking() == 0) {
                    last_leaderboard_update = game_time;
                }
            }
        }
        
        usleep(16666); // ~60 FPS
    }
   
    // Only show game over screen if game ended naturally (not by pressing 'q')
    if (!return_to_menu) {
        // NEW: Submit scores to global leaderboard
        if (global_leaderboard_enabled) {
            for (int i = 0; i < num_players; i++) {
                if (players[i].score > 0) { // Only submit if they actually scored
                    submit_score(players[i].player_name, players[i].score);
                }
            }
            // Refresh leaderboard to show new scores
            fetch_leaderboard();
        }
        
        // Game over screen
        werase(game_win);
       
        if (num_players > 1) {
            int winner = find_winner();
            wattron(game_win, A_BOLD | COLOR_PAIR(3));
            center_text(game_win, 10, "GAME OVER!");
            mvwprintw(game_win, 12, (term_cols - 40) / 2, "WINNER: %s with %d points!",
                      players[winner].player_name, players[winner].score);
            wattroff(game_win, A_BOLD | COLOR_PAIR(3));
           
            // Add winner to leaderboard (only if they scored) - NOW WITH CUSTOM NAME
            add_to_leaderboard(players[winner].player_name, players[winner].score);
        } else {
            wattron(game_win, A_BOLD | COLOR_PAIR(3));
            center_text(game_win, 10, "GAME OVER!");
            mvwprintw(game_win, 12, (term_cols - 20) / 2, "Final Score: %d", players[0].score);
            wattroff(game_win, A_BOLD | COLOR_PAIR(3));
           
            // Add score to leaderboard for single player (only if scored) - NOW WITH CUSTOM NAME
            add_to_leaderboard(players[0].player_name, players[0].score);
        }
       
        // NEW: Show global leaderboard status
        if (global_leaderboard_enabled) {
            if (score_count > 0) {
                center_text(game_win, 14, "Score submitted to global leaderboard!");
                mvwprintw(game_win, 15, (term_cols - 20) / 2, "Global #1: %s - %d", 
                         top_scores[0].name, top_scores[0].score);
            } else {
                center_text(game_win, 14, "Could not connect to global leaderboard");
            }
        }
       
        center_text(game_win, 18, "Press any key to continue...");
        wrefresh(game_win);
       
        // Wait for key press
        nodelay(game_win, FALSE);
        wgetch(game_win);
        nodelay(game_win, TRUE);
    }
   
    // Cleanup threads
    for (int i = 0; i < num_players; i++) {
        pthread_cancel(drop_threads[i]);
        pthread_cancel(input_threads[i]);
        pthread_join(drop_threads[i], NULL);
        pthread_join(input_threads[i], NULL);
        pthread_mutex_destroy(&players[i].mutex);
    }
   
    delwin(game_win);
}
// Initialize colors
void init_colors() {
    start_color();
   
    // Standard color pairs 1-7
    for (int i = 0; i < 7; i++) {
        init_pair(i + 1, color_pairs[i][0], color_pairs[i][1]);
    }
   
    // Background color pairs 10-16
    for (int i = 0; i < 7; i++) {
        init_pair(i + 10, COLOR_WHITE, bg_colors[i]);
    }
}

int main() {
    // Setup signal handling
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
   
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
   
    // Get terminal dimensions
    get_terminal_dimensions();
   
    // Initialize colors
    init_colors();
   
    // Load leaderboard
    load_leaderboard();
   
    // Seed random number generator
    srand(time(NULL));
   
    // Initialize player states with default names
    for (int i = 0; i < MAX_PLAYERS; i++) {
        init_player_state(i);
    }
   
    // Main menu loop
    WINDOW* main_win = create_centered_window(term_rows, term_cols);
    wbkgd(main_win, COLOR_PAIR(background_color + 10));
   
    while (!shutdown_requested) {
        // Update terminal dimensions in case of resize
        get_terminal_dimensions();
        wresize(main_win, term_rows, term_cols);
        mvwin(main_win, (LINES - term_rows) / 2, (COLS - term_cols) / 2);
        
        render_main_menu(main_win);
       
        int ch = getch();
        switch (ch) {
            case 'q':
                shutdown_requested = 1;
                break;
            case KEY_UP:
                current_menu = (current_menu - 1 + MENU_TOTAL) % MENU_TOTAL;
                break;
            case KEY_DOWN:
                current_menu = (current_menu + 1) % MENU_TOTAL;
                break;
            case '\n': // Enter key
                switch (current_menu) {
                    case MENU_START:
                        start_game();
                        break;
                    case MENU_PLAYERS:
                        num_players = (num_players % MAX_PLAYERS) + 1;
                        break;
                    case MENU_KEYBINDS:
                        render_keybinds_menu(main_win);
                        break;
                    case MENU_VOLUME:
                        render_volume_menu(main_win);
                        break;
                    case MENU_BACKGROUND:
                        background_color = (background_color + 1) % 7;
                        wbkgd(main_win, COLOR_PAIR(background_color + 10));
                        break;
                    case MENU_LEADERBOARD:
                        display_full_leaderboard(main_win);
                        break;
                    case MENU_QUIT:
                        shutdown_requested = 1;
                        break;
                }
                break;
            case KEY_RESIZE:
                // Handle terminal resize
                get_terminal_dimensions();
                break;
        }
       
        usleep(100000); // 100ms delay
    }
   
    // Cleanup
    delwin(main_win);
    endwin();
   
    printf("Thanks for playing Multiplayer Tetris!\n");
    return 0;
}
