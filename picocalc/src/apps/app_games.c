/*
 * apps/app_games.c — Games and toys.
 */
#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "syscall.h"

#define SPRITE_FILE      "/SPRITE.TXT"
#define SPRITE_SIDE      16
#define SNAKE_W          16
#define SNAKE_H          10
#define SNAKE_MAX_CELLS  (SNAKE_W * SNAKE_H)

static uint32_t games_rand_u32(void) {
    static uint32_t state = 0xA5A5F00Du;
    state ^= (uint32_t)sys_time_ms() + 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void app_dice(const char *arg) {
    char count_tok[16];
    char sides_tok[16];
    const char *s = next_token(arg, count_tok, sizeof count_tok);
    next_token(s, sides_tok, sizeof sides_tok);

    int count = *count_tok ? atoi(count_tok) : 1;
    int sides = *sides_tok ? atoi(sides_tok) : 6;
    if (count < 1 || count > 20 || sides < 2 || sides > 1000) {
        print_line("usage: dice [COUNT] [SIDES]");
        return;
    }

    int total = 0;
    for (int i = 0; i < count; i++) {
        int roll = (int)(games_rand_u32() % (uint32_t)sides) + 1;
        char out[64];
        snprintf(out, sizeof out, "roll %d: %d", i + 1, roll);
        print_line(out);
        total += roll;
    }
    if (count > 1) {
        char out[64];
        snprintf(out, sizeof out, "total: %d", total);
        print_line(out);
    }
}

void app_coin(const char *arg) {
    char count_tok[16];
    next_token(arg, count_tok, sizeof count_tok);
    int count = *count_tok ? atoi(count_tok) : 1;
    if (count < 1 || count > 32) {
        print_line("usage: coin [COUNT]");
        return;
    }

    int heads = 0;
    for (int i = 0; i < count; i++) {
        bool is_heads = (games_rand_u32() & 1u) != 0;
        if (is_heads) heads++;
        print_line(is_heads ? "HEADS" : "TAILS");
    }
    if (count > 1) {
        char out[64];
        snprintf(out, sizeof out, "heads: %d  tails: %d", heads, count - heads);
        print_line(out);
    }
}

void app_guess(const char *arg) {
    (void)arg;
    int target = (int)(games_rand_u32() % 100u) + 1;
    int turns = 0;
    print_line("Guess a number from 1 to 100. Type q to quit.");

    while (1) {
        char line[32];
        if (app_read_line("guess> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        if (ci_eq(line, "q") || ci_eq(line, "quit") || ci_eq(line, "exit")) break;

        int guess = atoi(line);
        if (guess < 1 || guess > 100) {
            print_line("Enter a number from 1 to 100.");
            continue;
        }

        turns++;
        if (guess < target) print_line("Too low");
        else if (guess > target) print_line("Too high");
        else {
            char out[64];
            snprintf(out, sizeof out, "Correct in %d turns", turns);
            print_line(out);
            return;
        }
    }
}

static void snake_spawn_food(int *food_x, int *food_y, const int *sx, const int *sy, int len) {
    while (1) {
        int x = (int)(games_rand_u32() % (uint32_t)SNAKE_W);
        int y = (int)(games_rand_u32() % (uint32_t)SNAKE_H);
        bool clash = false;
        for (int i = 0; i < len; i++) {
            if (sx[i] == x && sy[i] == y) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            *food_x = x;
            *food_y = y;
            return;
        }
    }
}

static void snake_render(const int *sx, const int *sy, int len, int food_x, int food_y, int score) {
    sys_clear();
    char out[64];
    snprintf(out, sizeof out, "Snake score: %d", score);
    print_line(out);
    print_line("Use w/a/s/d to move, q to quit");

    for (int y = -1; y <= SNAKE_H; y++) {
        char line[64];
        int pos = 0;
        for (int x = -1; x <= SNAKE_W; x++) {
            char ch = ' ';
            if (y < 0 || y >= SNAKE_H || x < 0 || x >= SNAKE_W) {
                ch = '#';
            } else if (x == food_x && y == food_y) {
                ch = '*';
            }
            for (int i = 0; i < len && x >= 0 && y >= 0 && x < SNAKE_W && y < SNAKE_H; i++) {
                if (sx[i] == x && sy[i] == y) {
                    ch = (i == 0) ? '@' : 'o';
                    break;
                }
            }
            if (pos + 1 < (int)sizeof line) line[pos++] = ch;
        }
        line[pos] = '\0';
        print_line(line);
    }
}

void app_snake(const char *arg) {
    (void)arg;
    int sx[SNAKE_MAX_CELLS];
    int sy[SNAKE_MAX_CELLS];
    int len = 3;
    int dx = 1, dy = 0;
    int food_x = 0, food_y = 0;
    int score = 0;
    uint32_t speed_ms = 200; /* ms per tick — gets faster */

    /* Load persistent high score */
    int hi_score = 0;
    {
        char hbuf[32];
        int hn = sys_fread("/HISCORE.TXT", hbuf, sizeof hbuf - 1);
        if (hn > 0) { hbuf[hn] = '\0'; hi_score = atoi(hbuf); }
    }

    sx[0] = 4; sy[0] = 4;
    sx[1] = 3; sy[1] = 4;
    sx[2] = 2; sy[2] = 4;
    snake_spawn_food(&food_x, &food_y, sx, sy, len);
    if (hi_score > 0) {
        char hmsg[48];
        snprintf(hmsg, sizeof hmsg, "High score: %d", hi_score);
        print_line(hmsg);
    }

    while (1) {
        snake_render(sx, sy, len, food_x, food_y, score);

        /* Real-time: poll for input during the tick interval */
        uint32_t start = sys_time_ms();
        int last_dir = -1;
        while (sys_time_ms() - start < speed_ms) {
            watchdog_update();
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 'Q' || ch == 0x03) goto snake_done;
            if ((ch == 'w' || ch == 'k') && dy != 1)  last_dir = 0;
            else if ((ch == 's' || ch == 'j') && dy != -1) last_dir = 1;
            else if ((ch == 'a' || ch == 'h') && dx != 1)  last_dir = 2;
            else if ((ch == 'd' || ch == 'l') && dx != -1) last_dir = 3;
            /* Arrow keys */
            else if (ch == 0xB5 && dy != 1)  last_dir = 0;
            else if (ch == 0xB4 && dy != -1) last_dir = 1;
            else if (ch == 0xB6 && dx != 1)  last_dir = 2;
            else if (ch == 0xB7 && dx != -1) last_dir = 3;
            sleep_ms(10);
        }

        if (last_dir == 0) { dx = 0; dy = -1; }
        else if (last_dir == 1) { dx = 0; dy = 1; }
        else if (last_dir == 2) { dx = -1; dy = 0; }
        else if (last_dir == 3) { dx = 1; dy = 0; }

        int nx = sx[0] + dx;
        int ny = sy[0] + dy;
        if (nx < 0 || nx >= SNAKE_W || ny < 0 || ny >= SNAKE_H) {
            print_line("snake: wall hit; game over");
            goto snake_done;
        }
        for (int i = 0; i < len; i++) {
            if (sx[i] == nx && sy[i] == ny) {
                print_line("snake: self collision; game over");
                goto snake_done;
            }
        }

        bool grow = (nx == food_x && ny == food_y);
        int new_len = len + (grow && len < SNAKE_MAX_CELLS ? 1 : 0);
        for (int i = new_len - 1; i > 0; i--) {
            sx[i] = sx[i - 1];
            sy[i] = sy[i - 1];
        }
        sx[0] = nx;
        sy[0] = ny;
        len = new_len;

        if (grow) {
            score++;
            snake_spawn_food(&food_x, &food_y, sx, sy, len);
            /* Speed up every 5 points */
            if (speed_ms > 80 && score % 5 == 0) speed_ms -= 20;
        }
    }
snake_done:
    if (score > hi_score) {
        char sbuf[32];
        snprintf(sbuf, sizeof sbuf, "%d", score);
        sys_fwrite("/HISCORE.TXT", sbuf, (uint32_t)strlen(sbuf));
        char msg[48];
        snprintf(msg, sizeof msg, "New high score: %d!", score);
        print_line(msg);
    }
    print_line("Press any key...");
    (void)sys_getchar();
    sys_clear();
}

static void sprite_render(const uint8_t *pixels, int cur_x, int cur_y, const char *path) {
    sys_clear();
    sys_print("Sprite editor\n");
    if (path && *path) {
        sys_print(path);
        sys_putchar('\n');
    }
    sys_print("w/a/s/d move, x toggle, c clear, p save, l load, q quit\n\n");

    for (int y = 0; y < SPRITE_SIDE; y++) {
        char line[64];
        int pos = 0;
        for (int x = 0; x < SPRITE_SIDE; x++) {
            bool on = pixels[y * SPRITE_SIDE + x] != 0;
            char ch = on ? '#' : '.';
            if (x == cur_x && y == cur_y) ch = on ? '*' : '@';
            line[pos++] = ch;
        }
        line[pos] = '\0';
        print_line(line);
    }
}

void app_sprite(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) copy_cstr(path, sizeof path, SPRITE_FILE);

    char target[APP_TOKEN_MAX];
    app_make_abs(path, target, sizeof target);

    static uint8_t pixels[SPRITE_SIDE * SPRITE_SIDE];
    int cur_x = 0, cur_y = 0;

    while (1) {
        sprite_render(pixels, cur_x, cur_y, target);
        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if ((ch == 'w' || ch == 'W') && cur_y > 0) cur_y--;
        else if ((ch == 's' || ch == 'S') && cur_y < SPRITE_SIDE - 1) cur_y++;
        else if ((ch == 'a' || ch == 'A') && cur_x > 0) cur_x--;
        else if ((ch == 'd' || ch == 'D') && cur_x < SPRITE_SIDE - 1) cur_x++;
        else if (ch == 'x' || ch == 'X' || ch == ' ') {
            size_t idx = (size_t)cur_y * SPRITE_SIDE + (size_t)cur_x;
            pixels[idx] ^= 1u;
        } else if (ch == 'c' || ch == 'C') {
            memset(pixels, 0, sizeof pixels);
            print_line("sprite: cleared");
        } else if (ch == 'p' || ch == 'P') {
            char buf[(SPRITE_SIDE + 1) * SPRITE_SIDE + 1];
            size_t pos = 0;
            for (int y = 0; y < SPRITE_SIDE; y++) {
                for (int x = 0; x < SPRITE_SIDE; x++) {
                    buf[pos++] = pixels[y * SPRITE_SIDE + x] ? '#' : '.';
                }
                buf[pos++] = '\n';
            }
            buf[pos] = '\0';
            if (sys_fwrite(target, buf, (uint32_t)pos) < 0) print_line("sprite: save failed");
            else print_line("sprite: saved");
        } else if (ch == 'l' || ch == 'L') {
            char buf[(SPRITE_SIDE + 1) * SPRITE_SIDE + 1];
            int n = sys_fread(target, buf, (uint32_t)(sizeof buf - 1));
            if (n < 0) {
                print_line("sprite: load failed");
            } else {
                buf[n] = '\0';
                int idx = 0;
                for (int y = 0; y < SPRITE_SIDE; y++) {
                    for (int x = 0; x < SPRITE_SIDE; x++) {
                        while (idx < n && (buf[idx] == '\n' || buf[idx] == '\r')) idx++;
                        pixels[y * SPRITE_SIDE + x] = (idx < n && buf[idx] == '#') ? 1u : 0u;
                        if (idx < n) idx++;
                    }
                }
                print_line("sprite: loaded");
            }
        }
    }
    sys_clear();
}

void app_games(const char *arg) {
    char cmd[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);

    if (!*cmd) {
        print_line("Games pack");
        print_line("  1 dice rolls");
        print_line("  2 coin flips");
        print_line("  3 guess-the-number");
        print_line("  4 snake");
        print_line("  q return");
        int ch = sys_getchar();
        sys_putchar('\n');
        if (ch == '1') app_dice("");
        else if (ch == '2') app_coin("");
        else if (ch == '3') app_guess("");
        else if (ch == '4') app_snake("");
        return;
    }

    if (ci_eq(cmd, "dice")) app_dice(s);
    else if (ci_eq(cmd, "coin")) app_coin(s);
    else if (ci_eq(cmd, "guess")) app_guess(s);
    else if (ci_eq(cmd, "snake")) app_snake(s);
    else print_line("usage: games [dice|coin|guess|snake]");
}

/* ============================================================
 * Minesweeper
 * ============================================================ */
#define MS_W    9
#define MS_H    9
#define MS_MINES 10

static void ms_reveal_flood(uint8_t state[MS_H][MS_W],
                             const uint8_t board[MS_H][MS_W],
                             int rx, int ry) {
    int stk[MS_W * MS_H][2];
    int top = 0;
    stk[top][0] = rx; stk[top][1] = ry; top++;
    while (top > 0) {
        top--;
        int x = stk[top][0], y = stk[top][1];
        if (x < 0 || x >= MS_W || y < 0 || y >= MS_H) continue;
        if (state[y][x] != 0) continue;
        state[y][x] = 1;
        if (board[y][x] == 0) {
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (dy == 0 && dx == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H
                            && state[ny][nx] == 0 && top < MS_W * MS_H - 1) {
                        stk[top][0] = nx; stk[top][1] = ny; top++;
                    }
                }
        }
    }
}

void app_minesweeper(const char *arg) {
    (void)arg;
    static uint8_t state[MS_H][MS_W];  /* 0=hidden, 1=revealed, 2=flagged */
    static uint8_t board[MS_H][MS_W];  /* 9=mine, 0-8=adjacent count */
    bool game_over, won;

restart:
    memset(state, 0, sizeof state);
    memset(board, 0, sizeof board);

    /* Place mines */
    int placed = 0;
    while (placed < MS_MINES) {
        int x = (int)(games_rand_u32() % MS_W);
        int y = (int)(games_rand_u32() % MS_H);
        if (board[y][x] != 9) { board[y][x] = 9; placed++; }
    }
    /* Compute adjacency counts */
    for (int y = 0; y < MS_H; y++) {
        for (int x = 0; x < MS_W; x++) {
            if (board[y][x] == 9) continue;
            int cnt = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H
                            && board[ny][nx] == 9) cnt++;
                }
            board[y][x] = (uint8_t)cnt;
        }
    }

    int cx = 0, cy = 0;
    game_over = false; won = false;

    while (1) {
        watchdog_update();
        sys_clear();
        int flags = 0;
        for (int y = 0; y < MS_H; y++)
            for (int x = 0; x < MS_W; x++)
                if (state[y][x] == 2) flags++;

        char hdr[64];
        snprintf(hdr, sizeof hdr, "Minesweeper  mines:%d  flags:%d", MS_MINES, flags);
        print_line(hdr);
        print_line("Arrows=move  Enter=reveal  f=flag  r=new  q=quit");

        for (int y = 0; y < MS_H; y++) {
            char row[MS_W * 2 + 4];
            int ri = 0;
            for (int x = 0; x < MS_W; x++) {
                row[ri++] = (x == cx && y == cy) ? '[' : ' ';
                if (game_over && board[y][x] == 9)
                    row[ri++] = '*';
                else if (state[y][x] == 0)
                    row[ri++] = '.';
                else if (state[y][x] == 2)
                    row[ri++] = 'F';
                else if (board[y][x] == 0)
                    row[ri++] = ' ';
                else
                    row[ri++] = (char)('0' + board[y][x]);
                if (x == cx && y == cy) row[ri++] = ']';
            }
            row[ri] = '\0';
            print_line(row);
        }

        if (won)       { print_line("*** YOU WIN! ***  r=new  q=quit"); }
        if (game_over && !won) { print_line("*** BOOM! ***  r=new  q=quit"); }

        int ch = sys_getchar();
        if (ch == 'q' || ch == 0x03) return;
        if (ch == 'r') goto restart;

        if (!game_over) {
            if      (ch == 0xB5 && cy > 0)        cy--;
            else if (ch == 0xB4 && cy < MS_H - 1) cy++;
            else if (ch == 0xB6 && cx > 0)        cx--;
            else if (ch == 0xB7 && cx < MS_W - 1) cx++;
            else if (ch == 'f') {
                if      (state[cy][cx] == 0) state[cy][cx] = 2;
                else if (state[cy][cx] == 2) state[cy][cx] = 0;
            } else if (ch == '\r' || ch == '\n') {
                if (state[cy][cx] == 2) continue;
                if (board[cy][cx] == 9) {
                    state[cy][cx] = 1;
                    game_over = true;
                } else {
                    ms_reveal_flood(state, board, cx, cy);
                    int still_hidden = 0;
                    for (int y = 0; y < MS_H; y++)
                        for (int x = 0; x < MS_W; x++)
                            if (state[y][x] == 0 && board[y][x] != 9) still_hidden++;
                    if (still_hidden == 0) { won = true; game_over = true; }
                }
            }
        }
    }
}

/* ============================================================
 * New programs: life, tetris, mandelbrot, piano, forth
 * ============================================================ */

/* ============================================================
 * New programs: life, tetris, mandelbrot, piano, forth
 * ============================================================ */

/* 2048 */
#define G2048_SZ 4

static void g2048_add_tile(uint16_t board[G2048_SZ][G2048_SZ]) {
    int ex[G2048_SZ * G2048_SZ], ey[G2048_SZ * G2048_SZ];
    int ne = 0;
    for (int y = 0; y < G2048_SZ; y++)
        for (int x = 0; x < G2048_SZ; x++)
            if (board[y][x] == 0) { ex[ne] = x; ey[ne] = y; ne++; }
    if (ne == 0) return;
    int i = (int)(games_rand_u32() % (uint32_t)ne);
    board[ey[i]][ex[i]] = (games_rand_u32() % 10 < 9) ? 2 : 4;
}

void app_2048(const char *arg) {
    (void)arg;
    static uint16_t board[G2048_SZ][G2048_SZ];
    int score = 0;

restart:
    memset(board, 0, sizeof board);
    score = 0;
    g2048_add_tile(board); g2048_add_tile(board);

    while (1) {
        watchdog_update();
        sys_clear();
        char hdr2048[32];
        snprintf(hdr2048, sizeof hdr2048, "2048  score:%d", score);
        print_line(hdr2048);
        print_line("Arrows=move  r=new  q=quit");
        for (int y = 0; y < G2048_SZ; y++) {
            char row[G2048_SZ * 7 + 2];
            int ri = 0;
            row[ri++] = '|';
            for (int x = 0; x < G2048_SZ; x++) {
                if (board[y][x])
                    ri += snprintf(row + ri, (size_t)(sizeof row - ri - 1),
                                   "%5u|", (unsigned)board[y][x]);
                else {
                    memcpy(row + ri, "     |", 6); ri += 6;
                }
            }
            row[ri] = '\0';
            print_line(row);
        }

        bool has_2048 = false, has_empty = false, has_merge = false;
        for (int y = 0; y < G2048_SZ; y++)
            for (int x = 0; x < G2048_SZ; x++) {
                if (board[y][x] == 2048) has_2048 = true;
                if (board[y][x] == 0)    has_empty = true;
                if (x + 1 < G2048_SZ && board[y][x] == board[y][x+1] && board[y][x]) has_merge = true;
                if (y + 1 < G2048_SZ && board[y][x] == board[y+1][x] && board[y][x]) has_merge = true;
            }
        if (has_2048) { print_line("*** 2048! ***  r=new  q=quit"); }
        if (!has_empty && !has_merge) { print_line("Game over!  r=new  q=quit"); }

        int ch = sys_getchar();
        if (ch == 'q' || ch == 0x03) return;
        if (ch == 'r') goto restart;
        if (ch != 0xB5 && ch != 0xB4 && ch != 0xB6 && ch != 0xB7) continue;

        bool moved = false;

        if (ch == 0xB6 || ch == 0xB7) { /* left / right */
            int forward = (ch == 0xB6);  /* 0xB6=left, slide toward x=0 */
            for (int y = 0; y < G2048_SZ; y++) {
                uint16_t v[G2048_SZ];
                int vi = 0;
                if (forward) {
                    for (int x = 0; x < G2048_SZ; x++) if (board[y][x]) v[vi++] = board[y][x];
                } else {
                    for (int x = G2048_SZ - 1; x >= 0; x--) if (board[y][x]) v[vi++] = board[y][x];
                }
                for (int i = 0; i + 1 < vi; i++) {
                    if (v[i] == v[i+1]) {
                        v[i] = (uint16_t)(v[i] * 2); score += v[i];
                        for (int j = i+1; j+1 < vi; j++) v[j] = v[j+1];
                        vi--;
                    }
                }
                while (vi < G2048_SZ) v[vi++] = 0;
                if (forward) {
                    for (int x = 0; x < G2048_SZ; x++) { if (board[y][x] != v[x]) moved = true; board[y][x] = v[x]; }
                } else {
                    for (int x = G2048_SZ - 1; x >= 0; x--) {
                        int i = G2048_SZ - 1 - x;
                        if (board[y][x] != v[i]) moved = true;
                        board[y][x] = v[i];
                    }
                }
            }
        } else { /* up / down */
            int upward = (ch == 0xB5);
            for (int x = 0; x < G2048_SZ; x++) {
                uint16_t v[G2048_SZ];
                int vi = 0;
                if (upward) {
                    for (int y = 0; y < G2048_SZ; y++) if (board[y][x]) v[vi++] = board[y][x];
                } else {
                    for (int y = G2048_SZ - 1; y >= 0; y--) if (board[y][x]) v[vi++] = board[y][x];
                }
                for (int i = 0; i + 1 < vi; i++) {
                    if (v[i] == v[i+1]) {
                        v[i] = (uint16_t)(v[i] * 2); score += v[i];
                        for (int j = i+1; j+1 < vi; j++) v[j] = v[j+1];
                        vi--;
                    }
                }
                while (vi < G2048_SZ) v[vi++] = 0;
                if (upward) {
                    for (int y = 0; y < G2048_SZ; y++) { if (board[y][x] != v[y]) moved = true; board[y][x] = v[y]; }
                } else {
                    for (int y = G2048_SZ - 1; y >= 0; y--) {
                        int i = G2048_SZ - 1 - y;
                        if (board[y][x] != v[i]) moved = true;
                        board[y][x] = v[i];
                    }
                }
            }
        }
        if (moved) g2048_add_tile(board);
    }
}

/* Conway's Game of Life */
#define LIFE_W 38
#define LIFE_H 16
void app_life(const char *arg) {
    (void)arg;
    static uint8_t grid[LIFE_H][LIFE_W];
    static uint8_t next[LIFE_H][LIFE_W];

    /* Random seed */
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++)
            grid[y][x] = (games_rand_u32() % 3 == 0) ? 1 : 0;

    int gen = 0;
    while (1) {
        sys_clear();
        char hdr[48];
        snprintf(hdr, sizeof hdr, "Game of Life  gen:%d  q=quit r=reset", gen);
        print_line(hdr);

        for (int y = 0; y < LIFE_H; y++) {
            char row[LIFE_W + 1];
            for (int x = 0; x < LIFE_W; x++)
                row[x] = grid[y][x] ? '#' : '.';
            row[LIFE_W] = '\0';
            print_line(row);
        }

        /* Check for quit or let the gen advance */
        for (int w = 0; w < 3; w++) {
            watchdog_update();
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 0x03) { sys_clear(); return; }
            if (ch == 'r') {
                for (int y2 = 0; y2 < LIFE_H; y2++)
                    for (int x2 = 0; x2 < LIFE_W; x2++)
                        grid[y2][x2] = (games_rand_u32() % 3 == 0) ? 1 : 0;
                gen = 0;
                goto life_next;
            }
            sleep_ms(100);
        }

        /* Next generation */
        for (int y = 0; y < LIFE_H; y++) {
            for (int x = 0; x < LIFE_W; x++) {
                int neighbors = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int ny = (y + dy + LIFE_H) % LIFE_H;
                        int nx = (x + dx + LIFE_W) % LIFE_W;
                        neighbors += grid[ny][nx];
                    }
                }
                if (grid[y][x])
                    next[y][x] = (neighbors == 2 || neighbors == 3) ? 1 : 0;
                else
                    next[y][x] = (neighbors == 3) ? 1 : 0;
            }
        }
        memcpy(grid, next, sizeof grid);
        gen++;
        life_next:;
    }
}

/* Tetris */
#define TETRIS_W 10
#define TETRIS_H 18
#define TETRIS_TYPES 7

static const uint16_t _tetris_shapes[TETRIS_TYPES][4] = {
    /* I */ {0x0F00, 0x2222, 0x00F0, 0x4444},
    /* O */ {0x6600, 0x6600, 0x6600, 0x6600},
    /* T */ {0x0E40, 0x4C40, 0x4E00, 0x4640},
    /* S */ {0x06C0, 0x8C40, 0x6C00, 0x4620},
    /* Z */ {0x0C60, 0x4C80, 0xC600, 0x2640},
    /* J */ {0x0E80, 0xC440, 0x2E00, 0x44C0},
    /* L */ {0x0E20, 0x44C0, 0x8E00, 0xC440},
};

static bool tetris_cell(uint16_t shape, int r, int c) {
    return (shape & (0x8000u >> (r * 4 + c))) != 0;
}

void app_tetris(const char *arg) {
    (void)arg;
    static uint8_t board[TETRIS_H][TETRIS_W];
    memset(board, 0, sizeof board);
    int score = 0;
    int type = (int)(games_rand_u32() % TETRIS_TYPES);
    int rot = 0, px = 3, py = 0;
    uint32_t tick_ms = 500;
    bool game_over = false;

    while (!game_over) {
        /* Render */
        sys_clear();
        char hdr[48];
        snprintf(hdr, sizeof hdr, "Tetris  score:%d  q=quit", score);
        print_line(hdr);

        /* Merge current piece temporarily */
        uint8_t disp[TETRIS_H][TETRIS_W];
        memcpy(disp, board, sizeof board);
        uint16_t shape = _tetris_shapes[type][rot % 4];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (tetris_cell(shape, r, c) && py + r >= 0 &&
                    py + r < TETRIS_H && px + c >= 0 && px + c < TETRIS_W)
                    disp[py + r][px + c] = (uint8_t)(type + 1);

        for (int y = 0; y < TETRIS_H; y++) {
            char row[TETRIS_W + 3];
            row[0] = '|';
            for (int x = 0; x < TETRIS_W; x++)
                row[x + 1] = disp[y][x] ? '#' : ' ';
            row[TETRIS_W + 1] = '|';
            row[TETRIS_W + 2] = '\0';
            print_line(row);
        }
        {
            char bot[TETRIS_W + 3];
            bot[0] = '+';
            for (int x = 0; x < TETRIS_W; x++) bot[x + 1] = '-';
            bot[TETRIS_W + 1] = '+';
            bot[TETRIS_W + 2] = '\0';
            print_line(bot);
        }

        /* Wait and collect input */
        uint32_t start = sys_time_ms();
        int move = -1;
        while (sys_time_ms() - start < tick_ms) {
            watchdog_update();
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 0x03) goto tetris_end;
            if (ch == 'a' || ch == 0xB6) move = 0; /* left */
            else if (ch == 'd' || ch == 0xB7) move = 1; /* right */
            else if (ch == 's' || ch == 0xB4) move = 2; /* down */
            else if (ch == 'w' || ch == 0xB5 || ch == ' ') move = 3; /* rotate */
            sleep_ms(20);
        }

        /* Apply move */
        int npx = px, npy = py, nrot = rot;
        if (move == 0) npx--;
        else if (move == 1) npx++;
        else if (move == 2) npy++;
        else if (move == 3) nrot = (rot + 1) % 4;

        /* Collision check for move */
        bool ok = true;
        uint16_t ns = _tetris_shapes[type][nrot % 4];
        for (int r = 0; r < 4 && ok; r++)
            for (int c = 0; c < 4 && ok; c++)
                if (tetris_cell(ns, r, c)) {
                    int ny = npy + r, nx = npx + c;
                    if (nx < 0 || nx >= TETRIS_W || ny >= TETRIS_H) ok = false;
                    else if (ny >= 0 && board[ny][nx]) ok = false;
                }
        if (ok && move != 2) { px = npx; py = npy; rot = nrot; }
        if (ok && move == 2) { py = npy; }

        /* Gravity: try to fall */
        ok = true;
        shape = _tetris_shapes[type][rot % 4];
        for (int r = 0; r < 4 && ok; r++)
            for (int c = 0; c < 4 && ok; c++)
                if (tetris_cell(shape, r, c)) {
                    int ny = py + 1 + r, nx = px + c;
                    if (ny >= TETRIS_H) ok = false;
                    else if (ny >= 0 && board[ny][nx]) ok = false;
                }

        if (ok) {
            py++;
        } else {
            /* Lock piece */
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    if (tetris_cell(shape, r, c) && py + r >= 0 &&
                        py + r < TETRIS_H && px + c >= 0 && px + c < TETRIS_W)
                        board[py + r][px + c] = (uint8_t)(type + 1);

            /* Clear lines */
            for (int y = TETRIS_H - 1; y >= 0; y--) {
                bool full = true;
                for (int x = 0; x < TETRIS_W; x++)
                    if (!board[y][x]) { full = false; break; }
                if (full) {
                    memmove(board[1], board[0], (size_t)y * TETRIS_W);
                    memset(board[0], 0, TETRIS_W);
                    score += 10;
                    y++; /* recheck same row */
                }
            }

            /* New piece */
            type = (int)(games_rand_u32() % TETRIS_TYPES);
            rot = 0; px = 3; py = 0;

            /* Check game over */
            shape = _tetris_shapes[type][0];
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    if (tetris_cell(shape, r, c) && py + r >= 0 && board[py + r][px + c])
                        game_over = true;

            /* Speed up */
            if (tick_ms > 100 && score % 50 == 0) tick_ms -= 30;
        }
    }

tetris_end:
    {
        char msg[48];
        snprintf(msg, sizeof msg, "Game Over! Score: %d", score);
        print_line(msg);
    }
    print_line("Press any key...");
    (void)sys_getchar();
    sys_clear();
}
