#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>

#define WIDTH 1920
#define HEIGHT 1080
#define MAX_TREES 200
#define TILESX 400
#define TILESY 300
#define TILE_WORLD_SIZE 32
#define HITBOX_SIZE 0.90f


enum difficulty {
	EASY,
	MEDIUM,
	HARD,
	WRAP
};

difficulty operator+(difficulty current, float amount) {
	return static_cast<difficulty>(static_cast<int>(current) + static_cast<int>(amount));
}

typedef struct {
	SDL_Texture* texture;
	SDL_FRect base_size;
	SDL_FRect hover_size;
	SDL_FRect render_size;
	float lerp_value;
} grow_sprite;

typedef struct {
	SDL_Texture* texture;
	SDL_FRect size;
} sprite;

typedef struct {
	SDL_Texture* tree_texture;
	SDL_Texture* ground_texture;
	int rows;
	int cols;
	sprite location[MAX_TREES][MAX_TREES];
	SDL_FRect hitboxes[MAX_TREES][MAX_TREES];
	bool is_occupied[MAX_TREES][MAX_TREES];
} grid;

typedef struct {
	int rows;
	int cols;
	sprite environment[TILESY][TILESX];
} map;

typedef struct {
	float x;
	float y;
	float zoom;
	float view_w;
	float view_h;
} camera;

typedef struct {
	SDL_Window* window;
	SDL_Renderer* renderer;
	std::unordered_map<std::string, SDL_Texture*> textures;
	std::unordered_map<std::string, grow_sprite> menu;
	sprite test_tree;
	SDL_Surface* mouse_sprite[2];
	SDL_Cursor* cursor[2];
	grid grid;
	map map;
	camera camera;

	difficulty current_difficulty;
	float zoom_dist;

	bool main_menu;
	bool difficulty_menu;
	bool game_started;
	bool game_paused;

	Uint64 start_time;
	Uint64 previous_time;
} game_state;

void debug(game_state* state) 
{
	std::cout << "(" << state->camera.x << ", " << state->camera.y << ")" << std::endl;
}

bool operator==(const SDL_FRect &a, const SDL_FRect &b)
{
	return a.h == b.h && a.w == b.w;
}

bool operator!=(const SDL_FRect &a, const SDL_FRect &b)
{
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

bool is_inside(float x, float y, float rect_x, float rect_y, float rect_w, float rect_h)
{
	return
		x >= rect_x && x <= rect_w &&
		y >= rect_y && y <= rect_h;
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

// void set_difficulty(game_state* state, difficulty difficulty)
// {
// 	state->current_difficulty = difficulty;
// 	int diff_multiplier = 1; // Needs to divide 720 or whatever resolution the grid has
//
// 	switch(state->current_difficulty) {
// 		case EASY:
// 			state->grid.rows = 5;
// 			state->grid.cols = 5;
// 			diff_multiplier = 4;
// 			break;
// 		case MEDIUM:
// 			state->grid.rows = 10;
// 			state->grid.cols = 10;
// 			diff_multiplier = 2;
// 			break;
// 		case HARD:
// 			state->grid.rows = 20;
// 			state->grid.cols = 20;
// 			diff_multiplier = 1;
// 			break;
// 		case WRAP: break;
// 	}
//
// 	float tree_w, tree_h;
// 	SDL_GetTextureSize(state->textures["tree"], &tree_w, &tree_h);
//
// 	for(int y = 0; y < state->grid.rows; ++y) {
// 		for(int x = 0; x < state->grid.cols; ++x) {
// 			SDL_FRect temp;
// 			temp.x = ((float)x) * tree_w * diff_multiplier;
// 			temp.y = ((float)y) * tree_h * diff_multiplier;
// 			temp.w = tree_w * diff_multiplier;
// 			temp.h = tree_h * diff_multiplier;
// 			state->grid.location[y][x].lerp_value = 1.0f;
// 			state->grid.location[y][x].render_size = state->grid.location[y][x].hover_size = state->grid.location[y][x].base_size = temp;
// 			resize_rect(&state->grid.location[y][x].hover_size, 1.15);
// 			resize_rect(&temp, HITBOX_SIZE);
// 			state->grid.hitboxes[y][x] = temp;
// 			state->grid.is_occupied[y][x] = true;
// 		}
// 	}
// }

void zoom(sprite *sprite, float zoom)
{
	sprite->size.w *= zoom;
	sprite->size.h *= zoom;
}

void create_menu(std::initializer_list<grow_sprite*> argument_list)
{
	float enlargement = 1.15f;

	std::vector<grow_sprite*> items(argument_list);

	for(int i = 0; i < items.size(); ++i) {
		SDL_GetTextureSize(items[i]->texture, &items[i]->base_size.w, &items[i]->base_size.h);
		items[i]->base_size.x = 100;

		if(i == 0)
			items[i]->base_size.y = 50;
		else
			items[i]->base_size.y = 100 + items[i - 1]->base_size.y + items[i - 1]->base_size.h;

		items[i]->hover_size = items[i]->render_size = items[i]->base_size;
		resize_rect(&items[i]->hover_size, enlargement);
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

	if(!SDL_CreateWindowAndRenderer("Tree Cutter Simulator", WIDTH, HEIGHT, NULL, &state->window, &state->renderer)) {
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	// SDL_SetWindowResizable(state->window, false);
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
	state->zoom_dist = 1.0f;

#ifdef DEBUG
	set_difficulty(state, MEDIUM);
#endif


	// TODO:
	// 1. complete test_tree
	//    where should the test tree be? i have TILESX/Y which is the world space but the game should begin in the middle of the world
	float tree_w, tree_h;
	SDL_GetTextureSize(state->textures["tree"], &tree_w, &tree_h);
	state->test_tree.texture = state->textures["tree"];
	state->test_tree.size.x = state->test_tree.size.y = 100;
	state->test_tree.size.w = state->test_tree.size.h = 72;

	// 2. i need a camera that defines what is visible and how large things should be
	state->camera.x = WIDTH / 2;
	state->camera.y = HEIGHT / 2;
	state->camera.zoom = 1.0;
	state->camera.view_w = WIDTH / state->camera.zoom;
	state->camera.view_h = HEIGHT / state->camera.zoom;

	// 3. renderer should loop over all the tiles inside the camera.
	//   calculate which parts of trees are visible for srcrect
	float width, height;
	SDL_GetTextureSize(state->textures["tree"], &width, &height);
	for(int y = 0; y < TILESY; ++y) {
		for(int x = 0; x < TILESX; ++x) {
			if(y == x) {
				state->map.environment[y][x].texture = state->textures["tree"];
			}
			else {
				state->map.environment[y][x].texture = nullptr;
			}
			state->map.environment[y][x].size.x = ((float)x) * width;
			state->map.environment[y][x].size.y = ((float)y) * height;
			state->map.environment[y][x].size.w = width;
			state->map.environment[y][x].size.h = height;
		}
	}

	// Other stuff:
	// - game_state.grid is obsolete. do not need rows, cols 
	// - create debug flags so that i dont have to bother with the menu. i just want to open the game and zoom in and out
	// - difficulty is obsolete

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

					float xlclick, ylclick, logical_x, logical_y;
					SDL_GetMouseState(&xlclick, &ylclick);
					SDL_RenderCoordinatesFromWindow(state->renderer, xlclick, ylclick, &logical_x, &logical_y);
					if(state->main_menu) {
						if(is_inside(logical_x, logical_y, state->menu["start-game"].render_size)) {
							state->main_menu = false;
							state->game_started = true;
							break;
						}
						if(is_inside(logical_x, logical_y, state->menu["difficulty"].render_size)) {
							state->main_menu = false;
							state->difficulty_menu = true;
						}
						if(is_inside(logical_x, logical_y, state->menu["exit"].render_size)) {
							return SDL_APP_SUCCESS;
						}
					}
					if(state->difficulty_menu) {
						if(is_inside(logical_x, logical_y, state->menu["easy"].render_size)) {
							// set_difficulty(state, EASY);
						}
						if(is_inside(logical_x, logical_y, state->menu["medium"].render_size)) {
							// set_difficulty(state, MEDIUM);
						}
						if(is_inside(logical_x, logical_y, state->menu["hard"].render_size)) {
							// set_difficulty(state, HARD);
						}
					}
					if(state->game_started && !state->game_paused) {
						for(int y = 0; y < state->grid.rows; ++y) {
							for(int x = 0; x < state->grid.cols; ++x) {
								if(is_inside(logical_x, logical_y, state->grid.hitboxes[y][x])) {
									state->grid.is_occupied[y][x] = false;
								}
							}
						}
					}
					if(state->game_paused) {
						if(is_inside(logical_x, logical_y, state->menu["continue"].hover_size)) {
							state->game_paused = false;
						}
						if(is_inside(logical_x, logical_y, state->menu["quit"].hover_size)) {
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
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if(event->wheel.y > 0) {
				state->camera.zoom += 0.0001f;
			} 
			else {
				state->camera.zoom -= 0.0001f;
			}
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
				case SDLK_LEFT:
					state->camera.x -= 1.0;
					break;
				case SDLK_RIGHT:
					state->camera.x += 1.0;
					break;
				case SDLK_UP:
					state->camera.y -= 1.0;
					break;
				case SDLK_DOWN:
					state->camera.y += 1.0;
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

#ifdef DEBUG
	float xpos, ypos;
	SDL_GetMouseState(&xpos, &ypos);
	float logical_x, logical_y;
	SDL_RenderCoordinatesFromWindow(state->renderer, xpos, ypos, &logical_x, &logical_y);
	float lerp_max = 1.15, lerp_min = 1.0;
	float lerp_value = 0.01;

	for(auto& [key, item] : state->menu) {
		if(is_inside(logical_x, logical_y, item.base_size)) {
			item.lerp_value= std::lerp(item.lerp_value, lerp_max, lerp_value);
		}
		else {
			item.lerp_value = std::lerp(item.lerp_value, lerp_min, lerp_value);
		}

		SDL_FRect temp;
		temp.x = item.base_size.x;
		temp.y = item.base_size.y;
		temp.w = item.base_size.w;
		temp.h = item.base_size.h;

		item.render_size.w = temp.w * (item.lerp_value);
		item.render_size.h = temp.h * (item.lerp_value);
		item.render_size.x = temp.x - (item.render_size.w - temp.w) / 2;
		item.render_size.y = temp.y - (item.render_size.h - temp.h) / 2;
	}

	if(state->game_started && !state->game_paused) {
		for(int y = 0; y < state->grid.rows; ++y) {
			for(int x = 0; x < state->grid.cols; ++x) {
				auto& item = state->grid.location[y][x];
				if(is_inside(logical_x, logical_y, state->grid.hitboxes[y][x])) {
					item.lerp_value = std::lerp(item.lerp_value, lerp_max, lerp_value);
				}
				else {
					item.lerp_value = std::lerp(item.lerp_value, lerp_min, lerp_value);
				}

				SDL_FRect temp;
				temp.x = item.base_size.x;
				temp.y = item.base_size.y;
				temp.w = item.base_size.w;
				temp.h = item.base_size.h;

				item.render_size.w = temp.w * (item.lerp_value);
				item.render_size.h = temp.h * (item.lerp_value);
				item.render_size.x = temp.x - (item.render_size.w - temp.w) / 2;
				item.render_size.y = temp.y - (item.render_size.h - temp.h) / 2;
			}
		}
	}

	for(int y = 0; y < state->grid.rows; ++y) {
		for(int x = 0; x < state->grid.cols; ++x) {
			SDL_RenderTexture(state->renderer, state->grid.ground_texture, NULL, &state->grid.location[y][x].base_size);
			if(state->grid.is_occupied[y][x]) {
				if(state->game_started && !state->game_paused) {
					SDL_RenderTexture(state->renderer, state->grid.tree_texture, NULL, &state->grid.location[y][x].render_size);
				}
				else {
					SDL_RenderTexture(state->renderer, state->grid.tree_texture, NULL, &state->grid.location[y][x].base_size);
				}

			}
			// SDL_RenderRect(state->renderer, &state->grid.hitboxes[y][x]);
		}
	}
#endif

	float left    = state->camera.x - state->camera.view_w / 2;
	float right   = state->camera.x + state->camera.view_w / 2;
	float top     = state->camera.y - state->camera.view_h / 2;
	float bottom  = state->camera.y + state->camera.view_h / 2;

	int col_start = (int)(left   / TILE_WORLD_SIZE);
	int col_end =   (int)(right  / TILE_WORLD_SIZE) + 1;
	int row_start = (int)(top    / TILE_WORLD_SIZE);
	int row_end =   (int)(bottom / TILE_WORLD_SIZE) + 1;

	for(int row = row_start; row < row_end; ++row) {
		for(int col = col_start; col < col_end; ++col) {
			SDL_RenderTexture(state->renderer, state->map.environment[row][col].texture, NULL, &state->map.environment[row][col].size);
		}
	}

	debug(state);

	if(state->main_menu) {
		SDL_RenderTexture(state->renderer, state->menu["start-game"].texture, NULL, &state->menu["start-game"].render_size);
		SDL_RenderTexture(state->renderer, state->menu["difficulty"].texture, NULL, &state->menu["difficulty"].render_size);
		SDL_RenderTexture(state->renderer, state->menu["exit"].texture, NULL, &state->menu["exit"].render_size);
	}
	if(state->difficulty_menu) {
		SDL_RenderTexture(state->renderer, state->menu["easy"].texture, NULL, &state->menu["easy"].render_size);
		SDL_RenderTexture(state->renderer, state->menu["medium"].texture, NULL, &state->menu["medium"].render_size);
		SDL_RenderTexture(state->renderer, state->menu["hard"].texture, NULL, &state->menu["hard"].render_size);
	}
	if(state->game_paused) {
		SDL_RenderTexture(state->renderer, state->menu["continue"].texture, NULL, &state->menu["continue"].render_size);
		SDL_RenderTexture(state->renderer, state->menu["quit"].texture, NULL, &state->menu["quit"].render_size);
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
