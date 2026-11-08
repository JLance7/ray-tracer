# Code walkthrough

Everything is in `src/main.c`, organized from game data through simulation, software rendering, checks and the Windows host. Functions and variables use descriptive names, and comments explain the coordinate and ownership rules.

## World and simulation

`Game` holds the player, 20-by-20 tile map, exploration mask, enemies, items and projectiles. One tile is one world unit. Angles are radians; times are seconds. `reset_game` builds the deterministic level. Tile zero is open space; tiles 1-3 are wall materials; tile 4 is an interactive door. Opening a door changes its cell to zero.

`walkable` tests four corners around the player's collision radius. `move_player` resolves the two axes separately to allow sliding along walls. Input normalization prevents diagonal movement from being faster. The frame loop subdivides elapsed time into small steps to keep collision dependable.

`fire_weapon` performs hitscan shooting: it selects the closest living enemy intersecting the line of fire and checks for a blocking wall. `clear_line` reuses the raycaster for both shooting and enemy sight. Enemies pursue when visible, and fire traveling projectiles. The Warden switches to a faster double-shot pattern below half health.

`update_game` advances timers and reloads, moves the player, reveals map cells, collects nearby items, updates enemies and processes projectiles. Short damage protection prevents every frame of contact from consuming health. `interact` handles doors and the win conditions at the exit.

## Raycasting

`cast_ray` implements digital differential analysis (DDA). It compares the distance to the next horizontal and vertical grid boundary, visits whichever is closer, and repeats until it reaches a solid cell. The result includes distance, wall material, hit side and a fractional texture coordinate.

For each screen column, `render_environment` creates a ray from the forward vector and perpendicular camera plane. The wall height is inversely proportional to distance. Camera-plane rays yield perpendicular depth without a fish-eye correction pass. `wall_texture` generates brick, panel, illuminated and hazard-door textures directly from texture coordinates.

Floors and ceilings use horizontal perspective casting. For a screen row, the renderer calculates a world distance from the horizon, then steps through world coordinates across the row and samples procedural tile patterns.

## Sprites and pixels

`make_sprites` builds transparent 64-by-64 images in memory. A zero pixel is transparent. `render_billboards` sorts objects from farthest to nearest, projects them into camera space, and draws their nontransparent pixels only where their depth is closer than the wall depth for that column.

The weapon, muzzle flash, HUD and custom five-by-seven pixel font draw directly into `framebuffer`. Color values use `0x00RRGGBB`, which matches the byte layout of the 32-bit Windows DIB on this target. No downloaded textures or fonts are used.

## Windows presentation

`handle_message` processes one-off actions, focus changes, resizing and painting. Continuous movement and held fire are polled in the loop. F1 mouse look reads horizontal cursor displacement and recenters the cursor; pause, focus loss and map opening release it.

`present_frame` recreates its client-sized bitmap only when the window size changes. It composes the black letterbox bars and the scaled framebuffer off-screen, then copies that finished image to the window with one `BitBlt`. `WM_ERASEBKGND` is suppressed. Do not add a visible-window clear before presentation, because it causes flashing between frames.

## Tests

`self_test` uses the same gameplay methods as a real run, then checks map connectivity with a flood fill that treats openable doors as traversable. Preview files are uncompressed BMPs written directly with standard C file operations and Windows bitmap headers. No graphics SDK beyond the Windows API is needed.
