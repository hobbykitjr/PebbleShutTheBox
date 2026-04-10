/**
 * Shut the Box — Dice game for Pebble
 * Targets: emery (Time 2), gabbro (Round 2)
 *
 * 2-6 players. Each player has tiles 1-9/10/12. Roll dice, shut tiles
 * that sum to the roll. If no valid combo, you bust.
 * Win by: lowest sum or fewest remaining tiles.
 * Shutting all tiles wins immediately.
 */

#include <pebble.h>
#include <stdlib.h>

#define MAX_PLAYERS 6
#define MAX_TILES   12
#define NUM_TOKENS  6

enum { ST_SETUP, ST_SETTINGS, ST_ORDER, ST_ROLL, ST_SELECT, ST_BUST, ST_GAMEOVER };
enum { WIN_SUM, WIN_TILES };

typedef struct {
  bool open[MAX_TILES];
  int  score;
  bool shut_all;
  int  icon;
} Player;

static const char *s_tok_name[] = {
  "Star","Heart","Diamond","Circle","Square","Bolt"
};
static const char *s_tok_char[] = {
  "\xEF\x80\x85","\xEF\x80\x84","\xEF\x88\x99",
  "\xEF\x84\x91","\xEF\x83\x88","\xEF\x83\xA7",
};

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_win;
static Layer  *s_canvas;
static GFont   s_icon_font_20, s_icon_font_14;

static int s_state = ST_SETUP;
static int s_num_players;
static int s_setup_cursor = 0;
static int s_cursor = 0;

// Settings
static int s_num_tiles = 9;       // 9, 10, or 12
static int s_win_mode = WIN_SUM;  // WIN_SUM or WIN_TILES
static int s_settings_cursor = 0; // 0=tiles, 1=win, 2=start

static Player s_players[MAX_PLAYERS];
static int    s_order[MAX_PLAYERS];
static int    s_cur_idx;

static int  s_dice[2];
static int  s_dice_sum;
static bool s_one_die;
static bool s_selected[MAX_TILES];
static int  s_selected_sum;

static bool s_show_scores;
static int  s_gameover_cursor; // 0=rematch, 1=new game

// Tile count options
static const int s_tile_opts[] = {9, 10, 12};
static int s_tile_opt_idx = 0; // index into s_tile_opts

// ============================================================================
// COLORS
// ============================================================================
#ifdef PBL_COLOR
static GColor tok_color(int t) {
  switch(t) {
    case 0: return GColorYellow;
    case 1: return GColorRed;
    case 2: return GColorCyan;
    case 3: return GColorGreen;
    case 4: return GColorOrange;
    default: return GColorPurple;
  }
}
#endif

// ============================================================================
// DRAWING HELPERS
// ============================================================================
static void draw_token(GContext *ctx, int cx, int cy, int icon, bool lg) {
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, tok_color(icon));
  #else
  graphics_context_set_text_color(ctx, GColorWhite);
  #endif
  GFont f = lg ? s_icon_font_20 : s_icon_font_14;
  int sz = lg ? 30 : 22;
  if(!f) {
    f = fonts_get_system_font(lg ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
    char nm[2] = {s_tok_name[icon][0], 0};
    graphics_draw_text(ctx, nm, f, GRect(cx-sz/2, cy-sz/2, sz, sz),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  graphics_draw_text(ctx, s_tok_char[icon], f, GRect(cx-sz/2, cy-sz/2, sz, sz),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void draw_die(GContext *ctx, int cx, int cy, int sz, int val) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(cx-sz/2, cy-sz/2, sz, sz), 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, GRect(cx-sz/2, cy-sz/2, sz, sz), 4);
  graphics_context_set_fill_color(ctx, GColorBlack);
  int d = sz/4, dr = sz/10;
  if(dr < 2) dr = 2;
  if(val==1||val==3||val==5) graphics_fill_circle(ctx, GPoint(cx, cy), dr);
  if(val>=2) {
    graphics_fill_circle(ctx, GPoint(cx-d, cy-d), dr);
    graphics_fill_circle(ctx, GPoint(cx+d, cy+d), dr);
  }
  if(val>=4) {
    graphics_fill_circle(ctx, GPoint(cx+d, cy-d), dr);
    graphics_fill_circle(ctx, GPoint(cx-d, cy+d), dr);
  }
  if(val==6) {
    graphics_fill_circle(ctx, GPoint(cx-d, cy), dr);
    graphics_fill_circle(ctx, GPoint(cx+d, cy), dr);
  }
}

static void draw_tile(GContext *ctx, int x, int y, int tw, int th,
                      int num, bool is_open, bool selected, bool cursor_on) {
  if(is_open) {
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, selected ? GColorYellow :
      GColorFromHEX(0xAA8800));
    #else
    graphics_context_set_fill_color(ctx, selected ? GColorWhite : GColorLightGray);
    #endif
    graphics_fill_rect(ctx, GRect(x, y, tw, th), 3, GCornersAll);
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, cursor_on ? GColorWhite : GColorFromHEX(0x554400));
    #else
    graphics_context_set_stroke_color(ctx, cursor_on ? GColorWhite : GColorBlack);
    #endif
    graphics_context_set_stroke_width(ctx, cursor_on ? 2 : 1);
    graphics_draw_round_rect(ctx, GRect(x, y, tw, th), 3);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, selected ? GColorBlack : GColorWhite);
    #else
    graphics_context_set_text_color(ctx, selected ? GColorBlack : GColorBlack);
    #endif
  } else {
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x333333));
    #else
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    #endif
    graphics_fill_rect(ctx, GRect(x, y, tw, th), 3, GCornersAll);
    graphics_context_set_text_color(ctx, GColorDarkGray);
  }
  char nb[3];
  snprintf(nb, sizeof(nb), "%d", num);
  GFont tf = (num >= 10)
    ? fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD)
    : fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_draw_text(ctx, nb, tf,
    GRect(x, y - 1, tw, th + 2),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Draw all tiles for current player
static void draw_tiles(GContext *ctx, int w, Player *p, int tiles_y) {
  int row1_n, row2_n;
  if(s_num_tiles == 12) { row1_n = 6; row2_n = 6; }
  else if(s_num_tiles == 10) { row1_n = 5; row2_n = 5; }
  else { row1_n = 5; row2_n = 4; }

  int tw = (s_num_tiles == 12) ? 24 : 28;
  int th = tw, tgap = 4;

  int row1_w = row1_n * tw + (row1_n - 1) * tgap;
  int row2_w = row2_n * tw + (row2_n - 1) * tgap;
  int row1_x = (w - row1_w) / 2;
  int row2_x = (w - row2_w) / 2;

  for(int i = 0; i < row1_n; i++) {
    int tx = row1_x + i * (tw + tgap);
    draw_tile(ctx, tx, tiles_y, tw, th, i+1, p->open[i],
      s_selected[i], s_state == ST_SELECT && s_cursor == i);
  }
  for(int i = 0; i < row2_n; i++) {
    int idx = row1_n + i;
    int tx = row2_x + i * (tw + tgap);
    draw_tile(ctx, tx, tiles_y + th + tgap, tw, th, idx+1, p->open[idx],
      s_selected[idx], s_state == ST_SELECT && s_cursor == idx);
  }
}

// ============================================================================
// GAME LOGIC
// ============================================================================
static int cur_player(void) { return s_order[s_cur_idx]; }

static void shuffle_order(void) {
  int icons[NUM_TOKENS];
  for(int i = 0; i < NUM_TOKENS; i++) icons[i] = i;
  for(int i = NUM_TOKENS - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int t = icons[i]; icons[i] = icons[j]; icons[j] = t;
  }
  for(int i = 0; i < s_num_players; i++) {
    s_players[i].icon = icons[i];
    s_order[i] = i;
  }
  for(int i = s_num_players - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int t = s_order[i]; s_order[i] = s_order[j]; s_order[j] = t;
  }
}

static void init_player_boxes(int pi) {
  for(int i = 0; i < MAX_TILES; i++) s_players[pi].open[i] = (i < s_num_tiles);
  s_players[pi].score = 0;
  s_players[pi].shut_all = false;
}

static int remaining_sum(int pi) {
  int s = 0;
  for(int i = 0; i < s_num_tiles; i++)
    if(s_players[pi].open[i]) s += i + 1;
  return s;
}

static int remaining_count(int pi) {
  int c = 0;
  for(int i = 0; i < s_num_tiles; i++)
    if(s_players[pi].open[i]) c++;
  return c;
}

static int player_score(int pi) {
  return (s_win_mode == WIN_TILES) ? remaining_count(pi) : remaining_sum(pi);
}

static bool all_shut(int pi) {
  for(int i = 0; i < s_num_tiles; i++)
    if(s_players[pi].open[i]) return false;
  return true;
}

static bool check_one_die(int pi) {
  for(int i = 6; i < s_num_tiles; i++)
    if(s_players[pi].open[i]) return false;
  return true;
}

static bool can_make_sum(int pi, int target) {
  bool *open = s_players[pi].open;
  for(int mask = 1; mask < (1 << s_num_tiles); mask++) {
    int sum = 0;
    bool valid = true;
    for(int i = 0; i < s_num_tiles; i++) {
      if(mask & (1 << i)) {
        if(!open[i]) { valid = false; break; }
        sum += i + 1;
        if(sum > target) { valid = false; break; }
      }
    }
    if(valid && sum == target) return true;
  }
  return false;
}

static void roll_dice(void) {
  int cp = cur_player();
  s_one_die = check_one_die(cp);
  s_dice[0] = (rand() % 6) + 1;
  s_dice[1] = s_one_die ? 0 : (rand() % 6) + 1;
  s_dice_sum = s_dice[0] + s_dice[1];
  for(int i = 0; i < MAX_TILES; i++) s_selected[i] = false;
  s_selected_sum = 0;
}

static bool tile_selectable(int tile) {
  int cp = cur_player();
  if(!s_players[cp].open[tile]) return false;
  if(s_selected[tile]) return true; // can deselect
  // Check if this tile can be part of any valid combo reaching remaining target
  int remaining = s_dice_sum - s_selected_sum;
  if((tile + 1) > remaining) return false;
  bool *open = s_players[cp].open;
  for(int mask = 1; mask < (1 << s_num_tiles); mask++) {
    if(!(mask & (1 << tile))) continue; // must include this tile
    int sum = 0;
    bool valid = true;
    for(int i = 0; i < s_num_tiles; i++) {
      if(mask & (1 << i)) {
        if(!open[i] || (s_selected[i] && i != tile)) { valid = false; break; }
        sum += i + 1;
        if(sum > remaining) { valid = false; break; }
      }
    }
    if(valid && sum == remaining) return true;
  }
  return false;
}

static void move_cursor(int dir) {
  int start = s_cursor;
  do {
    s_cursor = (s_cursor + dir + s_num_tiles) % s_num_tiles;
  } while(!tile_selectable(s_cursor) && s_cursor != start);
}

static void cursor_to_first_open(void) {
  s_cursor = 0;
  while(s_cursor < s_num_tiles && !tile_selectable(s_cursor)) s_cursor++;
  if(s_cursor >= s_num_tiles) s_cursor = 0;
}

static void next_player(void) {
  s_cur_idx = (s_cur_idx + 1) % s_num_players;
}

// ============================================================================
// CANVAS RENDERING
// ============================================================================
static void canvas_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  int w = b.size.w, h = b.size.h;
  int pad = PBL_IF_ROUND_ELSE(18, 4);

  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorFromHEX(0x002244));
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // ======== SETUP (player count) ========
  if(s_state == ST_SETUP) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SHUT", f_lg,
      GRect(0, h*5/100, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "THE BOX", f_md,
      GRect(0, h*5/100+30, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    const char *opts[] = {"2 Players","3 Players","4 Players","5 Players","6 Players"};
    int cy = h * 42 / 100;

    graphics_context_set_text_color(ctx, GColorLightGray);
    if(s_icon_font_20) {
      graphics_draw_text(ctx, "\xEF\x83\x98", s_icon_font_20,
        GRect(w/2-15, cy-28, 30, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      graphics_draw_text(ctx, "\xEF\x83\x97", s_icon_font_20,
        GRect(w/2-15, cy+30, 30, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x004466));
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_rect(ctx, GRect(40, cy+2, w-80, 30), 6, GCornersAll);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorWhite);
    #else
    graphics_context_set_text_color(ctx, GColorBlack);
    #endif
    graphics_draw_text(ctx, opts[s_setup_cursor], f_lg,
      GRect(0, cy-2, w, 30),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT to continue", f_sm,
      GRect(0, h*78/100, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== SETTINGS (tiles + win mode) ========
  else if(s_state == ST_SETTINGS) {
    graphics_context_set_text_color(ctx, GColorWhite);
    int title_y = PBL_IF_ROUND_ELSE(pad + 10, pad + 4);
    graphics_draw_text(ctx, "Settings", f_lg,
      GRect(0, title_y, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int oy = title_y + 44;
    int row_h = 36;
    int mx = PBL_IF_ROUND_ELSE(40, 20);

    // Row 0: Tiles
    {
      bool sel = (s_settings_cursor == 0);
      #ifdef PBL_COLOR
      if(sel) {
        graphics_context_set_fill_color(ctx, GColorFromHEX(0x004466));
        graphics_fill_rect(ctx, GRect(mx, oy-2, w-mx*2, row_h-4), 6, GCornersAll);
      }
      graphics_context_set_text_color(ctx, sel ? GColorYellow : GColorLightGray);
      #else
      graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
      #endif
      graphics_draw_text(ctx, "Tiles:", f_sm,
        GRect(mx+6, oy, 50, 18),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      char tb[8]; snprintf(tb, sizeof(tb), "%d", s_num_tiles);
      graphics_draw_text(ctx, tb, f_md,
        GRect(mx+6, oy+2, w-mx*2-12, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
    oy += row_h;

    // Row 1: Win mode
    {
      bool sel = (s_settings_cursor == 1);
      #ifdef PBL_COLOR
      if(sel) {
        graphics_context_set_fill_color(ctx, GColorFromHEX(0x004466));
        graphics_fill_rect(ctx, GRect(mx, oy-2, w-mx*2, row_h-4), 6, GCornersAll);
      }
      graphics_context_set_text_color(ctx, sel ? GColorYellow : GColorLightGray);
      #else
      graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
      #endif
      graphics_draw_text(ctx, "Win:", f_sm,
        GRect(mx+6, oy, 40, 18),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      const char *wm = (s_win_mode == WIN_SUM) ? "Lowest Sum" : "Fewest Tiles";
      graphics_draw_text(ctx, wm, f_md,
        GRect(mx+6, oy+2, w-mx*2-12, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
    oy += row_h + 8;

    // Row 2: Start button
    {
      bool sel = (s_settings_cursor == 2);
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, sel ? GColorGreen : GColorFromHEX(0x003300));
      #else
      graphics_context_set_fill_color(ctx, sel ? GColorWhite : GColorDarkGray);
      #endif
      int bw = 100, bx = (w - bw) / 2;
      graphics_fill_rect(ctx, GRect(bx, oy, bw, 28), 6, GCornersAll);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, sel ? GColorBlack : GColorLightGray);
      #else
      graphics_context_set_text_color(ctx, sel ? GColorBlack : GColorWhite);
      #endif
      graphics_draw_text(ctx, "START", f_md,
        GRect(bx, oy + 3, bw, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT to change / start", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== ORDER ========
  else if(s_state == ST_ORDER) {
    graphics_context_set_text_color(ctx, GColorWhite);
    int title_y = PBL_IF_ROUND_ELSE(pad + 8, pad + 2);
    graphics_draw_text(ctx, "Players", f_lg,
      GRect(0, title_y, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cols = 2, rows_n = (s_num_players + 1) / 2;
    int cell_w = (w - pad*2) / cols, cell_h = 50;
    int grid_h = rows_n * cell_h;
    int grid_y = title_y + 36;
    int avail = h - grid_y - 50;
    if(grid_h < avail) grid_y += (avail - grid_h) / 2;

    for(int i = 0; i < s_num_players; i++) {
      int pi = s_order[i];
      int col = i % cols, row = i / cols;
      int cx = pad + col * cell_w + cell_w / 2;
      int cy = grid_y + row * cell_h + 18;
      draw_token(ctx, cx, cy, s_players[pi].icon, true);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, s_tok_name[s_players[pi].icon], f_sm,
        GRect(cx - cell_w/2, cy + 16, cell_w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "Everyone choose a symbol", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(42, 34), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT to deal", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(26, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== ROLL / SELECT / BUST ========
  else if(s_state == ST_ROLL || s_state == ST_SELECT || s_state == ST_BUST) {
    int cp = cur_player();
    Player *p = &s_players[cp];

    int icon_y = PBL_IF_ROUND_ELSE(32, 18);
    draw_token(ctx, w/2, icon_y, p->icon, true);

    int tiles_y = icon_y + 24;
    draw_tiles(ctx, w, p, tiles_y);

    // Dice area below tiles
    int tw = (s_num_tiles == 12) ? 24 : 28;
    int dice_y = tiles_y + 2*(tw + 4) + 10;
    int die_sz = 30;

    if(s_state == ST_ROLL) {
      graphics_context_set_text_color(ctx, GColorWhite);
      char rt[24];
      if(s_win_mode == WIN_TILES)
        snprintf(rt, sizeof(rt), "Open: %d tiles", remaining_count(cp));
      else
        snprintf(rt, sizeof(rt), "Remaining: %d", remaining_sum(cp));
      graphics_draw_text(ctx, rt, f_sm,
        GRect(0, dice_y, w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      graphics_draw_text(ctx, "SELECT to roll", f_md,
        GRect(0, dice_y + 20, w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      if(s_one_die) {
        graphics_context_set_text_color(ctx, GColorLightGray);
        graphics_draw_text(ctx, "(1 die)", f_sm,
          GRect(0, dice_y + 42, w, 16),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }
    } else {
      // Dice + sum on same line
      {
        int dice_cx = w/2 - 20;
        int sum_x;
        if(s_one_die) {
          draw_die(ctx, dice_cx, dice_y + die_sz/2, die_sz, s_dice[0]);
          sum_x = dice_cx + die_sz/2 + 6;
        } else {
          draw_die(ctx, dice_cx - die_sz/2 - 2, dice_y + die_sz/2, die_sz, s_dice[0]);
          draw_die(ctx, dice_cx + die_sz/2 + 2, dice_y + die_sz/2, die_sz, s_dice[1]);
          sum_x = dice_cx + die_sz + 8;
        }
        char sb[16]; snprintf(sb, sizeof(sb), "=%d", s_dice_sum);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, sb, f_md,
          GRect(sum_x, dice_y + die_sz/2 - 11, 40, 22),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      }

      if(s_state == ST_SELECT && s_selected_sum > 0) {
        char sel[20];
        snprintf(sel, sizeof(sel), "Selected: %d / %d", s_selected_sum, s_dice_sum);
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx,
          s_selected_sum == s_dice_sum ? GColorGreen : GColorYellow);
        #endif
        graphics_draw_text(ctx, sel, f_sm,
          GRect(0, dice_y + die_sz + 4, w, 16),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }

      if(s_state == ST_BUST) {
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, GColorRed);
        #else
        graphics_context_set_text_color(ctx, GColorWhite);
        #endif
        graphics_draw_text(ctx, "BUST!", f_lg,
          GRect(0, dice_y + die_sz + 2, w, 32),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        graphics_context_set_text_color(ctx, GColorWhite);
        char sc[20];
        if(s_win_mode == WIN_TILES)
          snprintf(sc, sizeof(sc), "%d tiles left", remaining_count(cp));
        else
          snprintf(sc, sizeof(sc), "Score: %d", p->score);
        graphics_draw_text(ctx, sc, f_sm,
          GRect(0, dice_y + die_sz + 32, w, 16),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, "SELECT to continue", f_sm,
          GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }
    }
  }

  // ======== GAMEOVER ========
  else if(s_state == ST_GAMEOVER) {
    int title_y = PBL_IF_ROUND_ELSE(pad + 10, pad);

    int winner_pi = -1;
    for(int i = 0; i < s_num_players; i++)
      if(s_players[i].shut_all) { winner_pi = i; break; }

    if(winner_pi >= 0) {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorYellow);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      graphics_draw_text(ctx, "SHUT!", f_lg,
        GRect(0, title_y, w, 34),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      draw_token(ctx, w/2, title_y + 50, s_players[winner_pi].icon, true);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, "shut the box!", f_md,
        GRect(0, title_y + 70, w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorYellow);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      graphics_draw_text(ctx, "GAME OVER!", f_lg,
        GRect(0, title_y, w, 34),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

      int lo = 999;
      for(int i = 0; i < s_num_players; i++)
        if(s_players[i].score < lo) lo = s_players[i].score;

      int ly = title_y + 38;
      int rh = (s_num_players <= 4) ? 30 : 24;
      int lx = PBL_IF_ROUND_ELSE(pad + 26, pad + 12);
      for(int i = 0; i < s_num_players; i++) {
        int pi = s_order[i];
        bool win = (s_players[pi].score == lo);
        draw_token(ctx, lx, ly + rh/2, s_players[pi].icon, false);
        char sc[20];
        if(s_win_mode == WIN_TILES) {
          if(win)
            snprintf(sc, sizeof(sc), "%d tiles  WIN", s_players[pi].score);
          else
            snprintf(sc, sizeof(sc), "%d tiles", s_players[pi].score);
        } else {
          if(win)
            snprintf(sc, sizeof(sc), "%d pts  WIN", s_players[pi].score);
          else
            snprintf(sc, sizeof(sc), "%d pts", s_players[pi].score);
        }
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, win ? GColorYellow : GColorWhite);
        #else
        graphics_context_set_text_color(ctx, GColorWhite);
        #endif
        graphics_draw_text(ctx, sc, win ? f_md : f_sm,
          GRect(lx + 18, ly + (rh-18)/2, w - lx - 22, 20),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        ly += rh;
      }
    }

    // Rematch / New Game buttons
    int btn_y = h - PBL_IF_ROUND_ELSE(42, 34);
    int btn_w = 80;
    int btn_gap = 8;
    int btn_x0 = (w - btn_w * 2 - btn_gap) / 2;
    int btn_x1 = btn_x0 + btn_w + btn_gap;
    for(int bi = 0; bi < 2; bi++) {
      bool bsel = (s_gameover_cursor == bi);
      int bx = (bi == 0) ? btn_x0 : btn_x1;
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, bsel ? GColorGreen : GColorFromHEX(0x003300));
      #else
      graphics_context_set_fill_color(ctx, bsel ? GColorWhite : GColorDarkGray);
      #endif
      graphics_fill_rect(ctx, GRect(bx, btn_y, btn_w, 22), 6, GCornersAll);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, bsel ? GColorBlack : GColorLightGray);
      #else
      graphics_context_set_text_color(ctx, bsel ? GColorBlack : GColorWhite);
      #endif
      graphics_draw_text(ctx, bi == 0 ? "Rematch" : "New Game",
        f_sm, GRect(bx, btn_y + 2, btn_w, 18),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }

  // ======== SCORES OVERLAY (hold UP) ========
  if(s_show_scores && (s_state == ST_ROLL || s_state == ST_SELECT)) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    int op = pad + 8;
    graphics_fill_rect(ctx, GRect(op, op, w-op*2, h-op*2), 8, GCornersAll);
    int ly = op + 6;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SCORES", f_md,
      GRect(op, ly, w-op*2, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    ly += 28;

    int sx = PBL_IF_ROUND_ELSE((w - 120) / 2, op + 14);
    for(int i = 0; i < s_num_players; i++) {
      int pi = s_order[i];
      bool cur = (i == s_cur_idx);
      bool done = (s_players[pi].score > 0 || s_players[pi].shut_all);
      draw_token(ctx, sx, ly + 10, s_players[pi].icon, false);

      char info[24];
      if(s_players[pi].shut_all)
        snprintf(info, sizeof(info), "SHUT!");
      else if(done) {
        if(s_win_mode == WIN_TILES)
          snprintf(info, sizeof(info), "%d tiles", s_players[pi].score);
        else
          snprintf(info, sizeof(info), "%d pts", s_players[pi].score);
      } else
        snprintf(info, sizeof(info), "playing...");

      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, cur ? GColorYellow : GColorWhite);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      graphics_draw_text(ctx, info, cur ? f_md : f_sm,
        GRect(sx + 18, ly + 2, w - sx - 22, 20),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      ly += 24;
    }
  }
}

// ============================================================================
// BUTTON HANDLERS
// ============================================================================
static void select_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_num_players = s_setup_cursor + 2;
    shuffle_order();
    s_settings_cursor = 0;
    s_state = ST_SETTINGS;
  }
  else if(s_state == ST_SETTINGS) {
    if(s_settings_cursor == 0) {
      // Cycle tile count
      s_tile_opt_idx = (s_tile_opt_idx + 1) % 3;
      s_num_tiles = s_tile_opts[s_tile_opt_idx];
    } else if(s_settings_cursor == 1) {
      // Toggle win mode
      s_win_mode = (s_win_mode == WIN_SUM) ? WIN_TILES : WIN_SUM;
    } else {
      // Start
      for(int i = 0; i < s_num_players; i++) init_player_boxes(i);
      s_cur_idx = 0;
      s_one_die = false;
      s_state = ST_ORDER;
    }
  }
  else if(s_state == ST_ORDER) {
    s_state = ST_ROLL;
  }
  else if(s_state == ST_ROLL) {
    int cp = cur_player();
    roll_dice();
    if(can_make_sum(cp, s_dice_sum)) {
      cursor_to_first_open();
      s_state = ST_SELECT;
    } else {
      s_players[cp].score = player_score(cp);
      s_state = ST_BUST;
      vibes_long_pulse();
    }
  }
  else if(s_state == ST_SELECT) {
    int cp = cur_player();
    int tile = s_cursor;
    if(tile < s_num_tiles && s_players[cp].open[tile]) {
      if(s_selected[tile]) {
        s_selected[tile] = false;
        s_selected_sum -= (tile + 1);
      } else if(s_selected_sum + tile + 1 <= s_dice_sum) {
        s_selected[tile] = true;
        s_selected_sum += (tile + 1);
      }

      // After toggle, reposition cursor if current tile no longer selectable
      if(!tile_selectable(s_cursor)) move_cursor(1);

      if(s_selected_sum == s_dice_sum) {
        for(int i = 0; i < s_num_tiles; i++)
          if(s_selected[i]) s_players[cp].open[i] = false;
        for(int i = 0; i < MAX_TILES; i++) s_selected[i] = false;
        s_selected_sum = 0;

        if(all_shut(cp)) {
          s_players[cp].shut_all = true;
          s_gameover_cursor = 0;
          s_state = ST_GAMEOVER;
          vibes_short_pulse();
        } else {
          s_one_die = check_one_die(cp);
          s_state = ST_ROLL;
        }
      }
    }
  }
  else if(s_state == ST_BUST) {
    next_player();
    if(s_cur_idx == 0) {
      s_gameover_cursor = 0;
      s_state = ST_GAMEOVER;
    } else {
      s_state = ST_ROLL;
    }
  }
  else if(s_state == ST_GAMEOVER) {
    if(s_gameover_cursor == 0) {
      // Rematch — same players, same settings
      for(int i = 0; i < s_num_players; i++) init_player_boxes(i);
      s_cur_idx = 0;
      s_one_die = false;
      s_state = ST_ROLL;
    } else {
      // New game
      s_state = ST_SETUP;
      s_setup_cursor = s_num_players - 2;
    }
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_setup_cursor = (s_setup_cursor + 4) % 5;
  } else if(s_state == ST_GAMEOVER) {
    s_gameover_cursor = (s_gameover_cursor + 1) % 2;
  } else if(s_state == ST_SETTINGS) {
    s_settings_cursor = (s_settings_cursor + 2) % 3;
  } else if(s_state == ST_SELECT) {
    move_cursor(-1);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_setup_cursor = (s_setup_cursor + 1) % 5;
  } else if(s_state == ST_GAMEOVER) {
    s_gameover_cursor = (s_gameover_cursor + 1) % 2;
  } else if(s_state == ST_SETTINGS) {
    s_settings_cursor = (s_settings_cursor + 1) % 3;
  } else if(s_state == ST_SELECT) {
    move_cursor(1);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP || s_state == ST_GAMEOVER)
    window_stack_pop(true);
  else if(s_state == ST_SETTINGS) {
    s_state = ST_SETUP;
    if(s_canvas) layer_mark_dirty(s_canvas);
  } else {
    s_state = ST_SETUP; s_setup_cursor = 0;
    if(s_canvas) layer_mark_dirty(s_canvas);
  }
}

static void up_long_down(ClickRecognizerRef ref, void *ctx) {
  s_show_scores = true; if(s_canvas) layer_mark_dirty(s_canvas);
}
static void up_long_up(ClickRecognizerRef ref, void *ctx) {
  s_show_scores = false; if(s_canvas) layer_mark_dirty(s_canvas);
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_down, up_long_up);
}

// ============================================================================
// WINDOW & LIFECYCLE
// ============================================================================
static void win_load(Window *w) {
  Layer *wl = window_get_root_layer(w);
  GRect b = layer_get_bounds(wl);
  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, canvas_proc);
  layer_add_child(wl, s_canvas);
  window_set_click_config_provider(w, click_config);
  s_state = ST_SETUP; s_setup_cursor = 0;
}

static void win_unload(Window *w) {
  if(s_canvas) { layer_destroy(s_canvas); s_canvas = NULL; }
}

static void init(void) {
  srand(time(NULL));
  s_icon_font_20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_20));
  s_icon_font_14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_14));
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_window_handlers(s_win, (WindowHandlers){.load=win_load,.unload=win_unload});
  window_stack_push(s_win, true);
}

static void deinit(void) {
  window_destroy(s_win);
  fonts_unload_custom_font(s_icon_font_20);
  fonts_unload_custom_font(s_icon_font_14);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
