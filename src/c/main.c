/**
 * Shut the Box — Dice game for Pebble
 * Targets: emery (Time 2), gabbro (Round 2)
 *
 * 2-6 players. Each player has tiles 1-9. Roll dice, shut tiles
 * that sum to the roll. If no valid combo, you bust. Lowest
 * remaining sum wins. Shutting all tiles wins immediately.
 */

#include <pebble.h>
#include <stdlib.h>

#define MAX_PLAYERS 6
#define NUM_TILES   9
#define NUM_TOKENS  6

enum { ST_SETUP, ST_ORDER, ST_ROLL, ST_SELECT, ST_BUST, ST_GAMEOVER };

typedef struct {
  bool open[NUM_TILES]; // true = open, false = shut
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

static Player s_players[MAX_PLAYERS];
static int    s_order[MAX_PLAYERS];
static int    s_cur_idx;

static int  s_dice[2];
static int  s_dice_sum;
static bool s_one_die;        // only 1 die if all open tiles <= 6
static bool s_selected[NUM_TILES]; // tiles selected for current shut
static int  s_selected_sum;

static bool s_show_scores;

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

// Draw a number tile
static void draw_tile(GContext *ctx, int x, int y, int tw, int th,
                      int num, bool is_open, bool selected, bool cursor) {
  if(is_open) {
    // Open tile — tan/wood color
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, selected ? GColorYellow :
      GColorFromHEX(0xAA8800));
    #else
    graphics_context_set_fill_color(ctx, selected ? GColorWhite : GColorLightGray);
    #endif
    graphics_fill_rect(ctx, GRect(x, y, tw, th), 3, GCornersAll);
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, cursor ? GColorWhite : GColorFromHEX(0x554400));
    #else
    graphics_context_set_stroke_color(ctx, cursor ? GColorWhite : GColorBlack);
    #endif
    graphics_context_set_stroke_width(ctx, cursor ? 2 : 1);
    graphics_draw_round_rect(ctx, GRect(x, y, tw, th), 3);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, selected ? GColorBlack : GColorWhite);
    #else
    graphics_context_set_text_color(ctx, selected ? GColorBlack : GColorBlack);
    #endif
  } else {
    // Shut tile — dark/dimmed
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
  graphics_draw_text(ctx, nb,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(x, y - 1, tw, th + 2),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
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
  for(int i = 0; i < NUM_TILES; i++) s_players[pi].open[i] = true;
  s_players[pi].score = 0;
  s_players[pi].shut_all = false;
}

static int remaining_sum(int pi) {
  int s = 0;
  for(int i = 0; i < NUM_TILES; i++)
    if(s_players[pi].open[i]) s += i + 1;
  return s;
}

static bool all_shut(int pi) {
  for(int i = 0; i < NUM_TILES; i++)
    if(s_players[pi].open[i]) return false;
  return true;
}

static bool check_one_die(int pi) {
  // Use 1 die if all open tiles are <= 6
  for(int i = 6; i < NUM_TILES; i++)
    if(s_players[pi].open[i]) return false;
  return true;
}

// Check if any subset of open tiles sums to target
static bool can_make_sum(int pi, int target) {
  bool *open = s_players[pi].open;
  for(int mask = 1; mask < (1 << NUM_TILES); mask++) {
    int sum = 0;
    bool valid = true;
    for(int i = 0; i < NUM_TILES; i++) {
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
  for(int i = 0; i < NUM_TILES; i++) s_selected[i] = false;
  s_selected_sum = 0;
}

// Move cursor to next open tile in direction
static void move_cursor(int dir) {
  int cp = cur_player();
  int start = s_cursor;
  do {
    s_cursor = (s_cursor + dir + NUM_TILES) % NUM_TILES;
  } while(!s_players[cp].open[s_cursor] && s_cursor != start);
}

// Find first open tile for cursor
static void cursor_to_first_open(void) {
  int cp = cur_player();
  s_cursor = 0;
  while(s_cursor < NUM_TILES && !s_players[cp].open[s_cursor]) s_cursor++;
  if(s_cursor >= NUM_TILES) s_cursor = 0;
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

  // ======== SETUP ========
  if(s_state == ST_SETUP) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SHUT", f_lg,
      GRect(0, h*5/100, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "THE BOX", f_md,
      GRect(0, h*5/100 + 30, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    const char *opts[] = {"2 Players","3 Players","4 Players","5 Players","6 Players"};
    int cy = h * 42 / 100;

    // Up arrow
    graphics_context_set_text_color(ctx, GColorLightGray);
    if(s_icon_font_20)
      graphics_draw_text(ctx, "\xEF\x83\x98", s_icon_font_20,
        GRect(w/2-15, cy-28, 30, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x004466));
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_rect(ctx, GRect(40, cy-2, w-80, 30), 6, GCornersAll);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorWhite);
    #else
    graphics_context_set_text_color(ctx, GColorBlack);
    #endif
    graphics_draw_text(ctx, opts[s_setup_cursor], f_lg,
      GRect(0, cy-2, w, 30),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Down arrow
    graphics_context_set_text_color(ctx, GColorLightGray);
    if(s_icon_font_20)
      graphics_draw_text(ctx, "\xEF\x83\x97", s_icon_font_20,
        GRect(w/2-15, cy+30, 30, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT to start", f_sm,
      GRect(0, h*78/100, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== ORDER ========
  else if(s_state == ST_ORDER) {
    graphics_context_set_text_color(ctx, GColorWhite);
    int title_y = PBL_IF_ROUND_ELSE(pad + 8, pad + 2);
    graphics_draw_text(ctx, "Players", f_lg,
      GRect(0, title_y, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cols = 2;
    int rows_n = (s_num_players + 1) / 2;
    int cell_w = (w - pad * 2) / cols;
    int cell_h = 50;
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
    graphics_draw_text(ctx, "SELECT to start", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(26, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== ROLL / SELECT / BUST ========
  else if(s_state == ST_ROLL || s_state == ST_SELECT || s_state == ST_BUST) {
    int cp = cur_player();
    Player *p = &s_players[cp];

    // Icon
    int icon_y = PBL_IF_ROUND_ELSE(32, 18);
    draw_token(ctx, w/2, icon_y, p->icon, true);

    // Tiles: row1 = 1-5, row2 = 6-9
    int tw = 28, th = 28, tgap = 4;
    int row1_w = 5 * tw + 4 * tgap; // 156
    int row2_w = 4 * tw + 3 * tgap; // 124
    int row1_x = (w - row1_w) / 2;
    int row2_x = (w - row2_w) / 2;
    int tiles_y = icon_y + 24;

    for(int i = 0; i < 5; i++) {
      int tx = row1_x + i * (tw + tgap);
      draw_tile(ctx, tx, tiles_y, tw, th, i+1, p->open[i],
        s_selected[i], s_state == ST_SELECT && s_cursor == i);
    }
    for(int i = 0; i < 4; i++) {
      int tx = row2_x + i * (tw + tgap);
      draw_tile(ctx, tx, tiles_y + th + tgap, tw, th, i+6, p->open[i+5],
        s_selected[i+5], s_state == ST_SELECT && s_cursor == i+5);
    }

    // Dice
    int dice_y = tiles_y + 2*(th + tgap) + 10;
    int die_sz = 30;
    if(s_state == ST_ROLL) {
      // Show "SELECT to roll"
      graphics_context_set_text_color(ctx, GColorWhite);
      char roll_txt[24];
      snprintf(roll_txt, sizeof(roll_txt), "Remaining: %d", remaining_sum(cp));
      graphics_draw_text(ctx, roll_txt, f_sm,
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
      // Show dice and sum
      if(s_one_die) {
        draw_die(ctx, w/2, dice_y + die_sz/2, die_sz, s_dice[0]);
      } else {
        draw_die(ctx, w/2 - die_sz/2 - 4, dice_y + die_sz/2, die_sz, s_dice[0]);
        draw_die(ctx, w/2 + die_sz/2 + 4, dice_y + die_sz/2, die_sz, s_dice[1]);
      }

      // Sum display
      char sum_buf[16];
      snprintf(sum_buf, sizeof(sum_buf), "= %d", s_dice_sum);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, sum_buf, f_md,
        GRect(0, dice_y + die_sz + 2, w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

      if(s_state == ST_SELECT && s_selected_sum > 0) {
        // Show selected sum progress
        char sel_buf[20];
        snprintf(sel_buf, sizeof(sel_buf), "Selected: %d / %d",
          s_selected_sum, s_dice_sum);
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx,
          s_selected_sum == s_dice_sum ? GColorGreen : GColorYellow);
        #endif
        graphics_draw_text(ctx, sel_buf, f_sm,
          GRect(0, dice_y + die_sz + 22, w, 16),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }

      if(s_state == ST_BUST) {
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, GColorRed);
        #else
        graphics_context_set_text_color(ctx, GColorWhite);
        #endif
        graphics_draw_text(ctx, "BUST!", f_lg,
          GRect(0, dice_y + die_sz + 4, w, 32),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        graphics_context_set_text_color(ctx, GColorWhite);
        char sc[16];
        snprintf(sc, sizeof(sc), "Score: %d", p->score);
        graphics_draw_text(ctx, sc, f_sm,
          GRect(0, dice_y + die_sz + 34, w, 16),
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

    // Check for shut-the-box winner
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

      // Find lowest score
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
        char sc[16];
        if(win)
          snprintf(sc, sizeof(sc), "%d pts  WIN", s_players[pi].score);
        else
          snprintf(sc, sizeof(sc), "%d pts", s_players[pi].score);
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, win ? GColorYellow : GColorWhite);
        #else
        graphics_context_set_text_color(ctx, GColorWhite);
        #endif
        graphics_draw_text(ctx, sc, win ? f_md : f_sm,
          GRect(lx + 18, ly + (rh - 18)/2, w - lx - 22, 20),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        ly += rh;
      }
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT: new game", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(26, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
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
      else if(done)
        snprintf(info, sizeof(info), "%d pts", s_players[pi].score);
      else
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
    s_state = ST_ORDER;
  }
  else if(s_state == ST_ORDER) {
    // Init all players
    for(int i = 0; i < s_num_players; i++) init_player_boxes(i);
    s_cur_idx = 0;
    s_one_die = false;
    s_state = ST_ROLL;
  }
  else if(s_state == ST_ROLL) {
    int cp = cur_player();
    roll_dice();
    if(can_make_sum(cp, s_dice_sum)) {
      cursor_to_first_open();
      s_state = ST_SELECT;
    } else {
      // Bust!
      s_players[cp].score = remaining_sum(cp);
      s_state = ST_BUST;
      vibes_long_pulse();
    }
  }
  else if(s_state == ST_SELECT) {
    int cp = cur_player();
    int tile = s_cursor;
    if(tile < NUM_TILES && s_players[cp].open[tile]) {
      if(s_selected[tile]) {
        // Deselect
        s_selected[tile] = false;
        s_selected_sum -= (tile + 1);
      } else if(s_selected_sum + tile + 1 <= s_dice_sum) {
        // Select (only if won't exceed sum)
        s_selected[tile] = true;
        s_selected_sum += (tile + 1);
      }

      // Check if selected sum matches dice
      if(s_selected_sum == s_dice_sum) {
        // Shut the selected tiles
        for(int i = 0; i < NUM_TILES; i++)
          if(s_selected[i]) s_players[cp].open[i] = false;
        for(int i = 0; i < NUM_TILES; i++) s_selected[i] = false;
        s_selected_sum = 0;

        // Check for shut the box
        if(all_shut(cp)) {
          s_players[cp].shut_all = true;
          s_state = ST_GAMEOVER;
          vibes_short_pulse();
        } else {
          // Continue rolling
          s_one_die = check_one_die(cp);
          s_state = ST_ROLL;
        }
      }
    }
  }
  else if(s_state == ST_BUST) {
    // Next player or game over
    next_player();
    if(s_cur_idx == 0) {
      // All players done
      s_state = ST_GAMEOVER;
    } else {
      s_state = ST_ROLL;
    }
  }
  else if(s_state == ST_GAMEOVER) {
    s_state = ST_SETUP;
    s_setup_cursor = s_num_players - 2;
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_setup_cursor = (s_setup_cursor + 4) % 5;
  } else if(s_state == ST_SELECT) {
    move_cursor(-1);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_setup_cursor = (s_setup_cursor + 1) % 5;
  } else if(s_state == ST_SELECT) {
    move_cursor(1);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP || s_state == ST_GAMEOVER)
    window_stack_pop(true);
  else {
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
