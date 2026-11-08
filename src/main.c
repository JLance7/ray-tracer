/* IRON VEIL - an original retro first-person game, written in C11.
 *
 * Renderer: DDA raycasting of a tile map, perspective floor/ceiling casting,
 * procedurally textured walls and depth-tested billboard sprites. This is a
 * 2.5D raycaster, rather than a general-purpose physical ray tracer.
 *
 * Coordinates: map cells are one world unit square. Angles are radians.
 * Framebuffer pixels are 0x00RRGGBB in a top-down Windows 32-bit DIB.
 * Reading order: data -> gameplay -> software drawing -> tests -> Win32 host.
 */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>

enum {
    SCREEN_W = 480,
    SCREEN_H = 300,
    VIEW_H = 240,
    MAP_SIZE = 20,
    MAX_ENEMIES = 8,
    MAX_BULLETS = 80,
    MAX_ITEMS = 12,
    SPRITE_SIZE = 64
};
enum { ITEM_CORE, ITEM_HEALTH, ITEM_AMMO };
enum { MODE_TITLE, MODE_PLAY, MODE_DEAD, MODE_WIN };
static const double PI = 3.141592653589793;
static const uint32_t INK = 0x101821, CREAM = 0xece2cb, GOLD = 0xefbf71;
static const uint32_t MINT = 0x8fdbc2, RED = 0xe97579, MUTED = 0x8eaaae;

typedef struct {
    double x, y;
} Vec2;
typedef struct {
    Vec2 position;
    int health, boss;
    double attack_timer, flash_timer;
} Enemy;
typedef struct {
    Vec2 position, velocity;
    double life;
    int damage;
} Bullet;
typedef struct {
    Vec2 position;
    int type, collected;
} Item;
typedef struct {
    double distance;
    int tile, side;
    double wall_u;
} RayHit;
typedef struct {
    int tiles[MAP_SIZE][MAP_SIZE];
    unsigned char explored[MAP_SIZE][MAP_SIZE];
    Vec2 player;
    double angle, time, recoil, fire_cooldown, reload_timer, injury_timer, message_timer;
    int health, magazine, reserve, cores, kills, mode, paused, map_open, moving;
    Enemy enemies[MAX_ENEMIES];
    int enemy_count;
    Bullet bullets[MAX_BULLETS];
    int bullet_count;
    Item items[MAX_ITEMS];
    int item_count;
    char message[128];
} Game;

static Game game;
static uint32_t framebuffer[SCREEN_W * SCREEN_H];
static double wall_depth[SCREEN_W];
static uint32_t sprites[7][SPRITE_SIZE * SPRITE_SIZE];
static wchar_t project_root[32768];
static int mouse_look;
static HWND game_window;

static double clamp(double n, double low, double high) {
    return fmax(low, fmin(high, n));
}
static double length(Vec2 a) {
    return hypot(a.x, a.y);
}
static double separation(Vec2 a, Vec2 b) {
    return hypot(a.x - b.x, a.y - b.y);
}
static uint32_t shade(uint32_t color, double brightness) {
    brightness = clamp(brightness, 0.0, 1.0);
    unsigned r = (unsigned)(((color >> 16) & 255) * brightness);
    unsigned g = (unsigned)(((color >> 8) & 255) * brightness);
    unsigned b = (unsigned)((color & 255) * brightness);
    return (r << 16) | (g << 8) | b;
}
static int map_tile(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_SIZE || y >= MAP_SIZE) {
        return 1;
    }
    return game.tiles[y][x];
}
static void notify(const char* message) {
    snprintf(game.message, sizeof(game.message), "%s", message);
    game.message_timer = 4.0;
}

/* Reset builds a deterministic level. Door cells use tile 4 until opened.
 * Wide central routes connect the three cores and the southeastern exit. */
static void reset_game(void) {
    memset(&game, 0, sizeof(game));
    game.player = (Vec2){2.5, 17.5};
    game.angle = -PI / 2;
    game.health = 100;
    game.magazine = 12;
    game.reserve = 72;
    game.mode = MODE_PLAY;
    for (int y = 0; y < MAP_SIZE; ++y) {
        for (int x = 0; x < MAP_SIZE; ++x) {
            game.tiles[y][x] = (x == 0 || y == 0 || x == MAP_SIZE - 1 || y == MAP_SIZE - 1) ? 1 : 0;
        }
    }
    for (int y = 1; y <= 15; ++y) {
        game.tiles[y][5] = 2;
    }
    game.tiles[5][5] = game.tiles[12][5] = 4;
    for (int y = 4; y <= 18; ++y) {
        game.tiles[y][11] = 3;
    }
    game.tiles[8][11] = game.tiles[16][11] = 4;
    for (int x = 1; x <= 18; ++x) {
        game.tiles[9][x] = 1;
    }
    game.tiles[9][3] = game.tiles[9][8] = game.tiles[9][14] = 4;
    for (int x = 12; x <= 18; ++x) {
        game.tiles[15][x] = 2;
    }
    game.tiles[15][16] = 4;
    for (int x = 7; x <= 9; ++x) {
        game.tiles[3][x] = 2;
    }
    game.tiles[5][16] = game.tiles[6][16] = 3;
    game.tiles[13][2] = game.tiles[13][3] = 2;
    game.enemies[0] = (Enemy){{3.5, 12.0}, 3, 0, 1.8, 0};
    game.enemies[1] = (Enemy){{8.5, 5.5}, 3, 0, 2.2, 0};
    game.enemies[2] = (Enemy){{9.5, 13.5}, 3, 0, 1.5, 0};
    game.enemies[3] = (Enemy){{14.5, 3.5}, 4, 0, 2.0, 0};
    game.enemies[4] = (Enemy){{17.5, 12.5}, 3, 0, 1.4, 0};
    game.enemies[5] = (Enemy){{14.5, 17.5}, 3, 0, 2.0, 0};
    game.enemies[6] = (Enemy){{17.5, 17.5}, 18, 1, 1.6, 0};
    game.enemy_count = 7;
    game.items[0] = (Item){{3.5, 3.5}, ITEM_CORE, 0};
    game.items[1] = (Item){{8.5, 15.5}, ITEM_CORE, 0};
    game.items[2] = (Item){{16.5, 3.5}, ITEM_CORE, 0};
    game.items[3] = (Item){{2.5, 11.5}, ITEM_HEALTH, 0};
    game.items[4] = (Item){{9.5, 7.5}, ITEM_HEALTH, 0};
    game.items[5] = (Item){{13.5, 16.5}, ITEM_HEALTH, 0};
    game.items[6] = (Item){{7.5, 11.5}, ITEM_AMMO, 0};
    game.items[7] = (Item){{14.5, 7.5}, ITEM_AMMO, 0};
    game.items[8] = (Item){{18.0, 16.5}, ITEM_AMMO, 0};
    game.item_count = 9;
    notify("FIND THREE POWER CORES. E OPENS MARKED DOORS.");
}

/* DDA visits grid boundaries in distance order. The returned distance is
 * along the supplied ray, allowing the camera plane to avoid fish-eye distortion. */
static RayHit cast_ray(Vec2 origin, Vec2 direction) {
    int cell_x = (int)floor(origin.x), cell_y = (int)floor(origin.y);
    double step_distance_x = fabs(direction.x) < 1e-10 ? 1e20 : fabs(1.0 / direction.x);
    double step_distance_y = fabs(direction.y) < 1e-10 ? 1e20 : fabs(1.0 / direction.y);
    int step_x = direction.x < 0 ? -1 : 1, step_y = direction.y < 0 ? -1 : 1;
    double side_x =
        (direction.x < 0 ? origin.x - cell_x : cell_x + 1.0 - origin.x) * step_distance_x;
    double side_y =
        (direction.y < 0 ? origin.y - cell_y : cell_y + 1.0 - origin.y) * step_distance_y;
    RayHit hit = {100, 1, 0, 0};
    for (int step = 0; step < MAP_SIZE * 4; ++step) {
        if (side_x < side_y) {
            side_x += step_distance_x;
            cell_x += step_x;
            hit.side = 0;
        } else {
            side_y += step_distance_y;
            cell_y += step_y;
            hit.side = 1;
        }
        if ((hit.tile = map_tile(cell_x, cell_y)) != 0) {
            hit.distance =
                fmax(.001, hit.side ? side_y - step_distance_y : side_x - step_distance_x);
            double wall = hit.side ? origin.x + hit.distance * direction.x
                                   : origin.y + hit.distance * direction.y;
            hit.wall_u = wall - floor(wall);
            return hit;
        }
    }
    return hit;
}
static int clear_line(Vec2 from, Vec2 to) {
    Vec2 delta = {to.x - from.x, to.y - from.y};
    double distance = length(delta);
    if (distance < .001) {
        return 1;
    }
    delta.x /= distance;
    delta.y /= distance;
    return cast_ray(from, delta).distance >= distance - .12;
}
static int walkable(Vec2 position) {
    const double radius = .19;
    return !map_tile((int)floor(position.x - radius), (int)floor(position.y - radius)) &&
           !map_tile((int)floor(position.x + radius), (int)floor(position.y - radius)) &&
           !map_tile((int)floor(position.x - radius), (int)floor(position.y + radius)) &&
           !map_tile((int)floor(position.x + radius), (int)floor(position.y + radius));
}
static void move_player(Vec2 delta) {
    Vec2 next = {game.player.x + delta.x, game.player.y};
    if (walkable(next)) {
        game.player.x = next.x;
    }
    next = (Vec2){game.player.x, game.player.y + delta.y};
    if (walkable(next)) {
        game.player.y = next.y;
    }
}
static void start_reload(void) {
    if (game.magazine < 12 && game.reserve > 0 && game.reload_timer <= 0) {
        game.reload_timer = 1.15;
        notify("RELOADING...");
    }
}
static void fire_weapon(void) {
    if (game.mode != MODE_PLAY || game.paused || game.map_open || game.fire_cooldown > 0 ||
        game.reload_timer > 0) {
        return;
    }
    if (game.magazine == 0) {
        start_reload();
        return;
    }
    --game.magazine;
    game.fire_cooldown = .24;
    game.recoil = .13;
    int nearest = -1;
    double nearest_distance = 30;
    for (int i = 0; i < game.enemy_count; ++i) {
        Enemy* enemy = &game.enemies[i];
        if (enemy->health <= 0) {
            continue;
        }
        double dx = enemy->position.x - game.player.x, dy = enemy->position.y - game.player.y;
        double forward = dx * cos(game.angle) + dy * sin(game.angle);
        double lateral = fabs(-dx * sin(game.angle) + dy * cos(game.angle));
        if (forward > 0 && forward < nearest_distance && lateral < (enemy->boss ? .52 : .30) &&
            clear_line(game.player, enemy->position)) {
            nearest = i;
            nearest_distance = forward;
        }
    }
    if (nearest >= 0) {
        Enemy* enemy = &game.enemies[nearest];
        --enemy->health;
        enemy->flash_timer = .12;
        if (enemy->health == 0) {
            ++game.kills;
            notify(enemy->boss ? "WARDEN DOWN. THE EXIT IS CLEAR." : "HOSTILE NEUTRALIZED.");
        }
    }
}
static void interact(void) {
    if (separation(game.player, (Vec2){18.25, 18.25}) < 1.2) {
        if (game.cores < 3) {
            notify("EXIT LOCKED: THREE POWER CORES REQUIRED.");
        } else if (game.enemies[6].health > 0) {
            notify("EXIT LOCKED: ELIMINATE THE WARDEN.");
        } else {
            game.mode = MODE_WIN;
            mouse_look = 0;
        }
        return;
    }
    Vec2 direction = {cos(game.angle), sin(game.angle)};
    for (double d = .25; d <= 1.35; d += .1) {
        int x = (int)(game.player.x + direction.x * d), y = (int)(game.player.y + direction.y * d);
        int tile = map_tile(x, y);
        if (tile == 4) {
            game.tiles[y][x] = 0;
            notify("BLAST DOOR OPEN.");
            return;
        }
        if (tile) {
            break;
        }
    }
    notify("E OPENS DOORS. THE EXIT IS IN THE SOUTHEAST.");
}
static void damage_player(int amount) {
    if (game.injury_timer > 0 || game.mode != MODE_PLAY) {
        return;
    }
    game.health -= amount;
    game.injury_timer = .45;
    if (game.health <= 0) {
        game.health = 0;
        game.mode = MODE_DEAD;
        mouse_look = 0;
    }
}
static void spawn_bullet(Vec2 position, Vec2 target, int damage, double speed) {
    if (game.bullet_count >= MAX_BULLETS) {
        return;
    }
    Vec2 delta = {target.x - position.x, target.y - position.y};
    double distance = fmax(.001, length(delta));
    game.bullets[game.bullet_count++] =
        (Bullet){position, {delta.x / distance * speed, delta.y / distance * speed}, 5, damage};
}

/* The frame loop substeps this simulation to keep movement and bullets from
 * crossing a wall or a player hitbox during a long display frame. */
static void update_game(double delta_time, double forward, double strafe, double turn, int sprint) {
    game.time += delta_time;
    game.fire_cooldown = fmax(0, game.fire_cooldown - delta_time);
    game.recoil = fmax(0, game.recoil - delta_time);
    game.injury_timer = fmax(0, game.injury_timer - delta_time);
    game.message_timer = fmax(0, game.message_timer - delta_time);
    if (game.reload_timer > 0) {
        game.reload_timer -= delta_time;
        if (game.reload_timer <= 0) {
            int rounds = 12 - game.magazine;
            if (rounds > game.reserve) {
                rounds = game.reserve;
            }
            game.magazine += rounds;
            game.reserve -= rounds;
        }
    }
    game.angle += turn * delta_time * 2.0;
    game.angle = remainder(game.angle, PI * 2);
    double magnitude = hypot(forward, strafe);
    if (magnitude > 1) {
        forward /= magnitude;
        strafe /= magnitude;
    }
    game.moving = magnitude > .01;
    double speed = sprint ? 3.7 : 2.6;
    move_player(
        (Vec2){(cos(game.angle) * forward - sin(game.angle) * strafe) * speed * delta_time,
               (sin(game.angle) * forward + cos(game.angle) * strafe) * speed * delta_time});
    int px = (int)game.player.x, py = (int)game.player.y;
    for (int y = py - 2; y <= py + 2; ++y) {
        for (int x = px - 2; x <= px + 2; ++x) {
            if (x >= 0 && y >= 0 && x < MAP_SIZE && y < MAP_SIZE) {
                game.explored[y][x] = 1;
            }
        }
    }
    for (int i = 0; i < game.item_count; ++i) {
        Item* item = &game.items[i];
        if (item->collected || separation(game.player, item->position) > .48) {
            continue;
        }
        if (item->type == ITEM_CORE) {
            ++game.cores;
            notify("POWER CORE SECURED. CHECK THE MAP FOR THE NEXT.");
        } else if (item->type == ITEM_HEALTH) {
            if (game.health == 100) {
                continue;
            }
            game.health = (int)fmin(100, game.health + 35);
            notify("MEDKIT: +35 HEALTH.");
        } else {
            game.reserve += 24;
            notify("AMMO CACHE: +24 ROUNDS.");
        }
        item->collected = 1;
    }
    for (int i = 0; i < game.enemy_count; ++i) {
        Enemy* enemy = &game.enemies[i];
        if (enemy->health <= 0) {
            continue;
        }
        enemy->flash_timer = fmax(0, enemy->flash_timer - delta_time);
        enemy->attack_timer -= delta_time;
        double distance = separation(enemy->position, game.player);
        if (distance > 9 || !clear_line(enemy->position, game.player)) {
            continue;
        }
        if (distance > (enemy->boss ? 3.0 : 2.3)) {
            Vec2 delta = {(game.player.x - enemy->position.x) / distance,
                          (game.player.y - enemy->position.y) / distance};
            Vec2 next = {enemy->position.x + delta.x * delta_time * .65,
                         enemy->position.y + delta.y * delta_time * .65};
            if (walkable(next)) {
                enemy->position = next;
            }
        }
        if (distance < .65) {
            damage_player(12);
        }
        if (enemy->attack_timer <= 0) {
            enemy->attack_timer = enemy->boss ? (enemy->health < 9 ? .85 : 1.35) : 2.3;
            spawn_bullet(enemy->position, game.player, enemy->boss ? 16 : 10,
                         enemy->boss ? 4.2 : 3.2);
            if (enemy->boss && enemy->health < 9) {
                Vec2 side_target = {game.player.x + .7, game.player.y - .7};
                spawn_bullet(enemy->position, side_target, 12, 4.0);
            }
        }
    }
    for (int i = game.bullet_count - 1; i >= 0; --i) {
        Bullet* bullet = &game.bullets[i];
        bullet->life -= delta_time;
        bullet->position.x += bullet->velocity.x * delta_time;
        bullet->position.y += bullet->velocity.y * delta_time;
        if (map_tile((int)floor(bullet->position.x), (int)floor(bullet->position.y))) {
            bullet->life = 0;
        }
        if (separation(bullet->position, game.player) < .27) {
            damage_player(bullet->damage);
            bullet->life = 0;
        }
        if (bullet->life <= 0) {
            game.bullets[i] = game.bullets[--game.bullet_count];
        }
    }
}

/* Software drawing uses a deliberately low resolution for a crisp retro look.
 * Every pixel is composed before the finished frame reaches the visible window. */
static void pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        framebuffer[y * SCREEN_W + x] = color;
    }
}
static void rectangle(int x, int y, int width, int height, uint32_t color) {
    int right = (int)fmin(SCREEN_W, x + width), bottom = (int)fmin(SCREEN_H, y + height);
    x = (int)fmax(0, x);
    y = (int)fmax(0, y);
    for (int row = y; row < bottom; ++row) {
        for (int col = x; col < right; ++col) {
            framebuffer[row * SCREEN_W + col] = color;
        }
    }
}
/* Five-bit rows for A-Z and 0-9. Text stays sharp when the framebuffer scales. */
static const unsigned char glyphs[36][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {31, 4, 4, 4, 4, 4, 31},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},   {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
static void text(const char* string, int x, int y, int scale, uint32_t color, int centered) {
    if (centered) {
        x -= (int)strlen(string) * 6 * scale / 2;
    }
    for (; *string; ++string, x += 6 * scale) {
        int c = toupper((unsigned char)*string), index = c >= 'A' && c <= 'Z'   ? c - 'A'
                                                         : c >= '0' && c <= '9' ? c - '0' + 26
                                                                                : -1;
        for (int row = 0; row < 7; ++row) {
            int bits = index >= 0                           ? glyphs[index][row]
                       : c == '-' && row == 3               ? 14
                       : c == '.' && row == 6               ? 4
                       : c == ':' && (row == 2 || row == 5) ? 4
                       : c == '/'                           ? (1 << row / 2)
                       : c == '+'                           ? (row == 3               ? 14
                                                               : row >= 1 && row <= 5 ? 4
                                                                                      : 0)
                                                            : 0;
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col))) {
                    rectangle(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}
static void sprite_rect(int type, int x, int y, int width, int height, uint32_t color) {
    for (int row = y; row < y + height; ++row) {
        for (int col = x; col < x + width; ++col) {
            if (row >= 0 && row < 64 && col >= 0 && col < 64) {
                sprites[type][row * 64 + col] = color;
            }
        }
    }
}
static void make_sprites(void) {
    memset(sprites, 0, sizeof(sprites));
    for (int type = 0; type < 2; ++type) {
        uint32_t armor = type ? 0xb85c5f : 0x718b8e, trim = type ? 0xf2bb79 : 0xa5b4ac;
        sprite_rect(type, 17, 21, 30, 29, armor);
        sprite_rect(type, 21, 10, 22, 22, trim);
        sprite_rect(type, 18, 5, 8, 12, armor);
        sprite_rect(type, 38, 5, 8, 12, armor);
        sprite_rect(type, 24, 19, 16, 7, 0x222936);
        sprite_rect(type, 26, 21, 4, 3, 0xff704e);
        sprite_rect(type, 35, 21, 4, 3, 0xff704e);
        sprite_rect(type, 8, 29, 10, 20, armor);
        sprite_rect(type, 46, 29, 10, 20, armor);
        sprite_rect(type, 17, 48, 12, 14, 0x303849);
        sprite_rect(type, 35, 48, 12, 14, 0x303849);
        sprite_rect(type, 27, 35, 10, 10, 0xd6a267);
        sprite_rect(type, 4, 41, 15, 7, 0x292e3c);
        if (type) {
            sprite_rect(type, 3, 23, 15, 7, trim);
            sprite_rect(type, 46, 23, 15, 7, trim);
        }
    }
    for (int y = 8; y < 56; ++y) {
        for (int x = 8; x < 56; ++x) {
            if (abs(x - 32) + abs(y - 32) < 23) {
                sprites[2][y * 64 + x] = abs(x - 32) + abs(y - 32) < 15 ? 0xc9ffee : 0x4ebba6;
            }
        }
    }
    sprite_rect(3, 15, 20, 34, 32, 0xd1cbb1);
    sprite_rect(3, 27, 24, 10, 24, 0xc25768);
    sprite_rect(3, 20, 31, 24, 10, 0xc25768);
    sprite_rect(4, 13, 24, 38, 28, 0x9a7955);
    sprite_rect(4, 16, 28, 32, 7, 0xe4bb75);
    sprite_rect(4, 22, 38, 20, 8, 0x443b38);
    for (int y = 12; y < 52; ++y) {
        for (int x = 12; x < 52; ++x) {
            double d = hypot(x - 32, y - 32);
            if (d < 17) {
                sprites[5][y * 64 + x] = d < 9 ? 0xffefad : 0xe98250;
            }
        }
    }
    sprite_rect(6, 7, 7, 50, 56, 0x2d746e);
    sprite_rect(6, 12, 12, 40, 46, 0x90d5bd);
    sprite_rect(6, 16, 16, 32, 39, 0x223d42);
    sprite_rect(6, 25, 22, 14, 26, 0xafe2c9);
    sprite_rect(6, 22, 30, 20, 9, 0xe0c382);
}

static uint32_t wall_texture(int tile, double u, double v, int side, double distance) {
    int x = (int)(u * 64) & 63, y = (int)(v * 64) & 63;
    uint32_t color;
    if (tile == 1) {
        int mortar = y % 20 < 2 || (x + (y / 20 % 2) * 16) % 32 < 2;
        color = mortar ? 0x30373e : 0x8f9291;
        if (!mortar && ((x * 13 + y * 7) % 17 < 3)) {
            color = 0x797f80;
        }
    } else if (tile == 2) {
        color = x % 16 < 2 || y < 3 || y > 60 ? 0x333f49 : 0x718991;
        if ((x % 16 == 5 || x % 16 == 6) && (y == 8 || y == 55)) {
            color = 0xb6c6c6;
        }
        if (y > 28 && y < 34) {
            color = 0xc69c66;
        }
    } else if (tile == 3) {
        color = x % 32 < 3 || y % 32 < 3 ? 0x352e3d : 0x98616b;
        if (x > 26 && x < 37 && y > 13 && y < 50) {
            color = 0xe98668;
        }
    } else {
        color = 0x536c70;
        if (x < 5 || x > 58) {
            color = 0x26363f;
        }
        if (y > 19 && y < 45) {
            color = ((x + y) / 8) % 2 ? 0xd3ae6a : 0x3c3d3d;
        }
        if (x > 28 && x < 35) {
            color = 0x25303a;
        }
    }
    return shade(color, (side ? .77 : 1.0) / (1 + distance * .10));
}
static void render_environment(void) {
    Vec2 direction = {cos(game.angle), sin(game.angle)};
    Vec2 plane = {-direction.y * .66, direction.x * .66};
    for (int y = 0; y < VIEW_H; ++y) {
        int below = y >= VIEW_H / 2;
        double row_distance = (VIEW_H * .5) / fmax(1, fabs(y - VIEW_H * .5));
        Vec2 left_ray = {direction.x - plane.x, direction.y - plane.y};
        Vec2 step = {row_distance * plane.x * 2 / SCREEN_W, row_distance * plane.y * 2 / SCREEN_W};
        Vec2 world = {game.player.x + left_ray.x * row_distance,
                      game.player.y + left_ray.y * row_distance};
        for (int x = 0; x < SCREEN_W; ++x) {
            int tx = (int)floor(world.x * 32), ty = (int)floor(world.y * 32);
            uint32_t color = below ? (((tx >> 5) ^ (ty >> 5)) & 1 ? 0x60615a : 0x51554e) : 0x3d4852;
            if ((tx & 31) == 0 || (ty & 31) == 0) {
                color = below ? 0x333d3b : 0x252f3c;
            }
            if (!below && (ty & 31) > 13 && (ty & 31) < 17 && ((int)floor(world.x) % 3 == 0)) {
                color = 0xb1bcb4;
            }
            pixel(x, y, shade(color, 1 / (1 + row_distance * .11)));
            world.x += step.x;
            world.y += step.y;
        }
    }
    for (int x = 0; x < SCREEN_W; ++x) {
        double camera_x = 2.0 * x / SCREEN_W - 1;
        Vec2 ray = {direction.x + plane.x * camera_x, direction.y + plane.y * camera_x};
        RayHit hit = cast_ray(game.player, ray);
        wall_depth[x] = hit.distance;
        double height = VIEW_H / hit.distance;
        int top = (int)fmax(0, VIEW_H * .5 - height * .5),
            bottom = (int)fmin(VIEW_H, VIEW_H * .5 + height * .5);
        for (int y = top; y < bottom; ++y) {
            double v = (y - (VIEW_H * .5 - height * .5)) / height;
            pixel(x, y, wall_texture(hit.tile, hit.wall_u, v, hit.side, hit.distance));
        }
    }
}
typedef struct {
    Vec2 position;
    int sprite;
    double scale, distance;
    int flashing;
} RenderSprite;
static void render_billboards(void) {
    RenderSprite list[MAX_ENEMIES + MAX_ITEMS + MAX_BULLETS + 1];
    int count = 0;
    for (int i = 0; i < game.enemy_count; ++i) {
        if (game.enemies[i].health > 0) {
            list[count++] = (RenderSprite){game.enemies[i].position, game.enemies[i].boss ? 1 : 0,
                                           game.enemies[i].boss ? 1.15 : .85, 0,
                                           game.enemies[i].flash_timer > 0};
        }
    }
    for (int i = 0; i < game.item_count; ++i) {
        if (!game.items[i].collected) {
            list[count++] =
                (RenderSprite){game.items[i].position, game.items[i].type + 2, .48, 0, 0};
        }
    }
    for (int i = 0; i < game.bullet_count; ++i) {
        list[count++] = (RenderSprite){game.bullets[i].position, 5, .20, 0, 0};
    }
    list[count++] = (RenderSprite){{18.25, 18.25}, 6, 1.0, 0, 0};
    for (int i = 0; i < count; ++i) {
        list[i].distance = separation(list[i].position, game.player);
    }
    for (int i = 1; i < count; ++i) {
        RenderSprite value = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].distance < value.distance) {
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = value;
    }
    double dx = cos(game.angle), dy = sin(game.angle);
    for (int i = 0; i < count; ++i) {
        RenderSprite* sprite = &list[i];
        double rx = sprite->position.x - game.player.x, ry = sprite->position.y - game.player.y;
        double depth = rx * dx + ry * dy, lateral = (-rx * dy + ry * dx) / .66;
        if (depth <= .08) {
            continue;
        }
        double size = fmin(4000, VIEW_H / depth * sprite->scale);
        int center_x = (int)(SCREEN_W * .5 * (1 + lateral / depth));
        double foot = VIEW_H * .5 + VIEW_H / depth * .5;
        if (sprite->sprite == 5) {
            foot = VIEW_H * .5 + size * .5;
        }
        int top = (int)(foot - size), left = (int)(center_x - size * .5);
        for (int x = (int)fmax(0, left); x < (int)fmin(SCREEN_W, left + size); ++x) {
            if (depth >= wall_depth[x]) {
                continue;
            }
            int u = (int)((x - left) * 64 / size);
            if (u < 0 || u >= 64) {
                continue;
            }
            for (int y = (int)fmax(0, top); y < (int)fmin(VIEW_H, foot); ++y) {
                int v = (int)((y - top) * 64 / size);
                if (v < 0 || v >= 64) {
                    continue;
                }
                uint32_t color = sprites[sprite->sprite][v * 64 + u];
                if (color) {
                    pixel(x, y, sprite->flashing ? CREAM : shade(color, 1 / (1 + depth * .09)));
                }
            }
        }
    }
}
static void draw_weapon(void) {
    int bob = game.moving ? (int)(sin(game.time * 11) * 3) : 0;
    int x = 293, y = 184 + bob + (game.reload_timer > 0 ? 28 : 0) + (game.recoil > 0 ? 6 : 0);
    rectangle(x + 16, y + 20, 56, 60, 0x9e7867);
    rectangle(x + 65, y + 34, 54, 40, 0x5d666a);
    rectangle(x + 9, y + 12, 72, 27, 0x242c38);
    rectangle(x + 4, y, 78, 28, 0x6f858c);
    rectangle(x + 10, y + 4, 64, 7, 0xa6b7b7);
    rectangle(x + 14, y + 20, 52, 5, 0x384b57);
    rectangle(x + 6, y - 15, 40, 21, 0x27343e);
    rectangle(x + 9, y - 13, 34, 8, 0x8ca6a7);
    rectangle(x + 19, y - 24, 10, 13, 0xced4be);
    rectangle(x + 40, y + 11, 15, 4, MINT);
    rectangle(x + 60, y + 28, 17, 35, 0x384451);
    if (game.recoil > .065) {
        rectangle(x - 1, y - 41, 56, 22, 0xe79b58);
        rectangle(x + 14, y - 52, 25, 42, GOLD);
        rectangle(x + 18, y - 38, 17, 19, 0xfff4c7);
    }
    int cx = SCREEN_W / 2, cy = VIEW_H / 2;
    rectangle(cx - 7, cy, 4, 1, MINT);
    rectangle(cx + 4, cy, 4, 1, MINT);
    rectangle(cx, cy - 7, 1, 4, MINT);
    rectangle(cx, cy + 4, 1, 4, MINT);
}
static void draw_map(int full) {
    int scale = full ? 10 : 3, ox = full ? 140 : 411, oy = full ? 30 : 9;
    rectangle(ox - 5, oy - 5, MAP_SIZE * scale + 10, MAP_SIZE * scale + 10, INK);
    for (int y = 0; y < MAP_SIZE; ++y) {
        for (int x = 0; x < MAP_SIZE; ++x) {
            uint32_t color = !game.explored[y][x]    ? 0x1b2933
                             : game.tiles[y][x] == 4 ? GOLD
                             : game.tiles[y][x]      ? 0x78908d
                                                     : 0x344b4e;
            rectangle(ox + x * scale, oy + y * scale, scale - (full ? 1 : 0),
                      scale - (full ? 1 : 0), color);
        }
    }
    if (full) {
        for (int i = 0; i < game.item_count; ++i) {
            if (!game.items[i].collected && game.items[i].type == ITEM_CORE) {
                rectangle(ox + (int)(game.items[i].position.x * scale) - 2,
                          oy + (int)(game.items[i].position.y * scale) - 2, 5, 5, MINT);
            }
        }
        for (int i = 0; i < game.enemy_count; ++i) {
            if (game.enemies[i].health > 0 &&
                game.explored[(int)game.enemies[i].position.y][(int)game.enemies[i].position.x]) {
                rectangle(ox + (int)(game.enemies[i].position.x * scale) - 2,
                          oy + (int)(game.enemies[i].position.y * scale) - 2, 4, 4, RED);
            }
        }
        rectangle(ox + 18 * scale, oy + 18 * scale, 6, 6, GOLD);
    }
    int px = ox + (int)(game.player.x * scale), py = oy + (int)(game.player.y * scale);
    rectangle(px - 2, py - 2, 4, 4, CREAM);
    for (int d = 0; d < 7; ++d) {
        pixel(px + (int)(cos(game.angle) * d), py + (int)(sin(game.angle) * d), CREAM);
    }
    if (full) {
        text("FACILITY AUTOMAP", 240, 9, 2, GOLD, 1);
        text("TEAL CORES   RED HOSTILES   GOLD DOORS / EXIT", 240, 246, 1, CREAM, 1);
        text("M / TAB / ESC - RETURN", 240, 265, 1, MINT, 1);
    }
}
static void render_frame(void) {
    render_environment();
    render_billboards();
    draw_weapon();
    if (game.injury_timer > 0) {
        for (int y = 0; y < VIEW_H; ++y) {
            for (int x = 0; x < SCREEN_W; ++x) {
                if (x < 4 || x >= SCREEN_W - 4 || y < 4 || y >= VIEW_H - 4) {
                    pixel(x, y, RED);
                }
            }
        }
    }
    rectangle(0, VIEW_H, SCREEN_W, SCREEN_H - VIEW_H, INK);
    rectangle(0, VIEW_H, SCREEN_W, 2, 0x4b7373);
    char buffer[128];
    text("VITAL", 13, 250, 1, MUTED, 0);
    snprintf(buffer, sizeof(buffer), "%03d", game.health);
    text(buffer, 13, 263, 3, game.health < 30 ? RED : CREAM, 0);
    text("MAG / RESERVE", 110, 250, 1, MUTED, 0);
    snprintf(buffer, sizeof(buffer), "%02d / %03d", game.magazine, game.reserve);
    text(buffer, 110, 267, 2, GOLD, 0);
    text("CORES", 271, 250, 1, MUTED, 0);
    snprintf(buffer, sizeof(buffer), "%d / 3", game.cores);
    text(buffer, 271, 267, 2, MINT, 0);
    text("HOSTILES", 371, 250, 1, MUTED, 0);
    snprintf(buffer, sizeof(buffer), "%d / 7", game.kills);
    text(buffer, 371, 267, 2, RED, 0);
    draw_map(0);
    if (game.enemies[6].health > 0 && separation(game.player, game.enemies[6].position) < 7 &&
        clear_line(game.player, game.enemies[6].position)) {
        rectangle(146, 8, 188, 22, INK);
        text("THE WARDEN", 240, 11, 1, GOLD, 1);
        rectangle(150, 22, 180, 3, 0x483747);
        rectangle(150, 22, 180 * game.enemies[6].health / 18, 3, RED);
    }
    if (game.message_timer > 0) {
        rectangle(8, 222, 464, 13, INK);
        text(game.message, 240, 225, 1, GOLD, 1);
    }
    if (game.reload_timer > 0) {
        text("RELOADING", 240, 166, 1, GOLD, 1);
    }
    if (game.map_open) {
        rectangle(95, 0, 290, SCREEN_H, INK);
        draw_map(1);
    }
    if (game.paused) {
        rectangle(65, 61, 350, 141, INK);
        text("SYSTEM PAUSED", 240, 78, 2, GOLD, 1);
        text("WASD MOVE   ARROWS TURN   SHIFT SPRINT", 240, 110, 1, CREAM, 1);
        text("SPACE / CLICK FIRE   R RELOAD   E USE", 240, 130, 1, CREAM, 1);
        text("F1 MOUSE LOOK   M MAP   ESC RESUME", 240, 150, 1, MINT, 1);
        text("Q QUIT", 240, 178, 1, MUTED, 1);
    }
    if (game.mode == MODE_TITLE) {
        rectangle(37, 37, 406, 178, INK);
        text("IRON VEIL", 240, 55, 4, GOLD, 1);
        text("A RETRO RAYCAST ADVENTURE IN C", 240, 100, 1, MINT, 1);
        text("THE FACILITY IS DARK. SOMETHING IS AWAKE.", 240, 127, 1, CREAM, 1);
        text("FIND 3 CORES. DEFEAT THE WARDEN. REACH THE EXIT.", 240, 145, 1, MUTED, 1);
        text("ENTER - BEGIN", 240, 175, 2, CREAM, 1);
        text("WASD MOVE  ARROWS TURN  SPACE FIRE  E DOORS", 240, 201, 1, MINT, 1);
    } else if (game.mode == MODE_DEAD || game.mode == MODE_WIN) {
        rectangle(55, 65, 370, 125, INK);
        text(game.mode == MODE_WIN ? "FACILITY SECURED" : "SIGNAL LOST", 240, 84, 3,
             game.mode == MODE_WIN ? MINT : RED, 1);
        text(game.mode == MODE_WIN ? "THE IRON VEIL HAS LIFTED."
                                   : "THE FACILITY CLAIMED ANOTHER SOUL.",
             240, 125, 1, CREAM, 1);
        text("ENTER - NEW RUN   ESC - QUIT", 240, 158, 1, GOLD, 1);
    }
}

/* BMP output is diagnostic only. It writes the exact software framebuffer
 * without a graphics library or any user save files. */
static int save_preview(const wchar_t* filename) {
    wchar_t directory[32768], path[32768];
    swprintf(directory, 32768, L"%ls\\screenshots", project_root);
    CreateDirectoryW(directory, NULL);
    swprintf(path, 32768, L"%ls\\%ls", directory, filename);
    FILE* output = _wfopen(path, L"wb");
    if (!output) {
        return 0;
    }
    BITMAPFILEHEADER header = {0};
    BITMAPINFOHEADER info = {0};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + sizeof(framebuffer);
    info.biSize = sizeof(info);
    info.biWidth = SCREEN_W;
    info.biHeight = -SCREEN_H;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    int okay = fwrite(&header, sizeof(header), 1, output) == 1 &&
               fwrite(&info, sizeof(info), 1, output) == 1 &&
               fwrite(framebuffer, sizeof(framebuffer), 1, output) == 1;
    fclose(output);
    return okay;
}
static int self_test(void) {
    wchar_t path[32768];
    swprintf(path, 32768, L"%ls\\self-test-results.txt", project_root);
    FILE* log = _wfopen(path, L"w");
    if (!log) {
        return 1;
    }
    int failures = 0;
#define CHECK(condition, label)                                                                    \
    do {                                                                                           \
        int result = !!(condition);                                                                \
        fprintf(log, "%s %s\n", result ? "PASS" : "FAIL", label);                                  \
        if (!result)                                                                               \
            ++failures;                                                                            \
    } while (0)
    reset_game();
    CHECK(walkable(game.player), "spawn clearance");
    Vec2 previous = game.player;
    update_game(.1, 1, 0, 0, 0);
    CHECK(separation(previous, game.player) > .2, "forward movement");
    game.player = (Vec2){1.2, 1.5};
    move_player((Vec2){-.15, 0});
    CHECK(game.player.x == 1.2, "wall collision");
    RayHit hit = cast_ray((Vec2){2.5, 2.5}, (Vec2){1, 0});
    CHECK(fabs(hit.distance - 2.5) < .001 && hit.tile == 2, "DDA wall distance");
    game.player = (Vec2){4.4, 5.5};
    game.angle = 0;
    interact();
    CHECK(game.tiles[5][5] == 0, "door interaction");
    reset_game();
    game.player = (Vec2){8.5, 6.5};
    game.angle = -PI / 2;
    fire_weapon();
    CHECK(game.enemies[1].health == 2 && game.magazine == 11, "hitscan enemy damage");
    game.fire_cooldown = 0;
    game.player = (Vec2){4.0, 5.5};
    game.angle = 0;
    fire_weapon();
    CHECK(game.enemies[1].health == 2, "closed door blocks gunfire");
    game.magazine = 0;
    start_reload();
    update_game(1.2, 0, 0, 0, 0);
    CHECK(game.magazine == 12 && game.reserve == 60, "magazine reload");
    for (int i = 0; i < 3; ++i) {
        game.player = game.items[i].position;
        update_game(.001, 0, 0, 0, 0);
    }
    CHECK(game.cores == 3, "three core pickups");
    game.health = 30;
    game.player = game.items[3].position;
    update_game(.001, 0, 0, 0, 0);
    CHECK(game.health == 65, "medkit pickup");
    int reserve = game.reserve;
    game.player = game.items[6].position;
    update_game(.001, 0, 0, 0, 0);
    CHECK(game.reserve == reserve + 24, "ammo pickup");
    game.injury_timer = 0;
    damage_player(10);
    damage_player(10);
    CHECK(game.health == 55, "damage protection window");
    game.player = (Vec2){18.25, 18.25};
    interact();
    CHECK(game.mode == MODE_PLAY, "living boss locks exit");
    game.enemies[6].health = 0;
    interact();
    CHECK(game.mode == MODE_WIN, "completed exit wins");
    reset_game();
    game.injury_timer = 0;
    damage_player(200);
    CHECK(game.mode == MODE_DEAD && game.health == 0, "death state");
    reset_game();
    game.player = (Vec2){16.0, 17.5};
    for (int i = 0; i < 6; ++i) {
        game.enemies[i].health = 0;
    }
    game.enemies[6].health = 8;
    game.enemies[6].attack_timer = 0;
    update_game(.01, 0, 0, 0, 0);
    CHECK(game.bullet_count == 2, "warden second-phase double shot");
    game.enemies[6].health = 0;
    game.bullet_count = 1;
    game.bullets[0] = (Bullet){game.player, {0, 0}, 1, 10};
    game.injury_timer = 0;
    update_game(.01, 0, 0, 0, 0);
    CHECK(game.health == 90 && game.bullet_count == 0, "projectile hits player");
    game.bullets[0] = (Bullet){{4.95, 6.5}, {3, 0}, 1, 10};
    game.bullet_count = 1;
    update_game(.1, 0, 0, 0, 0);
    CHECK(game.bullet_count == 0, "projectile stops at wall");
    reset_game();
    unsigned char reached[MAP_SIZE][MAP_SIZE] = {{0}};
    int queue[MAP_SIZE * MAP_SIZE], front = 0, back = 0;
    queue[back++] = 17 * MAP_SIZE + 2;
    reached[17][2] = 1;
    while (front < back) {
        int cell = queue[front++], x = cell % MAP_SIZE, y = cell / MAP_SIZE;
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i], ny = y + dy[i];
            if (nx >= 0 && ny >= 0 && nx < MAP_SIZE && ny < MAP_SIZE && !reached[ny][nx] &&
                (map_tile(nx, ny) == 0 || map_tile(nx, ny) == 4)) {
                reached[ny][nx] = 1;
                queue[back++] = ny * MAP_SIZE + nx;
            }
        }
    }
    for (int i = 0; i < game.item_count; ++i) {
        CHECK(reached[(int)game.items[i].position.y][(int)game.items[i].position.x],
              "item reachable through doors");
    }
    for (int i = 0; i < game.enemy_count; ++i) {
        CHECK(reached[(int)game.enemies[i].position.y][(int)game.enemies[i].position.x],
              "enemy reachable");
    }
    CHECK(reached[18][18], "exit reachable");
    make_sprites();
    game.player = (Vec2){3.5, 15.5};
    game.angle = -PI / 2;
    render_frame();
    CHECK(save_preview(L"gameplay.bmp"), "gameplay screenshot");
    game.player = (Vec2){8.5, 7.8};
    game.angle = -PI / 2;
    render_frame();
    CHECK(save_preview(L"combat.bmp"), "sprite rendering screenshot");
    memset(game.explored, 1, sizeof(game.explored));
    game.map_open = 1;
    render_frame();
    CHECK(save_preview(L"map.bmp"), "map screenshot");
    game.map_open = 0;
    game.mode = MODE_TITLE;
    render_frame();
    CHECK(save_preview(L"title.bmp"), "title screenshot");
    fprintf(log, "%s\n", failures ? "CHECKS FAILED" : "All checks passed.");
    fclose(log);
#undef CHECK
    return failures ? 1 : 0;
}

/* Windows host: one completed, client-sized bitmap is copied inside WM_PAINT.
 * Never clear the visible window separately; that would cause flickering. */
static HDC presentation_dc;
static HBITMAP presentation_bitmap;
static HGDIOBJ original_bitmap;
static int presentation_width, presentation_height;
static BITMAPINFO framebuffer_info;
static void present_frame(HDC target, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width != presentation_width || height != presentation_height) {
        HBITMAP replacement = CreateCompatibleBitmap(target, width, height);
        if (!replacement) {
            return;
        }
        HGDIOBJ previous = SelectObject(presentation_dc, replacement);
        if (!presentation_bitmap) {
            original_bitmap = previous;
        } else {
            DeleteObject(presentation_bitmap);
        }
        presentation_bitmap = replacement;
        presentation_width = width;
        presentation_height = height;
    }
    RECT client = {0, 0, width, height};
    FillRect(presentation_dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    double scale = fmin((double)width / SCREEN_W, (double)height / SCREEN_H);
    int draw_width = (int)(SCREEN_W * scale), draw_height = (int)(SCREEN_H * scale);
    SetStretchBltMode(presentation_dc, COLORONCOLOR);
    StretchDIBits(presentation_dc, (width - draw_width) / 2, (height - draw_height) / 2, draw_width,
                  draw_height, 0, 0, SCREEN_W, SCREEN_H, framebuffer, &framebuffer_info,
                  DIB_RGB_COLORS, SRCCOPY);
    BitBlt(target, 0, 0, width, height, presentation_dc, 0, 0, SRCCOPY);
}
static void set_mouse_look(int enabled) {
    mouse_look = enabled;
    if (enabled) {
        RECT client;
        GetClientRect(game_window, &client);
        POINT center = {client.right / 2, client.bottom / 2};
        ClientToScreen(game_window, &center);
        SetCursorPos(center.x, center.y);
    }
}
static LRESULT CALLBACK handle_message(HWND window, UINT message, WPARAM key, LPARAM data) {
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC target = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        if (presentation_dc) {
            present_frame(target, client.right, client.bottom);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        if (game.mode == MODE_PLAY) {
            game.paused = 1;
        }
        set_mouse_look(0);
        return 0;
    case WM_SETCURSOR:
        if (mouse_look && LOWORD(data) == HTCLIENT) {
            SetCursor(NULL);
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        fire_weapon();
        return 0;
    case WM_KEYDOWN:
        if (data & (1LL << 30)) {
            return 0;
        }
        if (game.mode != MODE_PLAY) {
            if (key == VK_RETURN) {
                reset_game();
                set_mouse_look(0);
            }
            if (key == VK_ESCAPE) {
                DestroyWindow(window);
            }
        } else if (game.map_open) {
            if (key == 'M' || key == VK_TAB || key == VK_ESCAPE) {
                game.map_open = 0;
            }
        } else if (game.paused) {
            if (key == VK_ESCAPE) {
                game.paused = 0;
            }
            if (key == 'Q') {
                DestroyWindow(window);
            }
        } else {
            if (key == VK_ESCAPE) {
                game.paused = 1;
                set_mouse_look(0);
            }
            if (key == 'M' || key == VK_TAB) {
                game.map_open = 1;
                set_mouse_look(0);
            }
            if (key == 'E') {
                interact();
            }
            if (key == 'R') {
                start_reload();
            }
            if (key == VK_SPACE || key == 'J') {
                fire_weapon();
            }
            if (key == VK_F1) {
                set_mouse_look(!mouse_look);
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, key, data);
}
static int held(int key) {
    return !!(GetAsyncKeyState(key) & 0x8000);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    GetModuleFileNameW(NULL, project_root, 32768);
    wchar_t* slash = wcsrchr(project_root, L'\\');
    if (slash) {
        *slash = 0;
    }
    slash = wcsrchr(project_root, L'\\');
    if (slash) {
        *slash = 0; /* executable lives in build/ */
    }
    if (wcsstr(command, L"--self-test")) {
        return self_test();
    }
    reset_game();
    make_sprites();
    game.mode = MODE_TITLE;
    framebuffer_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    framebuffer_info.bmiHeader.biWidth = SCREEN_W;
    framebuffer_info.bmiHeader.biHeight = -SCREEN_H;
    framebuffer_info.bmiHeader.biPlanes = 1;
    framebuffer_info.bmiHeader.biBitCount = 32;
    framebuffer_info.bmiHeader.biCompression = BI_RGB;
    SetProcessDPIAware();
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = handle_message;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"IronVeilWindow";
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassW(&window_class)) {
        return 1;
    }
    RECT desired = {0, 0, 960, 600};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    game_window = CreateWindowW(window_class.lpszClassName, L"Iron Veil - C Software Raycaster",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                desired.right - desired.left, desired.bottom - desired.top, NULL,
                                NULL, instance, NULL);
    if (!game_window) {
        return 1;
    }
    presentation_dc = CreateCompatibleDC(NULL);
    if (!presentation_dc) {
        DestroyWindow(game_window);
        return 1;
    }
    render_frame();
    ShowWindow(game_window, show);
    LARGE_INTEGER frequency, previous_time;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous_time);
    MSG message;
    int running = 1;
    while (running) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = 0;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double delta_time =
            fmin(.04, (double)(now.QuadPart - previous_time.QuadPart) / frequency.QuadPart);
        previous_time = now;
        if (game.mode == MODE_PLAY && !game.paused && !game.map_open) {
            double turn = held(VK_RIGHT) - held(VK_LEFT);
            if (mouse_look) {
                RECT client;
                GetClientRect(game_window, &client);
                POINT center = {client.right / 2, client.bottom / 2}, pointer;
                ClientToScreen(game_window, &center);
                GetCursorPos(&pointer);
                game.angle += (pointer.x - center.x) * .0028;
                SetCursorPos(center.x, center.y);
            }
            int steps = (int)ceil(delta_time * 120);
            if (steps < 1) {
                steps = 1;
            }
            for (int i = 0; i < steps && game.mode == MODE_PLAY; ++i) {
                update_game(delta_time / steps,
                            held('W') || held(VK_UP)     ? 1.0
                            : held('S') || held(VK_DOWN) ? -1.0
                                                         : 0,
                            held('D') - held('A'), turn, held(VK_SHIFT));
            }
            if (held(VK_SPACE) || held(VK_LBUTTON)) {
                fire_weapon();
            }
        }
        render_frame();
        InvalidateRect(game_window, NULL, FALSE);
        UpdateWindow(game_window);
        Sleep(8);
    }
    if (presentation_bitmap) {
        SelectObject(presentation_dc, original_bitmap);
        DeleteObject(presentation_bitmap);
    }
    DeleteDC(presentation_dc);
    return 0;
}
