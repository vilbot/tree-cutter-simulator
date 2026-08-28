#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#define WIDTH 720
#define HEIGHT 720
#define MAX_TREES 20
#define HITBOX_SIZE 0.90f

// TODO: The cursor should obviously be an axe 

enum difficulty {
	EASY,
	MEDIUM,
	HARD,
	WRAP
};

typedef struct {
	SDL_Texture* tree_texture;
	SDL_Texture* ground_texture;
	int rows;
	int cols;
	SDL_FRect location[MAX_TREES][MAX_TREES];
	SDL_FRect hitboxes[MAX_TREES][MAX_TREES];
	bool is_occupied[MAX_TREES][MAX_TREES];
} grid;

typedef struct {
	SDL_FRect base_size;
	SDL_FRect hover_size;
	SDL_FRect render_size;
} grow_rect;

typedef struct {
	SDL_Texture* texture;
	grow_rect rect;
} grow_sprite;

typedef struct {
	SDL_Window* window;
	SDL_Renderer* renderer;
	std::unordered_map<std::string, SDL_Texture*> textures;
	std::unordered_map<std::string, grow_sprite> menu;
	SDL_Surface* mouse_sprite[2];
	SDL_Cursor* cursor[2];
	grid grid;
	bool main_menu;
	bool difficulty_menu;
	bool game_started;
	bool game_paused;
	difficulty current_difficulty;
} game_state;

bool load_texture(game_state* state, const char* texture_name)
{
	char* texture_png_path = NULL;
	SDL_asprintf(&texture_png_path, "%s../assets/%s.png", SDL_GetBasePath(), texture_name);
	SDL_Surface* surface = SDL_LoadPNG(texture_png_path);
	if(!surface) {
		SDL_Log("Couldn't load png: %s", SDL_GetError());
		return false;
	}
	SDL_free(texture_png_path);

	SDL_Texture* temp = SDL_CreateTextureFromSurface(state->renderer, surface);
	state->textures[texture_name] = temp;
	if(!state->textures[texture_name]) {
		SDL_Log("Couldn't create texture from surface: %s", SDL_GetError());
		return false;
	}

	SDL_DestroySurface(surface);
	SDL_SetTextureScaleMode(state->textures[texture_name], SDL_SCALEMODE_NEAREST);
	return true;
}

bool is_inside(float xclick, float yclick, SDL_FRect area)
{
	return
	xclick >= area.x && xclick <= area.x + area.w &&
	yclick >= area.y && yclick <= area.y + area.h;
}

bool is_inside(float xclick, float yclick, float xstart, float ystart, float xend, float yend)
{
	return
	xclick >= xstart && xclick <= xend &&
	yclick >= ystart && yclick <= yend;
}

// NOTE: How to lerp?
void resize_rect(SDL_FRect* base, SDL_FRect target)
{
	base->x = target.x;
	base->y = target.y;
	base->w = target.w;
	base->h = target.h;
}

void resize_rect(SDL_FRect* base, float scale)
{
	SDL_FRect temp;
	temp.x = base->x;
	temp.y = base->y;
	temp.w = base->w;
	temp.h = base->h;

	base->w = temp.w * scale;
	base->h = temp.h * scale;
	base->x = temp.x - (base->w - temp.w) / 2;
	base->y = temp.y - (base->h - temp.h) / 2;
}

void set_difficulty(game_state* state, difficulty difficulty)
{
	state->current_difficulty = difficulty;
	int diff_multiplier = 1; // Needs to be divisible by 720 or whatever resolution the grid has

	switch(state->current_difficulty) {
		case EASY:
			state->grid.rows = 5;
			state->grid.cols = 5;
			diff_multiplier = 4;
			break;
		case MEDIUM:
			state->grid.rows = 10;
			state->grid.cols = 10;
			diff_multiplier = 2;
			break;
		case HARD:
			state->grid.rows = 20;
			state->grid.cols = 20;
			diff_multiplier = 1;
			break;
		case WRAP: break;
	}

	float tree_w, tree_h;
	SDL_GetTextureSize(state->textures["tree"], &tree_w, &tree_h);

	for(int y = 0; y < state->grid.rows; ++y) {
		for(int x = 0; x < state->grid.cols; ++x) {
			SDL_FRect temp;
			temp.x = ((float)x) * tree_w * diff_multiplier;
			temp.y = ((float)y) * tree_h * diff_multiplier;
			temp.w = tree_w * diff_multiplier;
			temp.h = tree_h * diff_multiplier;
			state->grid.location[y][x] = temp;
			resize_rect(&temp, HITBOX_SIZE);
			state->grid.hitboxes[y][x] = temp;
			state->grid.is_occupied[y][x] = true;
		}
	}
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	game_state* state = new game_state;
	if(!state) {
		return SDL_APP_FAILURE;
	}
	*appstate = state;

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if(!SDL_CreateWindowAndRenderer("Tree Cutter Simulator", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE, &state->window, &state->renderer)) {
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_SetRenderLogicalPresentation(state->renderer, WIDTH, HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	if(!load_texture(state, "continue")) return SDL_APP_FAILURE;
	if(!load_texture(state, "difficulty")) return SDL_APP_FAILURE;
	if(!load_texture(state, "easy")) return SDL_APP_FAILURE;
	if(!load_texture(state, "exit")) return SDL_APP_FAILURE;
	if(!load_texture(state, "ground")) return SDL_APP_FAILURE;
	if(!load_texture(state, "hard")) return SDL_APP_FAILURE;
	if(!load_texture(state, "medium")) return SDL_APP_FAILURE;
	if(!load_texture(state, "quit")) return SDL_APP_FAILURE;
	if(!load_texture(state, "start-game")) return SDL_APP_FAILURE;
	if(!load_texture(state, "tree")) return SDL_APP_FAILURE;

	state->grid.tree_texture = state->textures["tree"];
	state->grid.ground_texture = state->textures["ground"];
	state->menu["start-game"].texture = state->textures["start-game"];
	state->menu["difficulty"].texture = state->textures["difficulty"];
	state->menu["quit"].texture = state->textures["quit"];
	state->menu["easy"].texture = state->textures["easy"];
	state->menu["medium"].texture = state->textures["medium"];
	state->menu["hard"].texture = state->textures["hard"];
	state->menu["continue"].texture = state->textures["continue"];
	state->menu["exit"].texture = state->textures["exit"];


	// TODO: Refactor this so that adding submenus becomes easier
	float enlargement = 1.15f;
	SDL_FRect start_game, difficulty, quit;
	SDL_GetTextureSize(state->textures["start-game"], &start_game.w, &start_game.h);
	SDL_GetTextureSize(state->textures["difficulty"], &difficulty.w, &difficulty.h);
	SDL_GetTextureSize(state->textures["quit"], &quit.w, &quit.h);

	start_game.x = difficulty.x = quit.x = 100;

	start_game.y = 50;
	difficulty.y = 100 + start_game.y + start_game.h;
	quit.y = 100 + difficulty.y + difficulty.h;

	state->menu["start-game"].rect.base_size = state->menu["start-game"].rect.hover_size = state->menu["start-game"].rect.render_size = start_game;
	state->menu["difficulty"].rect.base_size = state->menu["difficulty"].rect.hover_size = state->menu["difficulty"].rect.render_size = difficulty;
	state->menu["quit"].rect.base_size = state->menu["quit"].rect.hover_size = state->menu["quit"].rect.render_size = quit;

	resize_rect(&state->menu["start-game"].rect.hover_size, enlargement);
	resize_rect(&state->menu["difficulty"].rect.hover_size, enlargement);
	resize_rect(&state->menu["quit"].rect.hover_size, enlargement);

	char* texture_png_path = NULL;
	SDL_asprintf(&texture_png_path, "%s../assets/axe1.png", SDL_GetBasePath());
	state->mouse_sprite[0] = SDL_LoadPNG(texture_png_path);
	SDL_free(texture_png_path);

	SDL_asprintf(&texture_png_path, "%s../assets/axe2.png", SDL_GetBasePath());
	state->mouse_sprite[1] = SDL_LoadPNG(texture_png_path);
	SDL_free(texture_png_path);

	state->cursor[0] = SDL_CreateColorCursor(state->mouse_sprite[0], 0, 0);
	state->cursor[1] = SDL_CreateColorCursor(state->mouse_sprite[1], 0, 0);
	SDL_SetCursor(state->cursor[0]);
	if(!state->cursor[0] || !state->cursor[1]) {
		SDL_Log("Couldn't create cursor: %s", SDL_GetError());
		return SDL_APP_FAILURE;

	}


	state->main_menu = true;
	state->difficulty_menu = false;
	state->game_started = false;

	set_difficulty(state, MEDIUM);

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	game_state* state = (game_state*)appstate;

	switch(event->type) {
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			SDL_SetCursor(state->cursor[1]);
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			switch(event->button.button) {
				case SDL_BUTTON_LEFT:
					SDL_SetCursor(state->cursor[0]);
					float xlclick, ylclick;
					SDL_GetMouseState(&xlclick, &ylclick);
					// TODO: Add the submenu system
					if(state->main_menu) {
						if(is_inside(xlclick, ylclick, state->menu["start-game"].rect.render_size)) {
							state->main_menu = false;
							state->game_started = true;
							break;
						}
						if(is_inside(xlclick, ylclick, state->menu["difficulty"].rect.render_size)) {
							state->main_menu = false;
							state->difficulty_menu = true;
						}
						if(is_inside(xlclick, ylclick, state->menu["quit"].rect.render_size)) {
							return SDL_APP_FAILURE;
						}
					}
					if(state->game_started) {
						for(int y = 0; y < state->grid.rows; ++y) {
							for(int x = 0; x < state->grid.cols; ++x) {
								if(is_inside(xlclick, ylclick, state->grid.hitboxes[y][x])) {
									state->grid.is_occupied[y][x] = false;
								}
							}
						}
					}
					break;
				case SDL_BUTTON_RIGHT:
					break;
			}
			break;
		case SDL_EVENT_MOUSE_MOTION:
			float xmotion, ymotion;
			SDL_GetMouseState(&xmotion, &ymotion);
			if(state->main_menu) {
				for(auto &[key, menu] : state->menu) {
					if(is_inside(xmotion, ymotion, menu.rect.render_size)) {
						resize_rect(&menu.rect.render_size, menu.rect.hover_size);
					}
					else {
						resize_rect(&menu.rect.render_size, menu.rect.base_size);
					}
				}
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			switch(event->key.key) {
				case SDLK_ESCAPE:
					if(state->difficulty_menu) {
						state->difficulty_menu = false;
						state->main_menu = true;
					}
					if(!state->main_menu) {
						state->main_menu = true;
					}
					break;
			}
			break;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	game_state* state = (game_state*)appstate;

	SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
	SDL_RenderClear(state->renderer);

	for(int y = 0; y < state->grid.rows; ++y) {
		for(int x = 0; x < state->grid.cols; ++x) {
			SDL_RenderTexture(state->renderer, state->grid.ground_texture, NULL, &state->grid.location[y][x]);
			if(state->grid.is_occupied[y][x]) {
				SDL_RenderTexture(state->renderer, state->grid.tree_texture, NULL, &state->grid.location[y][x]);
			}
			// SDL_RenderRect(state->renderer, &state->trees.hitboxes[y][x]);
		}
	}

	if(state->main_menu) {
		SDL_RenderTexture(state->renderer, state->menu["start-game"].texture, NULL, &state->menu["start-game"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["difficulty"].texture, NULL, &state->menu["difficulty"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["quit"].texture, NULL, &state->menu["quit"].rect.render_size);
	}
	if(state->difficulty_menu) {
		SDL_RenderTexture(state->renderer, state->menu["easy"].texture, NULL, &state->menu["easy"].rect.render_size);
	}

	SDL_RenderPresent(state->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	game_state* state = (game_state*)appstate;
	for(const auto &[key, texture] : state->textures) {
		SDL_DestroyTexture(texture);
	}
	SDL_DestroyRenderer(state->renderer);
	SDL_DestroyWindow(state->window);
	delete state;
}
