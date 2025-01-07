#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define SOKOL_METAL

#include "cimgui.h"
#include "fa_regular_400.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_imgui.h"
#include "sokol_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// if mouse movement between mouse down and mouse up is below this threshold (in
// px) then it is considered to be a mouse click
#define CLICK_THRESHOLD 1

// if the distance between mouse pos when clicked and the entity is below this
// threshold then the entity is considered to be selected
#define SELECT_THRESHOLD 25

#define ARENA_INITIAL_SIZE 5000

// ============================================================================
// utils/helpers
// ============================================================================

float vec2_magnitude_sqr(const ImVec2 *v) { return v->x * v->x + v->y * v->y; }

float vec2_distance_sqr(const ImVec2 *v1, const ImVec2 *v2) {
  return (v1->x - v2->x) * (v1->x - v2->x) + (v1->y - v2->y) * (v1->y - v2->y);
}

void project_point_to_segment(ImVec2 *out, const ImVec2 *v1, const ImVec2 *v2,
                              const ImVec2 *point) {
  // Consider:
  //
  // a  = vector v1 -> point
  // b  = vector v1 -> v2
  // b^ = unit vector of b
  //
  // then:
  //
  // projection of a on b = (a dot b) / ||b||
  // projection vector = (a dot b / ||b||)b^
  //                   = (a dot b / b dot b)b
  //
  // resultant point from origin = v1 + projection vector

  ImVec2 a = {
      point->x - v1->x,
      point->y - v1->y,
  };
  ImVec2 b = {
      v2->x - v1->x,
      v2->y - v1->y,
  };

  float a_dot_b = a.x * b.x + a.y * b.y;
  // if dot product is less than zero, then the point is outside of segment v1v2
  // closer to v1
  if (a_dot_b < 0) {
    out->x = v1->x;
    out->y = v1->y;
    return;
  }

  float b_dot_b = b.x * b.x + b.y * b.y;
  // if a dot b > b dot b, then the projection magnitude > ||b||
  // i.e. the point falls outside of segment v1v2 closer to v2
  if (a_dot_b > b_dot_b) {
    out->x = v2->x;
    out->y = v2->y;
    return;
  }

  float frac = a_dot_b / b_dot_b;

  // v1 + projection vector = resultant
  out->x = v1->x + frac * b.x;
  out->y = v1->y + frac * b.y;
}

void vec2_move(ImVec2 *vec, const ImVec2 *delta) {
  vec->x += delta->x;
  vec->y += delta->y;
}

bool vec2_is_in_area(const ImVec2 *vec, const ImVec2 *point_a,
                     const ImVec2 *point_b) {
  float a_to_vec_delta_x = vec->x - point_a->x;
  float b_to_vec_delta_x = vec->x - point_b->x;
  float a_to_vec_delta_y = vec->y - point_a->y;
  float b_to_vec_delta_y = vec->y - point_b->y;
  return (a_to_vec_delta_x >= 0 && b_to_vec_delta_x <= 0 ||
          b_to_vec_delta_x >= 0 && a_to_vec_delta_x <= 0) &&
         (a_to_vec_delta_y >= 0 && b_to_vec_delta_y <= 0 ||
          b_to_vec_delta_y >= 0 && a_to_vec_delta_y <= 0);
}

bool is_mouse_click(const ImVec2 *mouse_down_pos, const ImVec2 *mouse_up_pos) {
  if (igIsMouseReleased_Nil(ImGuiMouseButton_Left)) {
    return vec2_distance_sqr(mouse_up_pos, mouse_down_pos) <= CLICK_THRESHOLD;
  }
  return false;
}

// ============================================================================
// struct definitions
// ============================================================================

// ===========================
// struct: arena
// ===========================

typedef struct arena_page {
  void *data;
  size_t end_ptr;
  struct arena_page *next;
} arena_page_t;

typedef struct {
  arena_page_t *page;
  size_t capacity;
} arena_t;

arena_page_t *arena_new_page(size_t page_size) {
  arena_page_t *new_page = malloc(sizeof(arena_page_t));
  new_page->data = malloc(sizeof(char) * page_size);
  new_page->next = NULL;
  new_page->end_ptr = 0;
  return new_page;
}

arena_t *arena_alloc(size_t size) {
  arena_t *arena = malloc(sizeof(arena_t));
  arena->page = arena_new_page(size);
  arena->capacity = size;
  return arena;
}

void *arena_push(arena_t *arena, size_t size) {
  if (arena->page->end_ptr + size >= arena->capacity) {
    arena_page_t *new_page = arena_new_page(arena->capacity);
    new_page->next = arena->page;
    arena->page = new_page;
    printf("arena realloc\n");
  }
  void *ptr = arena->page->data + arena->page->end_ptr;
  arena->page->end_ptr += size;
  return ptr;
}

void arena_free(arena_t *arena) {
  printf("arena_free\n");
  for (arena_page_t *page = arena->page; page != NULL; page = page->next) {
    free(page->data);
    free(page);
  }
  free(arena);
}

// ===========================
// struct: point_list
// ===========================

typedef struct {
  ImVec2 *items;
  size_t length;
  size_t capacity;
} point_list_t;

point_list_t point_list_alloc(size_t capacity) {
  ImVec2 *items = malloc(sizeof(ImVec2) * capacity);
  return (point_list_t){
      .items = items,
      .length = 0,
      .capacity = capacity,
  };
}

ImVec2 *point_list_push(point_list_t *list) {
  if (list->length + 1 >= list->capacity) {
    list->capacity *= 2;
    list->items = realloc(list->items, sizeof(ImVec2) * list->capacity);
  }
  ImVec2 *item = list->items + list->length;
  list->length += 1;
  return item;
}

void point_list_clear(point_list_t *list) { list->length = 0; }

void point_list_copy(point_list_t *dest, point_list_t *src) {
  memcpy(dest->items, src->items, sizeof(ImVec2) * src->capacity);
  dest->capacity = src->capacity;
  dest->length = src->length;
}

void point_list_free(point_list_t *list) { free(list->items); }

// ===========================
// struct: entity
// ===========================

typedef enum {
  entity_flag_rect = 1 << 0,
  entity_flag_path = 1 << 1,
  entity_flag_selected = 1 << 2,
} entity_flag_t;

typedef struct entity {
  entity_flag_t flags;

  point_list_t points;
  ImColor color;

  struct entity *next;
  struct entity *prev;
} entity_t;

typedef struct selected_entity {
  entity_t *entity;
  struct selected_entity *next;
} selected_entity_t;

// ===========================
// struct: window info
// ===========================

typedef struct {
  ImVec2 dimension;
  ImVec2 position_top_left;
  ImVec2 position_bottom_right;
} window_info_t;

// ===========================
// struct: game state
// ===========================

typedef enum {
  toolbox_button_select,
  toolbox_button_draw,
  toolbox_button_rectangle,
} toolbox_button_kind_t;

typedef struct {
  arena_t *arena;

  ImFont *fa_font;
  sg_pass_action pass_action;

  window_info_t color_picker_window;
  ImVec2 color_picker_window_size;

  entity_t *entities;
  entity_t *freed_entity;
  bool has_selected_entities;
  entity_t *selected_entity;
  bool is_area_selecting;

  bool is_mouse_down;
  bool is_prev_mouse_down;
  bool is_moving_entities;
  point_list_t points;
  ImVec2 drag_start;
  ImVec2 last_mouse_pos;

  toolbox_button_kind_t selected_toolbox_button;
  bool is_color_picker_changing;
  ImColor picked_color;
} state_t;

entity_t *entity_alloc(state_t *state, size_t point_count) {
  entity_t *entity = state->freed_entity;
  if (entity) {
    state->freed_entity = state->freed_entity->next;
  } else {
    entity = arena_push(state->arena, sizeof(entity_t));
    entity->points = point_list_alloc(point_count);
  }

  entity->flags = 0;
  entity->next = NULL;
  entity->prev = NULL;

  return entity;
}

void entity_recycle(state_t *state, entity_t *entity) {
  entity->next = state->freed_entity;
  entity->prev = NULL;
  state->freed_entity = entity;
  point_list_clear(&entity->points);
}

void entity_free(entity_t *entity) { point_list_free(&entity->points); }

void push_entity(state_t *state, entity_t *entity) {
  entity->next = state->entities;
  if (state->entities) {
    state->entities->prev = entity;
  }
  state->entities = entity;
}

void remove_selected_entites(state_t *state) {
  entity_t *last_removed_entity = NULL;
  for (entity_t *entity = state->entities; entity != NULL;
       entity = entity->next) {
    if (last_removed_entity) {
      entity_recycle(state, last_removed_entity);
      last_removed_entity = NULL;
    }
    if (entity->flags & entity_flag_selected) {
      if (state->entities == entity) {
        state->entities = entity->next;
      }
      if (entity->prev) {
        entity->prev->next = entity->next;
      }
      if (entity->next) {
        entity->next->prev = entity->prev;
      }
      last_removed_entity = entity;
    }
  }

  if (state->entities == NULL) {
    for (entity_t *entity = state->freed_entity; entity != NULL;
         entity = entity->next) {
      entity_free(entity);
    }
    state->freed_entity = NULL;
    arena_free(state->arena);
    state->arena = arena_alloc(ARENA_INITIAL_SIZE);
  }

  state->has_selected_entities = false;
}

void create_entity(state_t *state) {
  const ImGuiIO *io = igGetIO();

  switch (state->selected_toolbox_button) {
  default:
    break;

  case toolbox_button_draw: {
    if (vec2_distance_sqr(&state->drag_start, &io->MousePos) > 100) {
      entity_t *entity = entity_alloc(state, state->points.capacity);
      entity->flags = entity_flag_path;
      entity->color = state->picked_color;
      point_list_copy(&entity->points, &state->points);
      push_entity(state, entity);
    }

    point_list_clear(&state->points);

    break;
  }

  case toolbox_button_rectangle: {
    if (vec2_distance_sqr(&state->drag_start, &io->MousePos) > 625) {
      entity_t *entity = entity_alloc(state, 2);
      entity->flags = entity_flag_rect;
      entity->color = state->picked_color;
      *point_list_push(&entity->points) = state->drag_start;
      *point_list_push(&entity->points) =
          (ImVec2){io->MousePos.x, state->drag_start.y};
      *point_list_push(&entity->points) = io->MousePos;
      *point_list_push(&entity->points) =
          (ImVec2){state->drag_start.x, io->MousePos.y};
      push_entity(state, entity);
    }

    break;
  }
  }
}

entity_t *find_entity_near_mouse(state_t *state, const ImVec2 *mouse_pos) {
  for (entity_t *entity = state->entities; entity != NULL;
       entity = entity->next) {
    if (entity->flags & entity_flag_rect) {
      ImVec2 *top_left = entity->points.items;
      ImVec2 *bottom_right = entity->points.items + 2;

      if (vec2_is_in_area(mouse_pos, top_left, bottom_right)) {
        return entity;
      }
    } else if (entity->flags & entity_flag_path) {
      for (size_t i = 1; i < entity->points.length; ++i) {
        ImVec2 *current_point = &entity->points.items[i];
        ImVec2 *last_point = &entity->points.items[i - 1];

        ImVec2 mouse_pos_proj;
        project_point_to_segment(&mouse_pos_proj, last_point, current_point,
                                 mouse_pos);

        ImVec2 mouse_pos_delta_to_segment = {mouse_pos_proj.x - mouse_pos->x,
                                             mouse_pos_proj.y - mouse_pos->y};

        float magnitude_sqr = vec2_magnitude_sqr(&mouse_pos_delta_to_segment);
        if (magnitude_sqr <= SELECT_THRESHOLD) {
          return entity;
        }
      }
    }
  }

  return NULL;
}

void select_entities_in_area(state_t *state, const ImVec2 *top_left,
                             const ImVec2 *bottom_right) {
  bool has_selected_entities = false;

  for (entity_t *entity = state->entities; entity != NULL;
       entity = entity->next) {
    const size_t no_of_points = entity->points.length;
    bool found = false;
    for (size_t i = 0; i < no_of_points; ++i) {
      if (vec2_is_in_area(entity->points.items + i, top_left, bottom_right)) {
        found = true;
        break;
      }
    }
    if (found) {
      has_selected_entities = true;
      entity->flags |= entity_flag_selected;
    } else {
      entity->flags &= ~entity_flag_selected;
    }
  }

  state->has_selected_entities = has_selected_entities;
}

// ============================================================================
// theme
// ============================================================================

static ImU32 theme_colors[1];
enum theme_color { theme_accent_color = 0 };

// ============================================================================
// ui functions
// ============================================================================

bool selectable_button(const char *label, const ImVec2 size,
                       const bool is_selected) {
  struct ImGuiStyle *style = igGetStyle();
  if (is_selected) {
    igPushStyleColor_U32(ImGuiCol_Button, theme_colors[theme_accent_color]);
  } else {
    igPushStyleColor_U32(ImGuiCol_Button, 0);
  }
  const bool clicked = igButton(label, size);
  igPopStyleColor(1);
  return clicked;
}

// ============================================================================
// main program logic
// ============================================================================

static state_t state;

static void init(void) {
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });
  simgui_setup(&(simgui_desc_t){
      .logger.func = slog_func,
  });

  theme_colors[theme_accent_color] =
      igGetColorU32_Vec4((ImVec4){0.114, 0.435, 1, 1});

  struct ImGuiIO *io = igGetIO();

  struct ImFontConfig *config = ImFontConfig_ImFontConfig();
  config->GlyphMinAdvanceX = 16.0f;
  config->FontDataOwnedByAtlas = false;

  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

  state.fa_font = ImFontAtlas_AddFontFromMemoryTTF(
      io->Fonts, fa4_ttf, FA4_TTF_SIZE, 16.0f, config, icon_ranges);

  state.arena = arena_alloc(ARENA_INITIAL_SIZE);
  state.points = point_list_alloc(100);
  state.last_mouse_pos.x = 0;
  state.last_mouse_pos.y = 0;
  state.is_mouse_down = false;
  state.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
  state.selected_entity = NULL;
  state.has_selected_entities = false;
  state.selected_toolbox_button = toolbox_button_select;
  state.picked_color = *ImColor_ImColor_U32(0xFFFFFFFF);
  state.color_picker_window_size.x = 0;
  state.color_picker_window_size.y = 0;
}

static void toolbox_window(void);
static void color_picker_window(void);

static void frame(void) {
  simgui_new_frame(&(simgui_frame_desc_t){
      .width = sapp_width(),
      .height = sapp_height(),
      .delta_time = sapp_frame_duration(),
      .dpi_scale = sapp_dpi_scale(),
  });

  struct ImGuiViewport *viewport = igGetMainViewport();

  igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0, 0});
  igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
  igSetNextWindowViewport(viewport->ID);
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);

  igBegin("canvas", 0,
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImDrawList *draw_list = igGetWindowDrawList();
  ImGuiIO *io = igGetIO();

  igInvisibleButton("canvas", viewport->WorkSize, ImGuiButtonFlags_None);

  if (igIsMouseDown_Nil(ImGuiMouseButton_Left)) {
    if (!state.is_mouse_down) {
      state.is_mouse_down = true;
      state.is_prev_mouse_down = false;
      state.drag_start = io->MousePos;
    } else {
      state.is_prev_mouse_down = true;
    }
  } else {
    state.is_mouse_down = false;
    if (state.is_prev_mouse_down) {
      create_entity(&state);
    }
    state.is_prev_mouse_down = false;
  }

  ImVec2 move_entity_by = {0, 0};
  entity_t *selected_entity = NULL;
  bool should_clear_prev_selections = false;
  ImU32 current_picked_color = igGetColorU32_Vec4(state.picked_color.Value);

  switch (state.selected_toolbox_button) {
  default:
    break;

  case toolbox_button_select: {
    if (state.is_mouse_down &&
        !vec2_is_in_area(&io->MousePos,
                         &state.color_picker_window.position_top_left,
                         &state.color_picker_window.position_bottom_right)) {
      if (!state.is_prev_mouse_down) {
        selected_entity = find_entity_near_mouse(&state, &io->MousePos);
        if (selected_entity) {
          if ((selected_entity->flags & entity_flag_selected) == 0) {
            should_clear_prev_selections = true;
            selected_entity->flags |= entity_flag_selected;
          }
          state.has_selected_entities = true;
        } else {
          should_clear_prev_selections = true;
          state.has_selected_entities = false;
        }
      } else if (state.has_selected_entities && !state.is_area_selecting) {
        selected_entity = find_entity_near_mouse(&state, &io->MousePos);
        if ((selected_entity &&
             selected_entity->flags & entity_flag_selected) ||
            state.is_moving_entities) {
          state.is_moving_entities = true;
          move_entity_by.x = io->MousePos.x - state.last_mouse_pos.x;
          move_entity_by.y = io->MousePos.y - state.last_mouse_pos.y;
        }
      } else if (!state.is_moving_entities) {
        state.is_area_selecting = true;
        select_entities_in_area(&state, &state.drag_start, &io->MousePos);
      }
    } else {
      if (state.is_area_selecting) {
        state.is_area_selecting = false;
      }
      if (state.is_moving_entities) {
        state.is_moving_entities = false;
      }
    }
    break;
  }
  }

  if (igIsKeyPressed_Bool(ImGuiKey_Backspace, false)) {
    remove_selected_entites(&state);
  }

  igSetNextWindowPos((ImVec2){io->DisplaySize.x * 0.5, 16}, ImGuiCond_Always,
                     (ImVec2){0.5, 0});

  toolbox_window();

  if (state.color_picker_window.dimension.x > 0) {
    ImVec2 *top_left = &state.color_picker_window.position_top_left;
    ImVec2 *bottom_right = &state.color_picker_window.position_bottom_right;

    top_left->x =
        io->DisplaySize.x - state.color_picker_window.dimension.x - 16;
    top_left->y = 16;
    bottom_right->x = top_left->x + state.color_picker_window.dimension.x;
    bottom_right->y = top_left->y + state.color_picker_window.dimension.y;

    igSetNextWindowPos(*top_left, ImGuiCond_Always, (ImVec2){0, 0});
  } else {
    igSetNextWindowPos((ImVec2){0, 0}, ImGuiCond_Always, (ImVec2){0, 0});
  }

  color_picker_window();

  igSetNextWindowPos((ImVec2){0, io->DisplaySize.y}, ImGuiCond_Always,
                     (ImVec2){0, 1});
  igSetNextWindowCollapsed(true, ImGuiCond_Once);
  igShowMetricsWindow(NULL);

  //======= draw entities to canvas =========

  for (entity_t *entity = state.entities; entity != NULL;
       entity = entity->next) {
    bool is_selected;
    if (should_clear_prev_selections && entity != selected_entity) {
      is_selected = false;
      entity->flags &= ~entity_flag_selected;
    } else {
      is_selected = entity->flags & entity_flag_selected;
    }

    if (entity->flags & entity_flag_path) {
      ImU32 fill_color;
      if (is_selected && state.is_color_picker_changing) {
        fill_color = current_picked_color;
        entity->color = state.picked_color;
      } else {
        fill_color = igGetColorU32_Vec4(entity->color.Value);
      }

      const size_t no_of_points = entity->points.length;
      for (size_t i = 1; i < no_of_points; ++i) {
        ImVec2 *current_point = &entity->points.items[i];
        ImVec2 *last_point = &entity->points.items[i - 1];

        if (is_selected) {
          if (i == 1) {
            vec2_move(last_point, &move_entity_by);
          }
          vec2_move(current_point, &move_entity_by);
        }

        ImDrawList_AddLine(draw_list, *last_point, *current_point, fill_color,
                           2);

        if (is_selected) {
          if (i == 1) {
            ImDrawList_AddCircleFilled(draw_list, *last_point, 4, 0xFFFFFFFF,
                                       10);
          } else if (i == no_of_points - 1) {
            ImDrawList_AddCircleFilled(draw_list, *current_point, 4, 0xFFFFFFFF,
                                       10);
          }
        }
      }
    } else if (entity->flags & entity_flag_rect) {
      if (is_selected) {
        vec2_move(entity->points.items, &move_entity_by);
        vec2_move(entity->points.items + 2, &move_entity_by);
      }

      ImU32 fill_color;
      if (is_selected && state.is_color_picker_changing) {
        fill_color = current_picked_color;
        entity->color = state.picked_color;
      } else {
        fill_color = igGetColorU32_Vec4(entity->color.Value);
      }

      ImDrawList_AddRectFilled(draw_list, entity->points.items[0],
                               entity->points.items[2], fill_color, 0.0,
                               ImDrawFlags_None);

      if (is_selected) {
        ImDrawList_AddRect(draw_list, entity->points.items[0],
                           entity->points.items[2],
                           igGetColorU32_Vec4((ImVec4){0.537, 0.706, 1, 1}),
                           0.0, ImDrawFlags_None, 2.0);
      }
    }
  }

  switch (state.selected_toolbox_button) {
  default:
    break;

  case toolbox_button_select: {
    if (state.is_mouse_down && state.is_prev_mouse_down &&
        !state.is_moving_entities) {
      ImDrawList_AddRect(draw_list, state.drag_start, io->MousePos, 0xFFFFFFFF,
                         0.0f, ImDrawFlags_None, 1);
    }
    break;
  }

  case toolbox_button_rectangle:
    if (state.is_mouse_down) {
      ImDrawList_AddRectFilled(draw_list, state.drag_start, io->MousePos,
                               igGetColorU32_Vec4(state.picked_color.Value),
                               0.0f, ImDrawFlags_None);
    }
    break;

  case toolbox_button_draw: {
    if (state.is_mouse_down && (state.last_mouse_pos.x != io->MousePos.x ||
                                state.last_mouse_pos.y != io->MousePos.y)) {
      *point_list_push(&state.points) = io->MousePos;
    }

    for (size_t i = 1; i < state.points.length; ++i) {
      ImDrawList_AddLine(draw_list, state.points.items[i - 1],
                         state.points.items[i], current_picked_color, 2);
    }

    break;
  }
  }

  state.last_mouse_pos = io->MousePos;

  igEnd();
  igPopStyleVar(2);

  sg_begin_pass(&(sg_pass){
      .action = state.pass_action,
      .swapchain = sglue_swapchain(),
  });
  simgui_render();
  sg_end_pass();
  sg_commit();
}

static void toolbox_window(void) {
  static ImVec2 button_size = {24, 24};

  igPushFont(state.fa_font);

  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.0);
  igPushStyleVar_Vec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){1, 0.9});

  igBegin("t", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

  if (selectable_button(ICON_FA_MOUSE_POINTER, button_size,
                        state.selected_toolbox_button ==
                            toolbox_button_select)) {
    state.selected_toolbox_button = toolbox_button_select;
  };

  igSameLine(0, 4);

  if (selectable_button(ICON_FA_PENCIL, button_size,
                        state.selected_toolbox_button == toolbox_button_draw)) {
    state.selected_toolbox_button = toolbox_button_draw;
  };

  igSameLine(0, 4);

  igPushStyleVar_Vec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){0.8, 1});

  if (selectable_button(ICON_FA_SQUARE_O, button_size,
                        state.selected_toolbox_button ==
                            toolbox_button_rectangle)) {
    state.selected_toolbox_button = toolbox_button_rectangle;
  };

  igPopStyleVar(3);
  igPopFont();

  igEnd();
}

static void color_picker_window(void) {
  igBegin("Colors", 0, ImGuiWindowFlags_AlwaysAutoResize);

  igGetWindowSize(&state.color_picker_window.dimension);

  state.is_color_picker_changing = igColorPicker4(
      "Color", (float *)&state.picked_color,
      ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs |
          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview,
      NULL);

  igEnd();
}

static void cleanup(void) {
  simgui_shutdown();
  sg_shutdown();
}

static void event(const sapp_event *event) { simgui_handle_event(event); }

sapp_desc sokol_main(int argc, char *argv[]) {
  return (sapp_desc){
      .init_cb = init,
      .frame_cb = frame,
      .cleanup_cb = cleanup,
      .event_cb = event,
      .logger.func = slog_func,
      .width = 640,
      .height = 480,
      .window_title = "ImDraw",
      .icon.sokol_default = true,
      .high_dpi = true,
  };
}
