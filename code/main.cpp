#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <cmath>

#define TILES_X 4000
#define TILES_Y 3000
#define DEFAULT_VIEW_TILES 20
#define WORLD_TILE_SIZE 32
#define MIN_ZOOM 0.1f
#define MAX_ZOOM 4.0f
#define ZOOM_STEP 1.15f   // zoom factor per wheel notch
#define ZOOM_SPEED 12.0f  // how fast zoom catches up to target, higher = snappier

enum tile_type : uint8_t {
	TILE_NONE,
	TILE_GROUND,
	TILE_TREE,
	TILE_COUNT
};

static const char* tile_files[TILE_COUNT] = {
	nullptr,
	"assets/map/ground.png",
	"assets/map/tree.png",
};

typedef struct {
	SDL_Texture* texture;
	SDL_FRect base_size;
	SDL_FRect hover_size;
	SDL_FRect render_size;
	float lerp_value;
} grow_sprite;

// TODO:
// read the claude chat about the menu system using a stack
// change the "difficulty" png to "settings"
typedef struct {
	std::unordered_map<std::string, grow_sprite> sprites;
	bool main_menu;
	bool settings;
	bool game_started;
	bool game_paused;
} menu;

// TODO:
// one container for environment (ground, water, mountain etc) and another for trees that renders on top of the environment
// ground, water and mountains all have different properties so the container needs to be generic
typedef struct {
	SDL_Texture* textures[TILE_COUNT];
	tile_type environment[TILES_Y][TILES_X];
	tile_type trees[TILES_Y][TILES_X];
} map;

typedef struct {
	float x, y;
	float zoom;
	float target_zoom;
	float view_w, view_h;
} camera;

typedef struct {
	SDL_Window* window;
	SDL_Renderer* renderer;
	Uint64 last_ticks;
	std::vector<SDL_Texture*> textures;
	menu menu;
	map map;
	camera camera;
} game;

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

void update_camera(game* state, float dt)
{
	camera* camera = &state->camera;

	float diff = SDL_logf(camera->target_zoom) - SDL_logf(camera->zoom);
	if(SDL_fabsf(diff) < 0.0001f) {
		camera->zoom = camera->target_zoom;
		return;
	}

	int log_w, log_h;
	SDL_RendererLogicalPresentation mode;
	SDL_GetRenderLogicalPresentation(state->renderer, &log_w, &log_h, &mode);

	// mouse position in logical screen coordinates
	float mouse_x, mouse_y;
	SDL_GetMouseState(&mouse_x, &mouse_y);
	SDL_RenderCoordinatesFromWindow(state->renderer, mouse_x, mouse_y, &mouse_x, &mouse_y);

	// world point currently under the mouse
	float scale = (float)log_w / camera->view_w;
	float world_mouse_x = (camera->x - camera->view_w / 2) + mouse_x / scale;
	float world_mouse_y = (camera->y - camera->view_h / 2) + mouse_y / scale;

	// ease zoom toward target, in log space so zooming in and out feel the same
	float t = 1.0f - SDL_expf(-ZOOM_SPEED * dt);
	camera->zoom = SDL_expf(SDL_logf(camera->zoom) + diff * t);

	camera->view_w = (DEFAULT_VIEW_TILES * WORLD_TILE_SIZE) / camera->zoom;
	camera->view_h = camera->view_w * ((float)log_h / (float)log_w);

	// move the camera so the same world point is still under the mouse
	scale = (float)log_w / camera->view_w;
	camera->x = world_mouse_x - mouse_x / scale + camera->view_w / 2;
	camera->y = world_mouse_y - mouse_y / scale + camera->view_h / 2;
}

std::filesystem::path asset_path(const std::string& relative)
{
	return std::filesystem::path(SDL_GetBasePath()) / ".." / relative;
}

SDL_Texture* load_texture(game* state, const std::filesystem::path& png_path)
{
	SDL_Surface* surface = SDL_LoadPNG(png_path.string().c_str());
	if(!surface) {
		SDL_Log("Couldn't load png: %s", SDL_GetError());
		return nullptr;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(state->renderer, surface);
	SDL_DestroySurface(surface);
	if(!texture) {
		SDL_Log("Couldn't create texture from surface: %s", SDL_GetError());
		return nullptr;
	}
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

	state->textures.push_back(texture);
	return texture;
}

template <typename T>
void load_textures(game* state, const std::string& folder, std::unordered_map<std::string, T>& sprites)
{
	for(const auto& entry : std::filesystem::directory_iterator(asset_path(folder))) {
		if(entry.is_regular_file() && entry.path().extension() == ".png") {
			sprites[entry.path().stem().string()].texture = load_texture(state, entry.path());
		}
	}
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
	game* state = new game;
	if(!state) {
		return SDL_APP_FAILURE;
	}
	*appstate = state;

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if(!SDL_CreateWindowAndRenderer("Tree Cutter Simulator", 1920, 1080, NULL, &state->window, &state->renderer)) {
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_SetRenderLogicalPresentation(state->renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(state->renderer, 1);

	load_textures(state, "assets/menu", state->menu.sprites);
	for(int i = 0; i < TILE_COUNT; ++i) {
		state->map.textures[i] = tile_files[i] ? load_texture(state, asset_path(tile_files[i])) : nullptr;
	}

	create_menu({&state->menu.sprites["start-game"], &state->menu.sprites["difficulty"], &state->menu.sprites["exit"]});
	create_menu({&state->menu.sprites["easy"], &state->menu.sprites["medium"], &state->menu.sprites["hard"]});
	create_menu({&state->menu.sprites["continue"], &state->menu.sprites["quit"]});

	for(int y = 0; y < TILES_Y; ++y) {
		for(int x = 0; x < TILES_X; ++x) {
			state->map.trees[y][x] = TILE_TREE;
			state->map.environment[y][x] = TILE_GROUND;
		}
	}

	camera* camera = &state->camera;
	camera->zoom = 1.0f;
	camera->target_zoom = 1.0f;
	camera->x = TILES_X * WORLD_TILE_SIZE / 2.0f;
	camera->y = TILES_Y * WORLD_TILE_SIZE / 2.0f;
	camera->view_w = (DEFAULT_VIEW_TILES * WORLD_TILE_SIZE) / camera->zoom;
	int log_w, log_h;
	SDL_RendererLogicalPresentation mode;
	SDL_GetRenderLogicalPresentation(state->renderer, &log_w, &log_h, &mode);
	camera->view_h = camera->view_w * ((float)log_h / (float)log_w);

	state->last_ticks = SDL_GetTicksNS();

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	game* state = (game*) appstate;
	camera* camera = &state->camera;
	switch(event->type) {
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;
		case SDL_EVENT_MOUSE_MOTION:
			if(event->motion.state & SDL_BUTTON_LMASK) {
				SDL_ConvertEventToRenderCoordinates(state->renderer, event);
				int log_w, log_h;
				SDL_RendererLogicalPresentation mode;
				SDL_GetRenderLogicalPresentation(state->renderer, &log_w, &log_h, &mode);
				float scale_x = (float)log_w / camera->view_w;
				float scale_y = (float)log_h / camera->view_h;
				camera->x -= event->motion.xrel / scale_x;
				camera->y -= event->motion.yrel / scale_y;
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			camera->target_zoom *= SDL_powf(ZOOM_STEP, event->wheel.y);
			camera->target_zoom = SDL_clamp(camera->target_zoom, MIN_ZOOM, MAX_ZOOM);
			break;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	game* state = (game*) appstate;
	camera* camera = &state->camera;

	Uint64 now = SDL_GetTicksNS();
	float dt = (now - state->last_ticks) / 1e9f;
	state->last_ticks = now;
	update_camera(state, dt);

	SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
	SDL_RenderClear(state->renderer);

	float left = camera->x - camera->view_w / 2;
	float right = camera->x + camera->view_w / 2;
	float top = camera->y - camera->view_h / 2;
	float bottom = camera->y + camera->view_h / 2;

	int col_start = (int)(left / WORLD_TILE_SIZE);
	if(col_start <= 0) col_start = 0; 
	int col_end = (int)(right / WORLD_TILE_SIZE) + 1;
	if(col_end >= TILES_X) col_end = TILES_X; 
	int row_start = (int)(top / WORLD_TILE_SIZE);
	if(row_start <= 0) row_start = 0; 
	int row_end = (int)(bottom / WORLD_TILE_SIZE) + 1;
	if(row_end >= TILES_Y) row_end = TILES_Y; 

	int log_w, log_h;
	SDL_RendererLogicalPresentation mode;
	SDL_GetRenderLogicalPresentation(state->renderer, &log_w, &log_h, &mode);
	float scale_x = (float)log_w / camera->view_w;
	float scale_y = (float)log_h / camera->view_h;
	for(int row = row_start; row < row_end; ++row) {
		for(int col = col_start; col < col_end; ++col) {
			float tile_world_x = col * WORLD_TILE_SIZE;
			float tile_world_y = row * WORLD_TILE_SIZE;
			SDL_FRect dstrect;
			dstrect.x = (tile_world_x - left) * scale_x;
			dstrect.y = (tile_world_y - top) * scale_y;
			dstrect.w = WORLD_TILE_SIZE * scale_x;
			dstrect.h = WORLD_TILE_SIZE * scale_y;
			SDL_Texture* tree_texture = state->map.textures[state->map.trees[row][col]];
			SDL_Texture* env_texture = state->map.textures[state->map.environment[row][col]];
			if(env_texture) SDL_RenderTexture(state->renderer, env_texture, NULL, &dstrect);
			if(tree_texture) SDL_RenderTexture(state->renderer, tree_texture, NULL, &dstrect);
		}
	}

	SDL_RenderPresent(state->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	game* state = (game*) appstate;
	for(SDL_Texture* texture : state->textures) {
		SDL_DestroyTexture(texture);
	}
	SDL_DestroyRenderer(state->renderer);
	SDL_DestroyWindow(state->window);
	delete state;
}
