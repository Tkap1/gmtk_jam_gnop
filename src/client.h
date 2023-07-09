enum e_string_input_result
{
	e_string_input_result_none,
	e_string_input_result_submit,
};

enum e_state
{
	e_state_main_menu,
	e_state_game,
	e_state_victory,
	e_state_count,
};

enum e_font
{
	e_font_small,
	e_font_medium,
	e_font_big,
	e_font_huge,
	e_font_count,
};

struct s_glyph
{
	int advance_width;
	int width;
	int height;
	int x0;
	int y0;
	int x1;
	int y1;
	s_v2 uv_min;
	s_v2 uv_max;
};

struct s_font
{
	float size;
	float scale;
	int ascent;
	int descent;
	int line_gap;
	s_texture texture;
	s_glyph glyph_arr[1024];
};


enum e_shader
{
	e_shader_default,
	e_shader_count,
};

struct s_shader_paths
{
	#ifdef m_debug
	FILETIME last_write_time;
	#endif // m_debug
	char* vertex_path;
	char* fragment_path;
};

struct s_entity
{
	int id;
	union
	{
		struct
		{
			float x;
			float y;
		};
		s_v2 pos;
	};
};

struct s_ball : s_entity
{
	float speed;
	s_v2 dir;
	float hit_time;
};

struct s_paddle : s_entity
{
	float dir;
};

struct s_portal
{
	float radius;
	s_v2 active;
	s_v2 target;
};

struct s_score_pickup : s_entity
{
	float radius;
};

struct s_death_pickup : s_entity
{
	float radius;
};

struct s_particle_spawn_data
{
	s8 render_type;
	float speed;
	float speed_rand;
	float radius;
	float radius_rand;
	float duration;
	float duration_rand;
	float angle;
	float angle_rand;
	s_v2 pos;
	s_v4 color;
	float color_rand;
};

struct s_particle
{
	s8 render_type;
	float time;
	float duration;
	float speed;
	s_v2 pos;
	s_v2 dir;
	float radius;
	s_v4 color;
};


struct s_level
{
	b8 spawn_pickups;
	b8 paddles_give_score;
	b8 moving_paddles;
	b8 synced_paddles;
	b8 obstacles;
	b8 portals;
	b8 boss;
	int score_to_beat;
	int obstacle_count;
	float paddle_speed;
	float ball_radius;
	float speed_boost;
	float ball_speed;
	float obstacle_radius;
	float portal_radius;
	s_v2 paddle_size;
};

struct s_delayed_sound
{
	float time;
	float delay;
	s_sound sound;
};

struct s_game
{
	b8 initialized;
	b8 reset_game;
	b8 reset_level;
	b8 beat_level;
	b8 go_to_previous_level;
	b8 spawn_obstacles;
	b8 spawn_portals;
	int current_resolution_index;
	int current_level;
	int score;
	int level_count;
	int update_count;
	int frame_count;
	float total_time;
	s_v2 title_pos;
	s_v3 title_color;
	s_ball ball;
	s_paddle paddle;
	s_paddle boss_paddle;
	f64 update_timer;
	s_rng rng;
	e_state state;
	s_font font_arr[e_font_count];

	s_sarray<s_level, 128> levels;
	s_sarray<s_particle, 16384> particles;
	s_sarray<s_score_pickup, 64> score_pickups;
	s_sarray<s_death_pickup, 64> death_pickups;
	s_sarray<s_portal, 64> portals;
	s_sarray<s_delayed_sound, 64> delayed_sounds;

	u32 default_vao;
	u32 default_ssbo;

	u32 programs[e_shader_count];

	s_sound paddle_sound;
	s_sound score_pickup_sound;
	s_sound win_sound;
	s_sound fail_sound;
	s_sound portal_sound;

	s_texture noise;
};

func void update();
func void render(float dt);
func b8 check_for_shader_errors(u32 id, char* out_error);
func void input_system(int start, int count);
func void draw_system(int start, int count, float dt);
func void revive_every_player(void);
func void draw_circle_system(int start, int count, float dt);
func void collision_system(int start, int count);
func s_font load_font(const char* path, float font_size, s_lin_arena* arena);
func s_texture load_texture_from_data(void* data, int width, int height, u32 filtering);
func s_v2 get_text_size(const char* text, e_font font_id);
func s_v2 get_text_size_with_count(const char* text, e_font font_id, int count);
func u32 load_shader(const char* vertex_path, const char* fragment_path);
func void handle_instant_movement_(int entity);
func void handle_instant_resize_(int entity);
func b8 is_key_down(int key);
func b8 is_key_up(int key);
func b8 is_key_pressed(int key);
func b8 is_key_released(int key);
void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
func void sine_alpha_system(int start, int count);
func s_v4 get_ball_color(s_ball ball);
func void init_levels();
func void do_ball_trail(s_ball old_ball, s_ball ball, float radius);
func char* handle_plural(int num);
func s_texture load_texture_from_file(char* path, u32 filtering);
func void spawn_particles(int count, s_particle_spawn_data data);
func void play_delayed_sound(s_sound sound, float delay);

#ifdef m_debug
func void hot_reload_shaders(void);
#endif // m_debug
