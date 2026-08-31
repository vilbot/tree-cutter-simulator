#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>

#define WIDTH 720
#define HEIGHT 720
#define MAX_TREES 20
#define HITBOX_SIZE 0.90f

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
	float lerp_value;
	SDL_FRect base_size;
	SDL_FRect hover_size;
	SDL_FRect render_size;
} grow_sprite;

typedef struct {
	SDL_Window* window;
	SDL_Renderer* renderer;
	std::unordered_map<std::string, SDL_Texture*> textures;
	std::unordered_map<std::string, grow_sprite> menu;
	SDL_Surface* mouse_sprite[2];
	SDL_Cursor* cursor[2];
	grid grid;
	difficulty current_difficulty;
	bool main_menu;
	bool difficulty_menu;
	bool game_started;
	bool game_paused;
	Uint64 start_time;
	Uint64 previous_time;
} game_state;

bool operator==(const SDL_FRect &a, const SDL_FRect &b) {
	return a.h == b.h && a.w == b.w;
}

bool operator!=(const SDL_FRect &a, const SDL_FRect &b) {
	return a.h != b.h || a.w != b.w;
}

SDL_AppResult load_texture(game_state* state, const char* texture_name, SDL_Texture*& texture_reference)
{
	char* texture_png_path = NULL;
	SDL_asprintf(&texture_png_path, "%s../assets/%s.png", SDL_GetBasePath(), texture_name);
	SDL_Surface* surface = SDL_LoadPNG(texture_png_path);
	if(!surface) {
		SDL_Log("Couldn't load png: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	SDL_free(texture_png_path);

	SDL_Texture* temp = SDL_CreateTextureFromSurface(state->renderer, surface);
	state->textures[texture_name] = temp;
	if(!state->textures[texture_name]) {
		SDL_Log("Couldn't create texture from surface: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_DestroySurface(surface);
	SDL_SetTextureScaleMode(state->textures[texture_name], SDL_SCALEMODE_NEAREST);

	texture_reference = state->textures[texture_name];

	return SDL_APP_SUCCESS;
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
	int diff_multiplier = 1; // Needs to divide 720 or whatever resolution the grid has

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

void create_menu(std::initializer_list<grow_sprite*> argument_list) 
{
	float enlargement = 1.15f;

	std::vector<grow_sprite*> items(argument_list);

	for(int i = 0; i < items.size(); ++i) {
		SDL_GetTextureSize(items[i]->texture, &items[i]->rect.base_size.w, &items[i]->rect.base_size.h);
		items[i]->rect.base_size.x = 100;

		if(i == 0) 
			items[i]->rect.base_size.y = 50;
		else 
			items[i]->rect.base_size.y = 100 + items[i - 1]->rect.base_size.y + items[i - 1]->rect.base_size.h;

		items[i]->rect.hover_size = items[i]->rect.render_size = items[i]->rect.base_size;
		resize_rect(&items[i]->rect.hover_size, enlargement);
		items[i]->lerp_value = 1.0f;
	}
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	game_state* state = new game_state;
	if(!state) {
		return SDL_APP_FAILURE;
	}
	*appstate = state;

	state->start_time = SDL_GetTicks();
	state->previous_time = state->start_time;

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if(!SDL_CreateWindowAndRenderer("Tree Cutter Simulator", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE, &state->window, &state->renderer)) {
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_SetRenderLogicalPresentation(state->renderer, WIDTH, HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	if(!load_texture(state, "continue", state->menu["continue"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "difficulty", state->menu["difficulty"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "easy", state->menu["easy"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "exit", state->menu["exit"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "hard", state->menu["hard"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "medium", state->menu["medium"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "quit", state->menu["quit"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "start-game", state->menu["start-game"].texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "tree", state->grid.tree_texture)) return SDL_APP_FAILURE;
	if(!load_texture(state, "ground", state->grid.ground_texture)) return SDL_APP_FAILURE;

	create_menu({&state->menu["start-game"], &state->menu["difficulty"], &state->menu["exit"]});
	create_menu({&state->menu["easy"], &state->menu["medium"], &state->menu["hard"]});
	create_menu({&state->menu["continue"], &state->menu["quit"]});

	// TODO: Animate the mouse rather than switch between two sprites
	char* png_path = NULL;
	SDL_asprintf(&png_path, "%s../assets/axe1.png", SDL_GetBasePath());
	state->mouse_sprite[0] = SDL_LoadPNG(png_path);
	SDL_free(png_path);

	SDL_asprintf(&png_path, "%s../assets/axe2.png", SDL_GetBasePath());
	state->mouse_sprite[1] = SDL_LoadPNG(png_path);
	SDL_free(png_path);

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
	state->game_paused = false;

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
						if(is_inside(xlclick, ylclick, state->menu["exit"].rect.render_size)) {
							return SDL_APP_SUCCESS;
						}
					}
					if(state->difficulty_menu) {
						if(is_inside(xlclick, ylclick, state->menu["easy"].rect.render_size)) {
							set_difficulty(state, EASY); 
						}
						if(is_inside(xlclick, ylclick, state->menu["medium"].rect.render_size)) {
							set_difficulty(state, MEDIUM); 
						}
						if(is_inside(xlclick, ylclick, state->menu["hard"].rect.render_size)) {
							set_difficulty(state, HARD); 
						}
					}
					if(state->game_started && !state->game_paused) {
						for(int y = 0; y < state->grid.rows; ++y) {
							for(int x = 0; x < state->grid.cols; ++x) {
								if(is_inside(xlclick, ylclick, state->grid.hitboxes[y][x])) {
									state->grid.is_occupied[y][x] = false;
								}
							}
						}
					}
					if(state->game_paused) {
						if(is_inside(xlclick, ylclick, state->menu["continue"].rect.hover_size)) {
							state->game_paused = false;
						}
						if(is_inside(xlclick, ylclick, state->menu["quit"].rect.hover_size)) {
							state->game_started = false;
							state->game_paused = false;
							state->main_menu = true;
							state->difficulty_menu = false;
							for(int y = 0; y < state->grid.rows; ++y) {
								for(int x = 0; x < state->grid.cols; ++x) {
									state->grid.is_occupied[y][x] = true;
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
			// for(auto &[key, menu] : state->menu) {
			// 	if(is_inside(xmotion, ymotion, menu.rect.render_size)) {
			// 		resize_rect(&menu.rect.render_size, menu.rect.hover_size);
			// 	}
			// 	else {
			// 		resize_rect(&menu.rect.render_size, menu.rect.base_size);
			// 	}
			// }
			break;
		case SDL_EVENT_KEY_DOWN:
			switch(event->key.key) {
				case SDLK_ESCAPE:
					if(state->game_started) {
						state->game_paused = !state->game_paused;
					}
					if(state->difficulty_menu) {
						state->difficulty_menu = false;
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

	float xpos, ypos;
	SDL_GetMouseState(&xpos, &ypos);
	float lerp_max = 1.15, lerp_min = 1.0;
	float lerp_value = 0.01;

	for(auto &[key, item] : state->menu) {
		if(is_inside(xpos, ypos, item.rect.base_size)) {
			item.lerp_value= std::lerp(item.lerp_value, lerp_max, lerp_value);
		}
		else {
			item.lerp_value = std::lerp(item.lerp_value, lerp_min, lerp_value);
		}

		SDL_FRect temp;
		temp.x = item.rect.base_size.x;
		temp.y = item.rect.base_size.y;
		temp.w = item.rect.base_size.w;
		temp.h = item.rect.base_size.h;

		item.rect.render_size.w = temp.w * (item.lerp_value);
		item.rect.render_size.h = temp.h * (item.lerp_value);
		item.rect.render_size.x = temp.x - (item.rect.render_size.w - temp.w) / 2;
		item.rect.render_size.y = temp.y - (item.rect.render_size.h - temp.h) / 2;

		if(item.rect.render_size.h <= item.rect.base_size.h || item.rect.render_size.w <= item.rect.base_size.w) {
			resize_rect(&item.rect.render_size, item.rect.base_size);
		}

		if(item.rect.render_size.h >= item.rect.hover_size.h || item.rect.render_size.w >= item.rect.hover_size.w) {
			resize_rect(&item.rect.render_size, item.rect.hover_size);
		}
	}

	for(int y = 0; y < state->grid.rows; ++y) {
		for(int x = 0; x < state->grid.cols; ++x) {
			SDL_RenderTexture(state->renderer, state->grid.ground_texture, NULL, &state->grid.location[y][x]);
			if(state->grid.is_occupied[y][x]) {
				SDL_RenderTexture(state->renderer, state->grid.tree_texture, NULL, &state->grid.location[y][x]);
			}
			// SDL_RenderRect(state->renderer, &state->grid.hitboxes[y][x]);
		}
	}

	if(state->main_menu) {
		SDL_RenderTexture(state->renderer, state->menu["start-game"].texture, NULL, &state->menu["start-game"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["difficulty"].texture, NULL, &state->menu["difficulty"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["exit"].texture, NULL, &state->menu["exit"].rect.render_size);
	}
	if(state->difficulty_menu) {
		SDL_RenderTexture(state->renderer, state->menu["easy"].texture, NULL, &state->menu["easy"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["medium"].texture, NULL, &state->menu["medium"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["hard"].texture, NULL, &state->menu["hard"].rect.render_size);
	}
	if(state->game_paused) {
		SDL_RenderTexture(state->renderer, state->menu["continue"].texture, NULL, &state->menu["continue"].rect.render_size);
		SDL_RenderTexture(state->renderer, state->menu["quit"].texture, NULL, &state->menu["quit"].rect.render_size);
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
