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

#define ARENA_INITIAL_SIZE 1000

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

typedef enum { entity_flag_rect = 1, entity_flag_path = 1 << 1 } entity_flag_t;

typedef struct entity {
  entity_flag_t flags;

  point_list_t points;

  struct entity *next;
  struct entity *prev;
} entity_t;

// ===========================
// struct: game state
// ===========================

typedef enum {
  toolbox_button_select,
  toolbox_button_draw,
  toolbox_button_rectangle,
} toolbox_button_kind_t;

typedef struct {
  ImFont *fa_font;

  arena_t *arena;
  entity_t *entites;
  entity_t *freed_entity;

  sg_pass_action pass_action;

  bool is_dragging;
  bool is_prev_dragging;
  point_list_t points;
  ImVec2 drag_start;
  ImVec2 last_mouse_pos;
  entity_t *selected_entity;

  toolbox_button_kind_t selected_toolbox_button;
} state_t;

entity_t *entity_alloc(state_t *state, size_t point_count) {
  entity_t *entity = state->freed_entity;
  if (entity) {
    state->freed_entity = state->freed_entity->next;
  } else {
    printf("pushing to arena\n");
    entity = arena_push(state->arena, sizeof(entity_t));
    entity->points = point_list_alloc(point_count);
  }

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
  entity->next = state->entites;
  if (state->entites) {
    state->entites->prev = entity;
  }
  state->entites = entity;
}

void remove_entity(state_t *state, entity_t *entity) {
  if (state->entites == entity) {
    state->entites = entity->next;
  }
  if (entity->prev) {
    entity->prev->next = entity->next;
  }
  if (entity->next) {
    entity->next->prev = entity->prev;
  }

  if (state->entites == NULL) {
    for (entity_t *entity = state->freed_entity; entity != NULL;
         entity = entity->next) {
      entity_free(entity);
    }
    arena_free(state->arena);
    state->arena = arena_alloc(ARENA_INITIAL_SIZE);
  } else {
    entity_recycle(state, entity);
  }
}

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

bool is_mouse_click(const ImVec2 *mouse_down_pos, const ImVec2 *mouse_up_pos) {
  if (igIsMouseReleased_Nil(ImGuiMouseButton_Left)) {
    return vec2_distance_sqr(mouse_up_pos, mouse_down_pos) <= CLICK_THRESHOLD;
  }
  return false;
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

  struct ImGuiIO *io = igGetIO();

  struct ImFontConfig *config = ImFontConfig_ImFontConfig();
  config->MergeMode = true;
  config->GlyphMinAdvanceX = 13.0f;
  config->FontDataOwnedByAtlas = false;

  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA};

  state.fa_font = ImFontAtlas_AddFontFromMemoryTTF(
      io->Fonts, FA4_TTF, FA4_TTF_SIZE, 16.0f, config, icon_ranges);

  state.arena = arena_alloc(ARENA_INITIAL_SIZE);
  state.points = point_list_alloc(100);
  state.last_mouse_pos.x = 0;
  state.last_mouse_pos.y = 0;
  state.is_dragging = false;
  state.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
  state.selected_entity = NULL;

  state.selected_toolbox_button = toolbox_button_select;
}

static void toolbox_window(void);

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

  igBegin("canvas", 0,
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImDrawList *draw_list = igGetWindowDrawList();
  struct ImGuiIO *io = igGetIO();

  igInvisibleButton("canvas", viewport->WorkSize, ImGuiButtonFlags_None);

  if (igIsMouseDown_Nil(ImGuiMouseButton_Left)) {
    if (!state.is_dragging) {
      state.is_dragging = true;
      state.is_prev_dragging = false;
      state.drag_start = io->MousePos;
    } else {
      state.is_prev_dragging = true;
      state.selected_entity = NULL;
    }
  } else {
    state.is_dragging = false;
  }

  switch (state.selected_toolbox_button) {
  default:
    break;

  case toolbox_button_select: {
    if (is_mouse_click(&state.drag_start, &io->MousePos)) {
      bool has_selected_entity = false;

      for (entity_t *entity = state.entites; entity != NULL;
           entity = entity->next) {
        for (size_t i = 1; i < entity->points.length; ++i) {
          ImVec2 *current_point = &entity->points.items[i];
          ImVec2 *last_point = &entity->points.items[i - 1];

          ImVec2 mouse_pos_proj;
          project_point_to_segment(&mouse_pos_proj, last_point, current_point,
                                   &io->MousePos);

          ImVec2 mouse_pos_delta_to_segment = {
              mouse_pos_proj.x - io->MousePos.x,
              mouse_pos_proj.y - io->MousePos.y};

          float magnitude_sqr = vec2_magnitude_sqr(&mouse_pos_delta_to_segment);
          if (magnitude_sqr <= SELECT_THRESHOLD) {
            has_selected_entity = true;
            state.selected_entity = entity;
            break;
          }
        }
      }

      if (!has_selected_entity) {
        state.selected_entity = NULL;
      }
    }
    break;
  }

  case toolbox_button_draw: {
    if (state.is_dragging && (state.last_mouse_pos.x != io->MousePos.x ||
                              state.last_mouse_pos.y != io->MousePos.y)) {
      *point_list_push(&state.points) = io->MousePos;
    } else if (!state.is_dragging && state.is_prev_dragging &&
               state.points.length > 0) {
      entity_t *entity = entity_alloc(&state, state.points.capacity);
      entity->flags = entity_flag_path;
      point_list_copy(&entity->points, &state.points);
      push_entity(&state, entity);
      point_list_clear(&state.points);
      state.is_prev_dragging = false;
    }

    for (size_t i = 1; i < state.points.length; ++i) {
      ImDrawList_AddLine(draw_list, state.points.items[i - 1],
                         state.points.items[i], 0xFFFFFFFF, 2);
    }

    break;
  }
  }

  if (igIsKeyPressed_Bool(ImGuiKey_Backspace, false)) {
    if (state.entites) {
      remove_entity(&state, state.selected_entity);
      state.selected_entity = NULL;
    }
  }

  //======= draw entities to canvas =========

  for (entity_t *entity = state.entites; entity != NULL;
       entity = entity->next) {
    if (entity->flags & entity_flag_path) {
      for (size_t i = 1; i < entity->points.length; ++i) {
        ImVec2 *current_point = &entity->points.items[i];
        ImVec2 *last_point = &entity->points.items[i - 1];
        ImU32 color = entity == state.selected_entity ? 0xFF0000FF : 0xFFFFFFFF;
        ImDrawList_AddLine(draw_list, *last_point, *current_point, color, 2);
      }
    }
  }

  igShowMetricsWindow(NULL);

  state.last_mouse_pos = io->MousePos;

  igSetNextWindowPos(viewport->WorkPos, ImGuiCond_None, (ImVec2){0, 0});
  toolbox_window();

  igEnd();
  igPopStyleVar(1);

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

  igBegin(ICON_FA_COG, 0, ImGuiWindowFlags_NoResize);

  igPushStyleVar_Vec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){1, 1});
  igPushStyleColor_U32(ImGuiCol_Button, 0);

  if (igButton(ICON_FA_MOUSE_POINTER, button_size)) {
    state.selected_toolbox_button = toolbox_button_select;
  };
  if (igButton(ICON_FA_PENCIL, button_size)) {
    state.selected_toolbox_button = toolbox_button_draw;
  };

  igPushStyleVar_Vec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){0.8, 1});
  igButton(ICON_FA_SQUARE_O, button_size);
  igPopStyleVar(1);

  igPopStyleVar(1);
  igPopStyleColor(1);

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
  };
}
