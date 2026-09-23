#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <cmath>

#define TILES_X 400
#define TILES_Y 300
#define DEFAULT_VIEW_TILES 20
#define TILE_WORLD_SIZE 32

enum Tile_Type : uint8_t {
	TILE_NONE,
	TILE_GROUND,
	TILE_WATER,
	TILE_TREE,
	TILE_COUNT
};

static const char* Tile_Files[TILE_COUNT] = {
	nullptr,
	"assets/map/ground.png",
	"assets/map/water.png",
	"assets/map/tree.png",
};

struct Grow_Sprite {
	SDL_Texture* texture;
	SDL_FRect base_size;
	SDL_FRect hover_size;
	SDL_FRect render_size;
	float lerp_value;
};

// TODO:
// - Read the claude chat about the menu system using a stack
// - Change the "difficulty" png to "settings"
struct Menu {
	std::unordered_map<std::string, Grow_Sprite> sprites;
	bool main_menu;
	bool settings;
	bool game_started;
	bool game_paused;
};

// TODO:
// - One container for environment (ground, water, mountain etc) and another for trees that renders on top of the 
// environment. Ground, water and mountains all have different properties so the container needs to be generic
// - Random environment generation
struct Map {
	SDL_Texture* textures[TILE_COUNT];
	Tile_Type environment[TILES_Y][TILES_X];
	Tile_Type trees[TILES_Y][TILES_X];
};

struct View {
	float left, right, top, bottom;
	int col_start, col_end, row_start, row_end;
};

// TODO:
// - Make it impossible to move the camera outside of bounds
// - Move when mouse is at screen edges
struct Camera {
	float x, y, w, h;
	float zoom_level;
	float target_zoom;
	View view;
	SDL_Renderer* renderer = nullptr;

	Camera() {
		zoom_level = 1.0f;
		target_zoom = 1.0f;
		w = DEFAULT_VIEW_TILES * TILE_WORLD_SIZE / zoom_level;
		h = w * (1080.0f / 1920.0f);
		x = TILES_X * TILE_WORLD_SIZE / 2;
		y = TILES_Y * TILE_WORLD_SIZE / 2;
	}

	View get_view() {
		view.left =   x - (w / 2);
		view.right =  x + (w / 2);
		view.top =    y - (h / 2);
		view.bottom = y + (h / 2);

		view.col_start = (int)(view.left / TILE_WORLD_SIZE);
		if(view.col_start <= 0) view.col_start = 0;

		view.col_end = (int)(view.right / TILE_WORLD_SIZE) + 1;
		if(view.col_end >= TILES_X) view.col_end = TILES_X;

		view.row_start = (int)(view.top / TILE_WORLD_SIZE);
		if(view.row_start <= 0) view.row_start = 0;

		view.row_end = (int)(view.bottom / TILE_WORLD_SIZE) + 1;
		if(view.row_end >= TILES_Y) view.row_end = TILES_Y;

		return view;
	}

	void zoom(float zoom_amount) {
		target_zoom *= std::pow(1.15f, zoom_amount);
		target_zoom = std::clamp(target_zoom, 0.05f, 4.0f);
	}

	void move(float x, float y) {
		SDL_RendererLogicalPresentation mode;
		int log_w, log_h;
		SDL_GetRenderLogicalPresentation(renderer, &log_w, &log_h, &mode);
		float scale = ((float)log_w) / this->w;
		this->x += x / scale;
		this->y += y / scale;
	}

	void update(float dt) {
		SDL_RendererLogicalPresentation mode;
		int log_w, log_h;
		SDL_GetRenderLogicalPresentation(renderer, &log_w, &log_h, &mode);

		float win_x, win_y;
		SDL_GetMouseState(&win_x, &win_y);

		float mouse_x, mouse_y;
		SDL_RenderCoordinatesFromWindow(renderer, win_x, win_y, &mouse_x, &mouse_y);

		float scale_before = log_w / w;
		float world_x = x + (mouse_x - log_w * 0.5f) / scale_before;
		float world_y = y + (mouse_y - log_h * 0.5f) / scale_before;

		float speed = 12.0f;
		float t = 1.0f - std::exp(-speed * dt);
		zoom_level = std::exp(std::lerp(std::log(zoom_level), std::log(target_zoom), t));
		w = DEFAULT_VIEW_TILES * TILE_WORLD_SIZE / zoom_level;
		h = w * (1080.0f / 1920.0f);

		float scale_after = log_w / w;
		x = world_x - (mouse_x - log_w * 0.5f) / scale_after;
		y = world_y - (mouse_y - log_h * 0.5f) / scale_after;
	}
};

struct Game {
	SDL_Window* window;
	SDL_Renderer* renderer;
	std::vector<SDL_Texture*> textures;
	Uint64 last_ticks;
	Menu menu;
	Map map;
	Camera camera;
	bool mouse_down;
};

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

std::filesystem::path asset_path(const std::string& relative)
{
	return std::filesystem::path(SDL_GetBasePath()) / ".." / relative;
}

SDL_Texture* load_texture(Game* state, const std::filesystem::path& png_path)
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
void load_textures(Game* state, const std::string& folder, std::unordered_map<std::string, T>& sprites)
{
	for(const auto& entry : std::filesystem::directory_iterator(asset_path(folder))) {
		if(entry.is_regular_file() && entry.path().extension() == ".png") {
			sprites[entry.path().stem().string()].texture = load_texture(state, entry.path());
		}
	}
}

void create_menu(std::initializer_list<Grow_Sprite*> argument_list)
{
	float enlargement = 1.15f;

	std::vector<Grow_Sprite*> items(argument_list);

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
	Game* state = new Game;
	if(!state) {
		return SDL_APP_FAILURE;
	}
	*appstate = state;
	state->last_ticks = SDL_GetTicksNS();

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if(!SDL_CreateWindowAndRenderer("Tree Cutter Simulator", 1920, 1080, NULL, &state->window, &state->renderer)) {
		SDL_Log("Couldn't create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	state->camera.renderer = state->renderer;

	SDL_SetRenderLogicalPresentation(state->renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(state->renderer, 1);

	load_textures(state, "assets/menu", state->menu.sprites);
	for(int i = 0; i < TILE_COUNT; ++i) {
		state->map.textures[i] = Tile_Files[i] ? load_texture(state, asset_path(Tile_Files[i])) : nullptr;
	}

	create_menu({&state->menu.sprites["start-game"], &state->menu.sprites["settings"], &state->menu.sprites["exit"]});
	create_menu({&state->menu.sprites["easy"], &state->menu.sprites["medium"], &state->menu.sprites["hard"]});
	create_menu({&state->menu.sprites["continue"], &state->menu.sprites["quit"]});

	for(int y = 0; y < TILES_Y; ++y) {
		for(int x = 0; x < TILES_X; ++x) {
			state->map.trees[y][x] = TILE_TREE;
			state->map.environment[y][x] = TILE_GROUND;
		}
	}

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	Game* state = (Game*) appstate;
	Camera* camera = &state->camera;

	switch(event->type) {
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			state->mouse_down = true;
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			state->mouse_down = false;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if(state->mouse_down) {
				camera->move(-event->motion.xrel, -event->motion.yrel);
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			camera->zoom(event->wheel.y);
			break;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	Game* state = (Game*) appstate;
	Camera* camera = &state->camera;

	Uint64 now = SDL_GetTicksNS();
	float dt = (now - state->last_ticks) / 1e9f;
	state->last_ticks = now;
	dt = std::min(dt, 0.1f);
	camera->update(dt);

	SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
	SDL_RenderClear(state->renderer);

	SDL_RendererLogicalPresentation mode;
	int log_w, log_h;
	SDL_GetRenderLogicalPresentation(state->renderer, &log_w, &log_h, &mode);
	float scale = ((float)log_w) / camera->w;

	View view = camera->get_view();
	for(int row = view.row_start; row < view.row_end; ++row) {
		for(int col = view.col_start; col < view.col_end; ++col) {
			float tile_world_x = col * TILE_WORLD_SIZE;
			float tile_world_y = row * TILE_WORLD_SIZE;

			SDL_FRect dstrect;
			dstrect.x = (tile_world_x - view.left) * scale;
			dstrect.y = (tile_world_y - view.top) * scale;
			dstrect.w = TILE_WORLD_SIZE * scale;
			dstrect.h = TILE_WORLD_SIZE * scale;

			SDL_Texture* env_texture = state->map.textures[state->map.environment[row][col]];
			SDL_Texture* tree_texture = state->map.textures[state->map.trees[row][col]];
			if(env_texture) SDL_RenderTexture(state->renderer, env_texture, NULL, &dstrect);
			if(tree_texture) SDL_RenderTexture(state->renderer, tree_texture, NULL, &dstrect);
		}
	}

	SDL_RenderPresent(state->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	Game* state = (Game*) appstate;
	for(SDL_Texture* texture : state->textures) {
		SDL_DestroyTexture(texture);
	}
	SDL_DestroyRenderer(state->renderer);
	SDL_DestroyWindow(state->window);
	delete state;
}
