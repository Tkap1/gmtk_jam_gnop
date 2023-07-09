
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "opengl32.lib")

#include "pch_client.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert assert
#include "external/stb_truetype.h"

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT assert
#include "external/stb_image.h"
#pragma warning(pop)



#include "config.h"
#include "platform_shared.h"


struct s_texture
{
	u32 id;
	s_v2 size;
	s_v2 sub_size;
};


#include "client.h"
#include "shader_shared.h"
#include "audio.h"

global s_sarray<s_transform, 16384> transforms;
global s_sarray<s_transform, 16384> particles;
global s_sarray<s_transform, c_max_entities> text_arr[e_font_count];

global s_lin_arena* frame_arena;

global s_game_window g_window;
global s_input* g_input;

global s_platform_data g_platform_data;
global s_platform_funcs g_platform_funcs;

global s_game* game;

global s_v2 previous_mouse;

global s_shader_paths shader_paths[e_shader_count] = {
	{
		.vertex_path = "shaders/vertex.vertex",
		.fragment_path = "shaders/fragment.fragment",
	},
};


#define X(type, name) static type name = null;
m_gl_funcs
#undef X

#include "draw.cpp"
#include "memory.cpp"
#include "file.cpp"
#include "str_builder.cpp"
#include "audio.cpp"

#ifdef m_debug
extern "C" {
__declspec(dllexport)
#endif // m_debug
m_update_game(update_game)
{
	static_assert(c_game_memory >= sizeof(s_game));
	static_assert((c_max_entities % c_num_threads) == 0);
	game = (s_game*)game_memory;
	frame_arena = platform_data.frame_arena;
	g_platform_funcs = platform_funcs;
	g_platform_data = platform_data;
	g_input = platform_data.input;

	if(!game->initialized)
	{
		game->initialized = true;
		#define X(type, name) name = (type)platform_funcs.load_gl_func(#name);
		m_gl_funcs
		#undef X

		game->current_resolution_index = c_base_resolution_index;

		glDebugMessageCallback(gl_debug_callback, null);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		game->rng.seed = (u32)__rdtsc();
		game->reset_game = true;

		game->state = e_state_main_menu;

		platform_funcs.set_swap_interval(1);

		game->paddle_sound = load_wav("assets/jump.wav", frame_arena);
		game->score_pickup_sound = load_wav("assets/jump2.wav", frame_arena);
		game->win_sound = load_wav("assets/win.wav", frame_arena);
		game->fail_sound = load_wav("assets/fail.wav", frame_arena);
		game->portal_sound = load_wav("assets/portal.wav", frame_arena);

		game->noise = load_texture_from_file("assets/noise.png", GL_LINEAR);

		game->font_arr[e_font_small] = load_font("assets/consola.ttf", 24, frame_arena);
		game->font_arr[e_font_medium] = load_font("assets/consola.ttf", 36, frame_arena);
		game->font_arr[e_font_big] = load_font("assets/consola.ttf", 72, frame_arena);
		game->font_arr[e_font_huge] = load_font("assets/consola.ttf", 256, frame_arena);

		for(int shader_i = 0; shader_i < e_shader_count; shader_i++)
		{
			game->programs[shader_i] = load_shader(shader_paths[shader_i].vertex_path, shader_paths[shader_i].fragment_path);
		}

		glGenVertexArrays(1, &game->default_vao);
		glBindVertexArray(game->default_vao);

		glGenBuffers(1, &game->default_ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, game->default_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, game->default_ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(transforms.elements), null, GL_DYNAMIC_DRAW);

		init_levels();
	}

	if(platform_data.recompiled)
	{
		init_levels();
		#define X(type, name) name = (type)platform_funcs.load_gl_func(#name);
		m_gl_funcs
		#undef X
	}

	g_window.width = platform_data.window_width;
	g_window.height = platform_data.window_height;
	g_window.size = v2ii(g_window.width, g_window.height);
	g_window.center = v2_mul(g_window.size, 0.5f);


	#ifdef m_debug
	if(is_key_pressed(c_key_f8))
	{
		write_file("save_state", game, sizeof(*game));
	}

	if(is_key_pressed(c_key_f9))
	{
		char* data = read_file("save_state", frame_arena, null);
		if(data)
		{
			memcpy(game, data, sizeof(*game));
			game->paddle_sound = load_wav("assets/jump.wav", frame_arena);
			game->score_pickup_sound = load_wav("assets/jump2.wav", frame_arena);
			game->win_sound = load_wav("assets/win.wav", frame_arena);
			game->fail_sound = load_wav("assets/fail.wav", frame_arena);
			game->portal_sound = load_wav("assets/portal.wav", frame_arena);
		}
	}
	#endif // m_debug


	if(is_key_pressed(c_key_f2))
	{
		game->current_resolution_index = g_platform_funcs.cycle_between_available_resolutions(game->current_resolution_index);
		g_platform_data.any_key_pressed = false;
	}
	g_platform_data.mouse.x *= c_base_res.x / (float)c_resolutions[game->current_resolution_index].x;
	g_platform_data.mouse.y *= c_base_res.y / (float)c_resolutions[game->current_resolution_index].y;

	game->update_timer += g_platform_data.time_passed;
	game->frame_count += 1;
	while(game->update_timer >= c_update_delay)
	{
		game->update_timer -= c_update_delay;
		update();

		for(int k_i = 0; k_i < c_max_keys; k_i++)
		{
			g_input->keys[k_i].count = 0;
		}
	}

	float interpolation_dt = (float)(game->update_timer / c_update_delay);
	render(interpolation_dt);
	// memset(game->e.drawn_last_render, true, sizeof(game->e.drawn_last_render));

	game->total_time += (float)platform_data.time_passed;

	frame_arena->used = 0;
}

#ifdef m_debug
}
#endif // m_debug

func void update()
{
	game->update_count += 1;
	float delta = c_delta;
	// g_platform_funcs.show_cursor(false);

	switch(game->state)
	{
		case e_state_main_menu:
		{
			game->title_pos = v2(
				c_half_res.x,
				range_lerp(sinf(game->total_time * 2), -1, 1, c_base_res.y * 0.2f, c_base_res.y * 0.4f)
			);
			s_ball* ball = &game->ball;
			s_ball old_ball = *ball;
			ball->x = c_half_res.x;
			ball->y = lerp(ball->y, g_platform_data.mouse.y, 40.0f * delta);
			ball->y = clamp(ball->y, 0.0f, c_base_res.y);
			game->title_color = v3(sinf2(game->total_time), 1, 1);
			do_ball_trail(old_ball, *ball, 16);

			if(g_platform_data.any_key_pressed)
			{
				game->state = e_state_game;
			}
		} break;

		case e_state_game:
		{
			#ifdef m_debug
			if(is_key_pressed(c_key_add))
			{
				game->beat_level = true;
			}
			if(is_key_pressed(c_key_subtract))
			{
				game->go_to_previous_level = true;
			}
			if(is_key_pressed(c_key_f1))
			{
				game->current_level = game->levels.count - 2;
				game->beat_level = true;
			}
			#endif // m_debug

			s_ball* ball = &game->ball;
			s_paddle* paddle = &game->paddle;
			s_rng* rng = &game->rng;
			s_level* level = &game->levels[game->current_level];

			// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		reset game start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if(game->reset_game)
			{
				game->reset_game = false;
				game->current_level = 0;
				game->reset_level = true;
				level = &game->levels[game->current_level];
			}
			// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		reset game end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

			if(game->reset_level)
			{
				game->reset_level = false;
				init_levels();
				game->score = 0;

				game->boss_paddle.pos = v2(c_base_res.x - c_boss_paddle_size.x / 2, c_half_res.y);

				game->score_pickups.count = 0;
				game->death_pickups.count = 0;
				game->portals.count = 0;
				game->score = 0;
				ball->speed = level->ball_speed;
				ball->hit_time = 0;
				ball->x = c_half_res.x;

				if(rng->rand_bool() && !level->boss)
				{
					paddle->x = c_base_res.x - level->paddle_size.x / 2;
					ball->dir.x = 1;
				}
				else
				{
					paddle->x = level->paddle_size.x / 2;
					ball->dir.x = -1;
				}
				paddle->y = c_half_res.y;

				paddle->dir = 1;
			}

			if(game->spawn_obstacles)
			{
				game->death_pickups.count = 0;
				game->spawn_obstacles = false;
				for(int i = 0; i < level->obstacle_count; i++)
				{
					s_v2 topleft;
					s_v2 bottomright;
					if(ball->dir.x < 0)
					{
						topleft = v2(c_base_res.x * 0.1f, 0);
						bottomright = v2(c_base_res.x * 0.45f, c_base_res.y);
					}
					else
					{
						topleft = v2(c_base_res.x * 0.55f, 0);
						bottomright = v2(c_base_res.x * 0.9f, c_base_res.y);
					}
					float x = rng->randf_range(topleft.x, bottomright.x);
					float y = rng->randf_range(topleft.y, bottomright.y);

					s_death_pickup pickup = zero;
					pickup.x = x;
					pickup.y = y;
					pickup.radius = level->obstacle_radius;
					game->death_pickups.add_checked(pickup);
				}
			}

			if(game->spawn_portals)
			{
				game->spawn_portals = false;
				game->portals.count = 0;
				game->death_pickups.count = 0;
				s_portal portal = zero;
				float temp = ball->dir.x > 0 ? 0.3f : 0.7f;
				portal.active.x = c_base_res.x * temp;
				portal.active.y = c_base_res.y * rng->randf32();
				portal.target.x = c_base_res.x * (1.0f - temp);
				portal.target.y = c_base_res.y * rng->randf32();
				portal.radius = level->portal_radius;
				game->portals.add(portal);

				for(int i = 0; i < 30; i++)
				{
					float p = i / 29.0f;
					s_death_pickup pickup = zero;
					pickup.pos = v2(
						c_half_res.x,
						c_base_res.y * p
					);
					pickup.radius = level->obstacle_radius;
					game->death_pickups.add(pickup);
				}
			}

			// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		update ball start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			s_ball old_ball = *ball;

			ball->x += ball->dir.x * delta * ball->speed;
			ball->y = lerp(ball->y, g_platform_data.mouse.y, 40.0f * delta);
			ball->y = clamp(ball->y, 0.0f, c_base_res.y);

			do_ball_trail(old_ball, *ball, level->ball_radius);

			ball->hit_time = at_least(0.0f, ball->hit_time - delta);
			s_v2 ball_pos_before_collision = ball->pos;
			if(rect_collides_circle(paddle->pos, level->paddle_size, ball->pos, level->ball_radius))
			{

				if(level->boss)
				{
					if(game->score == 99)
					{
						level->ball_radius = 128;
						ball->speed = 400;
						ball->x = paddle->x - (level->paddle_size.x / 2.0f * ball->dir.x) - (level->ball_radius * ball->dir.x);
					}
				}
				else
				{
					ball->x = paddle->x - (level->paddle_size.x / 2.0f * ball->dir.x) - (level->ball_radius * ball->dir.x);
					paddle->x += c_base_res.x * -ball->dir.x;
					paddle->x += level->paddle_size.x * ball->dir.x;
				}
				if(level->obstacles) { game->spawn_obstacles = true; }
				if(level->portals) { game->spawn_portals = true; }

				if(!level->synced_paddles && !level->boss)
				{
					paddle->dir = rng->rand_bool() ? 1.0f : -1.0f;

					// @Note(tkap, 08/07/2023): Try to place paddle at random position that doesn't overlap with previous position
					{
						float prev_paddle_y = paddle->y;
						int attempts = 0;
						while(attempts < 100)
						{
							paddle->y = rng->randf_range(level->paddle_size.y / 2, c_base_res.y - level->paddle_size.y / 2);
							if(fabsf(paddle->y - prev_paddle_y) > level->paddle_size.y) { break; }
							attempts++;
						}
					}
				}
				ball->dir.x = -ball->dir.x;
				ball->speed += level->speed_boost;
				ball->speed = at_most(c_ball_max_speed, ball->speed);
				g_platform_funcs.play_sound(game->paddle_sound);

				if(level->paddles_give_score)
				{
					game->score += 1;
				}
				ball->hit_time = c_ball_hit_time;

				float l = at_least(1.0f, powf(ball->speed - 800, 0.1f));
				spawn_particles(100, {
					.render_type = 1,
					.speed = 100 * l,
					.speed_rand = 1,
					.radius = level->ball_radius * l,
					.radius_rand = 1,
					.duration = 0.5f,
					.duration_rand = 1,
					.angle_rand = 1,
					.pos = ball_pos_before_collision,
					.color = v4(0.5f, 0.25f, 0.05f, 1),
				});

				if(level->spawn_pickups && game->score_pickups.count == 0)
				{
					s_score_pickup pickup = zero;
					float radius = c_half_res.x * (c_base_res.y / c_base_res.x);
					float angle = rng->randf32() * tau;
					pickup.x = cosf(angle) * radius * sqrtf(rng->randf32()) + c_half_res.x;
					pickup.y = sinf(angle) * radius * sqrtf(rng->randf32()) + c_half_res.y;
					pickup.radius = level->ball_radius;
					game->score_pickups.add_checked(pickup);
				}

			}

			if(level->boss)
			{
				s_paddle* boss_paddle = &game->boss_paddle;
				if(rect_collides_circle(boss_paddle->pos, c_boss_paddle_size, ball->pos, level->ball_radius))
				{
					ball->dir.x = -1;
					ball->hit_time = c_ball_hit_time;
					ball->x = boss_paddle->x - c_boss_paddle_size.x / 2.0f - level->ball_radius;

					float l = at_least(1.0f, powf(ball->speed - 800, 0.1f));
					spawn_particles(100, {
						.render_type = 1,
						.speed = 100 * l,
						.speed_rand = 1,
						.radius = level->ball_radius * l,
						.radius_rand = 1,
						.duration = 0.5f,
						.duration_rand = 1,
						.angle_rand = 1,
						.pos = ball_pos_before_collision,
						.color = v4(0.5f, 0.25f, 0.05f, 1),
					});
					g_platform_funcs.play_sound(game->paddle_sound);
					game->score += 1;

					ball->speed += level->speed_boost;
					ball->speed = at_most(c_ball_max_speed, ball->speed);

					if(game->score == 3)
					{
						level->moving_paddles = true;
						level->obstacle_count = 10;
					}
					else if(game->score == 6)
					{
						level->obstacles = false;
						game->death_pickups.count = 0;
						level->moving_paddles = false;
						level->speed_boost = 1000;
					}
					else if(game->score == 25)
					{
						level->moving_paddles = true;
						level->paddle_speed *= 0.5f;
						level->speed_boost = 0;
					}
					else if(game->score == 50)
					{
						level->moving_paddles = false;
						level->portals = true;
						ball->speed = 200;
						level->speed_boost = 50;
					}
					else if(game->score == 55)
					{
						game->portals.count = 0;
						game->death_pickups.count = 0;
						level->portals = false;
						ball->speed = 800;
						level->speed_boost = 200;
						level->moving_paddles = true;
						level->paddle_size.y *= 0.5f;
					}
					else if(game->score == 100)
					{
						for(int i = 0; i < 32; i++)
						{
							spawn_particles(100, {
								.render_type = 1,
								.speed = 2000,
								.speed_rand = 1,
								.radius = level->ball_radius,
								.radius_rand = 1,
								.duration = 5.0f,
								.duration_rand = 1,
								.angle_rand = 1,
								.pos = v2(boss_paddle->x, c_base_res.y * rng->randf32()),
								.color = v4(0.5f, 0.25f, 0.05f, 1),
							});
							play_delayed_sound(rng->rand_bool() ? game->paddle_sound : game->score_pickup_sound, 0.01f + 0.01f * i);
						}
					}
					if(level->obstacles) { game->spawn_obstacles = true; }
					if(level->portals) { game->spawn_portals = true; }

				}
			}

			// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		update paddles start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if(level->moving_paddles)
			{
				paddle->y += paddle->dir * level->paddle_speed * delta;
				if(paddle->y - level->paddle_size.y / 2 < 0)
				{
					paddle->y = level->paddle_size.y / 2;
					paddle->dir = 1;
				}
				else if(paddle->y + level->paddle_size.y / 2 > c_base_res.y)
				{
					paddle->y = c_base_res.y - level->paddle_size.y / 2;
					paddle->dir = -1;
				}
			}
			// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		update paddles end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

			foreach_raw(pickup_i, pickup, game->score_pickups)
			{
				if(circle_collides_circle(ball->pos, level->ball_radius, pickup.pos, level->ball_radius))
				{
					game->score += 1;
					game->score_pickups.remove_and_swap(pickup_i--);

					s_v4 color = c_score_pickup_color;

					for(int i = 0; i < 100; i++)
					{
						s_particle p = zero;
						p.render_type = 0;
						p.pos = ball_pos_before_collision;
						p.duration = 0.5f * rng->randf32();
						p.render_type = 1;
						p.color = color;
						p.radius = level->ball_radius * 2 * rng->randf32();
						p.dir.x = (float)rng->randf2();
						p.dir.y = (float)rng->randf2();
						p.dir = v2_normalized(p.dir);
						p.speed = 400 * rng->randf32();
						game->particles.add(p);
					}
					g_platform_funcs.play_sound(game->score_pickup_sound);
				}
			}

			foreach_raw(pickup_i, pickup, game->death_pickups)
			{
				if(circle_collides_circle(ball->pos, level->ball_radius, pickup.pos, level->ball_radius))
				{
					game->reset_level = true;
					g_platform_funcs.play_sound(game->fail_sound);
					game->death_pickups.remove_and_swap(pickup_i--);

					s_v4 color = c_death_pickup_color;

					for(int i = 0; i < 100; i++)
					{
						s_particle p = zero;
						p.render_type = 0;
						p.pos = ball_pos_before_collision;
						p.duration = 0.5f * rng->randf32();
						p.render_type = 1;
						p.color = color;
						p.radius = level->ball_radius * 2 * rng->randf32();
						p.dir.x = (float)rng->randf2();
						p.dir.y = (float)rng->randf2();
						p.dir = v2_normalized(p.dir);
						p.speed = 400 * rng->randf32();
						game->particles.add(p);
					}
					g_platform_funcs.play_sound(game->score_pickup_sound);
				}
			}

			foreach_raw(portal_i, portal, game->portals)
			{
				if(circle_collides_circle(ball->pos, level->ball_radius, portal.active, portal.radius))
				{
					ball->pos = portal.target;
					spawn_particles(100, {
						.render_type = 1,
						.speed = 400,
						.speed_rand = 1,
						.radius = portal.radius * 0.5f,
						.radius_rand = 1,
						.duration = 0.5f,
						.duration_rand = 1,
						.angle_rand = 1,
						.pos = portal.active,
						.color = c_portal_color,
						.color_rand = 0.25f,
					});
					g_platform_funcs.play_sound(game->portal_sound);
				}
			}

			if(level->boss && game->score == 99)
			{
				spawn_particles(1, {
					.render_type = 1,
					.speed = 400,
					.speed_rand = 1,
					.radius = level->ball_radius,
					.radius_rand = 1,
					.duration = 0.5f,
					.duration_rand = 1,
					.angle_rand = 1,
					.pos = ball->pos,
					.color = v4(0.5f, 0.25f, 0.05f, 1),
				});
			}

			// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		update ball end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

			if(ball->x > c_base_res.x + level->ball_radius || ball->x < -level->ball_radius)
			{
				game->reset_level = true;
				g_platform_funcs.play_sound(game->fail_sound);
			}

			if(game->score >= level->score_to_beat)
			{
				game->beat_level = true;
			}

			if(game->beat_level)
			{
				game->beat_level = false;
				if(game->current_level == game->levels.count - 1)
				{
					game->state = e_state_victory;
					g_platform_data.any_key_pressed = false;
				}
				else
				{
					game->current_level += 1;
				}
				game->spawn_obstacles = false;
				game->spawn_portals = false;
				game->score = 0;
				ball->speed = game->levels[game->current_level].ball_speed;
				g_platform_funcs.play_sound(game->win_sound);
				game->reset_level = true;
			}

			#ifdef m_debug
			if(game->go_to_previous_level)
			{
				game->go_to_previous_level = false;
				if(game->current_level > 0)
				{
					game->current_level -= 1;
					game->reset_level = true;
				}
			}
			#endif // m_debug

		} break;

		case e_state_victory:
		{
			game->title_color = v3(sinf2(game->total_time), 1, 1);

			if(is_key_pressed(c_key_escape))
			{
				exit(0);
			}
			else if(g_platform_data.any_key_pressed)
			{
				game->state = e_state_game;
				game->reset_game = true;
			}
		} break;

		invalid_default_case;

	}

	// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		update particles start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	foreach(particle_i, particle, game->particles)
	{
		particle->time += delta;
		particle->pos += particle->dir * particle->speed * delta;
		if(particle->time >= particle->duration)
		{
			game->particles.remove_and_swap(particle_i--);
		}
	}
	// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		update particles end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

	// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		delayed sounds start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	foreach(sound_i, sound, game->delayed_sounds)
	{
		sound->time += delta;
		if(sound->time >= sound->delay)
		{
			g_platform_funcs.play_sound(sound->sound);
			game->delayed_sounds.remove_and_swap(sound_i--);
		}
	}
	// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		delayed sounds end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

}

func void render(float dt)
{

	switch(game->state)
	{
		case e_state_main_menu:
		{
			s_v2i res = c_resolutions[game->current_resolution_index];
			draw_text("GNOP", game->title_pos, 15, v4(hsv_to_rgb(game->title_color), 1), e_font_huge, true);
			draw_circle(game->ball.pos, 5, 16, v4(1));
			draw_text("Control the Ball with the Mouse", v2(c_half_res.x, c_base_res.y * 0.6f), 15, v4(1), e_font_big, true);
			draw_text("Press Any Key to Start", v2(c_half_res.x, c_base_res.y * 0.7f), 15, v4(1), e_font_big, true);
			draw_text(
				format_text("Press F2 to change resolution (%ix%i)", res.x, res.y), v2(c_half_res.x, c_base_res.y * 0.8f), 15, v4(1), e_font_medium, true
			);
			draw_text("Made Live at twitch.tv/Tkap1", v2(c_half_res.x, c_base_res.y * 0.9f), 15, make_color(0.5f), e_font_medium, true);
		} break;

		case e_state_game:
		{
			s_level level = game->levels[game->current_level];
			draw_circle(game->ball.pos, 5, level.ball_radius, get_ball_color(game->ball));
			draw_rect(game->paddle.pos, 10, level.paddle_size, v4(1));

			if(level.boss)
			{
				draw_rect(game->boss_paddle.pos, 10, c_boss_paddle_size, v4(1));
			}

			draw_text(format_text("%i/%i", game->score, level.score_to_beat), c_half_res * v2(1, 0.5f), 15, v4(1), e_font_big, true);

			foreach_raw(pickup_i, pickup, game->score_pickups)
			{
				s_v4 color = c_score_pickup_color;

				draw_circle(pickup.pos, 4, pickup.radius, color);
				s_v4 light_color = v4(1);
				draw_circle_p(pickup.pos, 5, pickup.radius * 0.7f, light_color);
			}

			foreach_raw(pickup_i, pickup, game->death_pickups)
			{
				s_v4 color = c_death_pickup_color;

				draw_circle_p(pickup.pos, 4, pickup.radius, color);
				s_v4 light_color = v4(1);
				draw_circle_p(pickup.pos, 5, pickup.radius * 0.7f, light_color);
			}

			foreach_raw(portal_i, portal, game->portals)
			{
				float s = range_lerp(sinf(game->total_time * 4), -1, 1, 0.8f, 1.0f);
				draw_circle(portal.active, 4, portal.radius, c_portal_color, {.do_noise = 1});
				draw_circle(portal.target, 4, portal.radius, c_portal_color, {.do_noise = 1});
				draw_circle(portal.active, 5, portal.radius * 0.6f * s, v4(0.01f, 0.01f, 0.01f, 1));
				draw_circle(portal.target, 5, portal.radius * 0.6f * s, v4(0.01f, 0.01f, 0.01f, 1));
			}

			draw_text(format_text("Level: %i", game->current_level + 1), v2(0,0), 4, make_color(1), e_font_medium, false);
		} break;

		case e_state_victory:
		{
			draw_text("Congratulations! You win!", v2(c_half_res.x, c_base_res.y * 0.3f), 15, v4(hsv_to_rgb(game->title_color), 1), e_font_big, true);
			draw_text("Press Any Key to Replay", v2(c_half_res.x, c_base_res.y * 0.5f), 15, v4(1), e_font_big, true);
			draw_text("Press Escape to Exit", v2(c_half_res.x, c_base_res.y * 0.6f), 15, v4(1), e_font_big, true);
			draw_text("Get the source code at https://github.com/Tkap1/gmtk_jam_gnop", v2(c_half_res.x, c_base_res.y * 0.8f), 15, make_color(0.5f), e_font_medium, true);
			draw_text("Made Live at twitch.tv/Tkap1", v2(c_half_res.x, c_base_res.y * 0.9f), 15, make_color(0.5f), e_font_medium, true);
		} break;

		invalid_default_case;
	}

	foreach_raw(particle_i, particle, game->particles)
	{
		s_v4 color = particle.color;
		float percent = (particle.time / particle.duration);
		float percent_left = 1.0f - percent;
		color.w = powf(percent_left, 0.5f);
		if(particle.render_type == 0)
		{
			draw_circle(particle.pos, 1, particle.radius * range_lerp(percent, 0, 1, 1, 0.2f), color);
		}
		else
		{
			draw_circle_p(particle.pos, 1, particle.radius * range_lerp(percent, 0, 1, 1, 0.2f), color);
		}
	}

	{
		glUseProgram(game->programs[e_shader_default]);
		// glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepth(0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, g_window.width, g_window.height);
		// glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_GREATER);

		{
			int location = glGetUniformLocation(game->programs[e_shader_default], "window_size");
			glUniform2fv(location, 1, &c_base_res.x);
		}
		{
			int location = glGetUniformLocation(game->programs[e_shader_default], "time");
			glUniform1f(location, game->total_time);
		}

		if(transforms.count > 0)
		{
			glBindVertexArray(game->default_vao);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, game->noise.id);
			glUniform1i(1, 1);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			// glBlendFunc(GL_ONE, GL_ONE);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(*transforms.elements) * transforms.count, transforms.elements);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, transforms.count);
			transforms.count = 0;
		}

		if(particles.count > 0)
		{
			glBindVertexArray(game->default_vao);
			glEnable(GL_BLEND);
			// glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glBlendFunc(GL_ONE, GL_ONE);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(*particles.elements) * particles.count, particles.elements);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, particles.count);
			particles.count = 0;
		}

		for(int font_i = 0; font_i < e_font_count; font_i++)
		{
			glBindVertexArray(game->default_vao);
			if(text_arr[font_i].count > 0)
			{
				s_font* font = &game->font_arr[font_i];
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, font->texture.id);
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(*text_arr[font_i].elements) * text_arr[font_i].count, text_arr[font_i].elements);
				glDrawArraysInstanced(GL_TRIANGLES, 0, 6, text_arr[font_i].count);
				text_arr[font_i].count = 0;
			}
		}

	}

	#ifdef m_debug
	hot_reload_shaders();
	#endif // m_debug
}

func b8 check_for_shader_errors(u32 id, char* out_error)
{
	int compile_success;
	char info_log[1024];
	glGetShaderiv(id, GL_COMPILE_STATUS, &compile_success);

	if(!compile_success)
	{
		glGetShaderInfoLog(id, 1024, null, info_log);
		log("Failed to compile shader:\n%s", info_log);

		if(out_error)
		{
			strcpy(out_error, info_log);
		}

		return false;
	}
	return true;
}

func s_font load_font(const char* path, float font_size, s_lin_arena* arena)
{
	s_font font = zero;
	font.size = font_size;

	u8* file_data = (u8*)read_file(path, arena);
	assert(file_data);

	stbtt_fontinfo info = zero;
	stbtt_InitFont(&info, file_data, 0);

	stbtt_GetFontVMetrics(&info, &font.ascent, &font.descent, &font.line_gap);

	font.scale = stbtt_ScaleForPixelHeight(&info, font_size);
	constexpr int max_chars = 128;
	int bitmap_count = 0;
	u8* bitmap_arr[max_chars];
	const int padding = 10;

	int columns = floorfi(4096 / (font_size + padding));
	int rows = ceilfi((max_chars - columns) / (float)columns) + 1;

	int total_width = floorfi(columns * (font_size + padding));
	int total_height = floorfi(rows * (font_size + padding));

	for(int char_i = 0; char_i < max_chars; char_i++)
	{
		s_glyph glyph = zero;
		u8* bitmap = stbtt_GetCodepointBitmap(&info, 0, font.scale, char_i, &glyph.width, &glyph.height, 0, 0);
		stbtt_GetCodepointBox(&info, char_i, &glyph.x0, &glyph.y0, &glyph.x1, &glyph.y1);
		stbtt_GetGlyphHMetrics(&info, char_i, &glyph.advance_width, null);

		font.glyph_arr[char_i] = glyph;
		bitmap_arr[bitmap_count++] = bitmap;
	}

	// @Fixme(tkap, 23/06/2023): Use arena
	u8* gl_bitmap = (u8*)calloc(1, sizeof(u8) * 4 * total_width * total_height);

	for(int char_i = 0; char_i < max_chars; char_i++)
	{
		s_glyph* glyph = &font.glyph_arr[char_i];
		u8* bitmap = bitmap_arr[char_i];
		int column = char_i % columns;
		int row = char_i / columns;
		for(int y = 0; y < glyph->height; y++)
		{
			for(int x = 0; x < glyph->width; x++)
			{
				int current_x = floorfi(column * (font_size + padding));
				int current_y = floorfi(row * (font_size + padding));
				u8 src_pixel = bitmap[x + y * glyph->width];
				u8* dst_pixel = &gl_bitmap[((current_x + x) + (current_y + y) * total_width) * 4];
				dst_pixel[0] = 255;
				dst_pixel[1] = 255;
				dst_pixel[2] = 255;
				dst_pixel[3] = src_pixel;
			}
		}

		glyph->uv_min.x = column / (float)columns;
		glyph->uv_max.x = glyph->uv_min.x + (glyph->width / (float)total_width);

		glyph->uv_min.y = row / (float)rows;

		// @Note(tkap, 17/05/2023): For some reason uv_max.y is off by 1 pixel (checked the texture in renderoc), which causes the text to be slightly miss-positioned
		// in the Y axis. "glyph->height - 1" fixes it.
		glyph->uv_max.y = glyph->uv_min.y + (glyph->height / (float)total_height);

		// @Note(tkap, 17/05/2023): Otherwise the line above makes the text be cut off at the bottom by 1 pixel...
		// glyph->uv_max.y += 0.01f;
	}

	font.texture = load_texture_from_data(gl_bitmap, total_width, total_height, GL_LINEAR);

	for(int bitmap_i = 0; bitmap_i < bitmap_count; bitmap_i++)
	{
		stbtt_FreeBitmap(bitmap_arr[bitmap_i], null);
	}

	free(gl_bitmap);

	return font;
}

func s_texture load_texture_from_data(void* data, int width, int height, u32 filtering)
{
	assert(data);
	u32 id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);

	s_texture texture = zero;
	texture.id = id;
	texture.size = v22i(width, height);
	return texture;
}

func s_texture load_texture_from_file(char* path, u32 filtering)
{
	int width, height, num_channels;
	void* data = stbi_load(path, &width, &height, &num_channels, 4);
	s_texture texture = load_texture_from_data(data, width, height, filtering);
	stbi_image_free(data);
	return texture;
}

func s_v2 get_text_size_with_count(const char* text, e_font font_id, int count)
{
	assert(count >= 0);
	if(count <= 0) { return zero; }
	s_font* font = &game->font_arr[font_id];

	s_v2 size = zero;
	size.y = font->size;

	for(int char_i = 0; char_i < count; char_i++)
	{
		char c = text[char_i];
		s_glyph glyph = font->glyph_arr[c];
		size.x += glyph.advance_width * font->scale;
	}

	return size;
}

func s_v2 get_text_size(const char* text, e_font font_id)
{
	return get_text_size_with_count(text, font_id, (int)strlen(text));
}


#ifdef m_debug
func void hot_reload_shaders(void)
{
	for(int shader_i = 0; shader_i < e_shader_count; shader_i++)
	{
		s_shader_paths* sp = &shader_paths[shader_i];

		WIN32_FIND_DATAA find_data = zero;
		HANDLE handle = FindFirstFileA(sp->fragment_path, &find_data);
		if(handle == INVALID_HANDLE_VALUE) { continue; }

		if(CompareFileTime(&sp->last_write_time, &find_data.ftLastWriteTime) == -1)
		{
			// @Note(tkap, 23/06/2023): This can fail because text editor may be locking the file, so we check if it worked
			u32 new_program = load_shader(sp->vertex_path, sp->fragment_path);
			if(new_program)
			{
				if(game->programs[shader_i])
				{
					glUseProgram(0);
					glDeleteProgram(game->programs[shader_i]);
				}
				game->programs[shader_i] = load_shader(sp->vertex_path, sp->fragment_path);
				sp->last_write_time = find_data.ftLastWriteTime;
			}
		}

		FindClose(handle);
	}

}
#endif // m_debug

func u32 load_shader(const char* vertex_path, const char* fragment_path)
{
	u32 vertex = glCreateShader(GL_VERTEX_SHADER);
	u32 fragment = glCreateShader(GL_FRAGMENT_SHADER);
	const char* header = "#version 430 core\n";
	char* vertex_src = read_file(vertex_path, frame_arena);
	if(!vertex_src || !vertex_src[0]) { return 0; }
	char* fragment_src = read_file(fragment_path, frame_arena);
	if(!fragment_src || !fragment_src[0]) { return 0; }
	const char* vertex_src_arr[] = {header, read_file("src/shader_shared.h", frame_arena), vertex_src};
	const char* fragment_src_arr[] = {header, read_file("src/shader_shared.h", frame_arena), fragment_src};
	glShaderSource(vertex, array_count(vertex_src_arr), (const GLchar * const *)vertex_src_arr, null);
	glShaderSource(fragment, array_count(fragment_src_arr), (const GLchar * const *)fragment_src_arr, null);
	glCompileShader(vertex);
	char buffer[1024] = zero;
	check_for_shader_errors(vertex, buffer);
	glCompileShader(fragment);
	check_for_shader_errors(fragment, buffer);
	u32 program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	return program;
}



func b8 is_key_down(int key)
{
	assert(key < c_max_keys);
	return g_input->keys[key].is_down || g_input->keys[key].count >= 2;
}

func b8 is_key_up(int key)
{
	assert(key < c_max_keys);
	return !g_input->keys[key].is_down;
}

func b8 is_key_pressed(int key)
{
	assert(key < c_max_keys);
	return (g_input->keys[key].is_down && g_input->keys[key].count == 1) || g_input->keys[key].count > 1;
}

func b8 is_key_released(int key)
{
	assert(key < c_max_keys);
	return (!g_input->keys[key].is_down && g_input->keys[key].count == 1) || g_input->keys[key].count > 1;
}

void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	unreferenced(userParam);
	unreferenced(length);
	unreferenced(id);
	unreferenced(type);
	unreferenced(source);
	if(severity >= GL_DEBUG_SEVERITY_HIGH)
	{
		printf("GL ERROR: %s\n", message);
		assert(false);
	}
}

func s_v4 get_ball_color(s_ball ball)
{
	s_v4 a = v4(1, 0.1f, 0.1f, 1.0f);
	s_v4 b = make_color(1);
	float p = 1.0f - ilerp(0.0f, c_ball_hit_time, ball.hit_time);
	return lerp(a, b, p);
}

func s_level make_level()
{
	s_level level = zero;
	level.speed_boost = 100;
	level.ball_radius = 16;
	level.ball_speed = 1000;
	level.paddle_size = v2(16, 128);
	level.score_to_beat = 10;
	level.paddles_give_score = true;
	level.paddle_speed = 250;
	level.obstacle_radius = 16;
	level.portal_radius = 64;
	level.obstacle_count = 15;
	return level;
}

func void init_levels()
{
	game->levels.count = 0;

	// @Note(tkap, 08/07/2023): Basic
	{
		s_level level = make_level();
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.paddle_size.y *= 0.75f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.paddle_size.y *= 0.5f;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Slow then fast
	{
		s_level level = make_level();
		level.ball_speed *= 0.5f;
		level.speed_boost = 3000;
		level.score_to_beat = 2;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.ball_speed *= 0.5f;
		level.speed_boost = 2000;
		level.score_to_beat = 3;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.ball_speed *= 0.5f;
		level.speed_boost = 1200;
		level.score_to_beat = 4;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Collect
	{
		s_level level = make_level();
		level.spawn_pickups = true;
		level.paddles_give_score = false;
		level.score_to_beat = 5;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.spawn_pickups = true;
		level.ball_speed *= 1.25f;
		level.paddles_give_score = false;
		level.score_to_beat = 5;
		level.speed_boost *= 0.5f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.spawn_pickups = true;
		level.ball_speed *= 1.5f;
		level.paddles_give_score = false;
		level.score_to_beat = 5;
		level.speed_boost *= 0.5f;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Moving paddles
	{
		s_level level = make_level();
		level.moving_paddles = true;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.moving_paddles = true;
		level.paddle_speed *= 2;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.moving_paddles = true;
		level.paddle_speed *= 2.5f;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Synced paddles
	{
		s_level level = make_level();
		level.synced_paddles = true;
		level.moving_paddles = true;
		level.score_to_beat = 100;
		level.paddle_size.y *= 2;
		level.ball_speed *= 3;
		level.speed_boost *= 4;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.synced_paddles = true;
		level.moving_paddles = true;
		level.score_to_beat = 100;
		level.paddle_size.y *= 1.5f;
		level.ball_speed *= 3;
		level.speed_boost *= 4;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.synced_paddles = true;
		level.moving_paddles = true;
		level.score_to_beat = 100;
		level.paddle_size.y *= 1.0f;
		level.ball_speed *= 3;
		level.speed_boost *= 4;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Obstacles
	{
		s_level level = make_level();
		level.obstacles = true;
		level.score_to_beat = 8;
		level.ball_speed *= 0.5f;
		level.obstacle_radius = 8;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.obstacles = true;
		level.score_to_beat = 8;
		level.ball_speed *= 0.5f;
		level.obstacle_radius = 12;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.obstacles = true;
		level.score_to_beat = 8;
		level.ball_speed *= 0.5f;
		level.obstacle_radius = 16;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Portals
	{
		s_level level = make_level();
		level.score_to_beat = 5;
		level.portals = true;
		level.ball_speed *= 0.25f;
		level.speed_boost *= 0.25f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.score_to_beat = 5;
		level.portals = true;
		level.portal_radius *= 0.75f;
		level.ball_speed *= 0.33f;
		level.speed_boost *= 0.25f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.score_to_beat = 5;
		level.portals = true;
		level.portal_radius *= 0.5f;
		level.ball_speed *= 0.5f;
		level.speed_boost *= 0.25f;
		level.paddle_size.y *= 2;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Collect + slow then fast
	{
		s_level level = make_level();
		level.ball_speed *= 0.5f;
		level.speed_boost = 600;
		level.spawn_pickups = true;
		level.paddles_give_score = false;
		level.score_to_beat = 3;
		level.paddle_size.y *= 2;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.ball_speed *= 0.5f;
		level.speed_boost = 600;
		level.spawn_pickups = true;
		level.paddles_give_score = false;
		level.score_to_beat = 4;
		level.paddle_size.y *= 2;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.ball_speed *= 0.5f;
		level.speed_boost = 600;
		level.spawn_pickups = true;
		level.paddles_give_score = false;
		level.score_to_beat = 5;
		level.paddle_size.y *= 3;
		game->levels.add(level);
	}

	// @Note(tkap, 08/07/2023): Moving paddles + portal
	{
		s_level level = make_level();
		level.moving_paddles = true;
		level.score_to_beat = 5;
		level.paddle_speed *= 2;
		level.portals = true;
		level.ball_speed *= 0.33f;
		level.speed_boost *= 0.33f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.moving_paddles = true;
		level.score_to_beat = 5;
		level.paddle_speed *= 2;
		level.portals = true;
		level.ball_speed *= 0.33f;
		level.speed_boost *= 0.33f;
		level.paddle_size.y *= 0.75f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.moving_paddles = true;
		level.score_to_beat = 5;
		level.paddle_speed *= 2;
		level.portals = true;
		level.ball_speed *= 0.4f;
		level.speed_boost *= 0.4f;
		level.paddle_size.y *= 0.5f;
		game->levels.add(level);
	}

	{
		s_level level = make_level();
		level.boss = true;
		level.score_to_beat = 100;
		level.paddles_give_score = false;
		level.obstacles = true;
		level.speed_boost = 0;
		game->levels.add(level);
	}

}

func void do_ball_trail(s_ball old_ball, s_ball ball, float radius)
{
	float movement = v2_length(ball.pos - old_ball.pos);
	int count = ceilfi(movement / radius * 8);
	for(int i = 0; i < count; i++)
	{
		s_ball temp = old_ball;
		float p = count == 1 ? 1 : i / (float)(count - 1);
		temp.pos = lerp(old_ball.pos, ball.pos, p);

		s_particle particle = zero;
		particle.pos = temp.pos;
		particle.radius = radius;
		particle.color = get_ball_color(temp);
		particle.duration = 0.15f;
		particle.render_type = 0;
		game->particles.add_checked(particle);
	}
}

func char* handle_plural(int num)
{
	return (num == 1 || num == -1) ? "" : "s";
}

func void spawn_particles(int count, s_particle_spawn_data data)
{
	for(int i = 0; i < count; i++)
	{
		s_particle p = zero;
		p.render_type = data.render_type;
		p.duration = data.duration * (1 - data.duration_rand * game->rng.randf32());
		p.speed = data.speed * (1 - data.speed_rand * game->rng.randf32());
		p.radius = data.radius * (1 - data.radius_rand * game->rng.randf32());
		p.pos = data.pos;

		float foo = (float)game->rng.randf2() * data.angle_rand * tau;
		float angle = data.angle + foo;
		p.dir = v2_from_angle(angle);
		p.color.x = data.color.x * (1 - data.color_rand * game->rng.randf32());
		p.color.y = data.color.y * (1 - data.color_rand * game->rng.randf32());
		p.color.z = data.color.z * (1 - data.color_rand * game->rng.randf32());
		p.color.w = data.color.w;
		game->particles.add_checked(p);
	}
}

func void play_delayed_sound(s_sound sound, float delay)
{
	s_delayed_sound s = zero;
	s.sound = sound;
	s.delay = delay;
	game->delayed_sounds.add_checked(s);
}