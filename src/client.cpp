
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "opengl32.lib")

#include "pch_client.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert assert
#include "external/stb_truetype.h"

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_assert assert
#include "external/stb_image.h"
#pragma warning(pop)



#include "config.h"
#include "memory.h"
#include "platform_shared.h"


struct s_texture
{
	u32 id;
	s_v2 size;
	s_v2 sub_size;
};


#include "rng.h"
#include "client.h"
#include "shader_shared.h"
#include "str_builder.h"
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


#define X(type, name) type name = null;
m_gl_funcs
#undef X

#include "draw.cpp"
#include "memory.cpp"
#include "file.cpp"
#include "str_builder.cpp"
#include "audio.cpp"

extern "C"
{
__declspec(dllexport)
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

		glDebugMessageCallback(gl_debug_callback, null);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		game->rng.seed = (u32)__rdtsc();
		game->reset_game = true;

		game->state = e_state_game;

		platform_funcs.set_swap_interval(1);

		game->jump_sound = load_wav("assets/jump.wav", frame_arena);
		game->jump2_sound = load_wav("assets/jump2.wav", frame_arena);
		game->big_dog_sound = load_wav("assets/big_dog.wav", frame_arena);
		game->win_sound = load_wav("assets/win.wav", frame_arena);

		game->font_arr[e_font_small] = load_font("assets/consola.ttf", 24, frame_arena);
		game->font_arr[e_font_medium] = load_font("assets/consola.ttf", 36, frame_arena);
		game->font_arr[e_font_big] = load_font("assets/consola.ttf", 72, frame_arena);

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
	}

	if(platform_data.recompiled)
	{
		#define X(type, name) name = (type)platform_funcs.load_gl_func(#name);
		m_gl_funcs
		#undef X
	}


	g_window.width = platform_data.window_width;
	g_window.height = platform_data.window_height;
	g_window.size = v2ii(g_window.width, g_window.height);
	g_window.center = v2_mul(g_window.size, 0.5f);

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
}

func void update()
{
	game->update_count += 1;
	g_platform_funcs.show_cursor(false);
	switch(game->state)
	{
		case e_state_game:
		{

			s_ball* ball = &game->ball;
			s_paddle* paddle = &game->paddle;
			s_rng* rng = &game->rng;

			if(is_key_pressed(c_key_f))
			{
				game->reset_game = true;
			}

			// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		reset game start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if(game->reset_game)
			{
				game->reset_game = false;
				game->pickups.count = 0;
				game->score = 0;
				ball->speed = 800;
				ball->x = c_half_res.x;
				if(rng->rand_bool())
				{
					paddle->x = c_base_res.x - c_paddle_size.x / 2;
					ball->dir.x = 1;
				}
				else
				{
					paddle->x = c_paddle_size.x / 2;
					ball->dir.x = -1;
				}
				paddle->y = c_half_res.y;
			}
			// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		reset game end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

			// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv		update ball start		vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			// if(game->update_count % 10 == 0)
			{
				s_particle particle = zero;
				particle.pos = ball->pos;
				particle.radius = c_ball_radius;
				particle.color = get_ball_color(*ball);
				particle.duration = 0.25f;
				particle.render_type = 0;
				game->particles.add_checked(particle);
			}

			ball->x += ball->dir.x * delta * ball->speed;
			ball->hit_time = at_least(0.0f, ball->hit_time - delta);
			s_v2 ball_pos_before_collision = ball->pos;
			if(rect_collides_circle(paddle->pos, c_paddle_size, ball->pos, c_ball_radius))
			{
				ball->x = paddle->x - (c_paddle_size.x / 2.0f * ball->dir.x) - (c_ball_radius * ball->dir.x);
				paddle->x += c_base_res.x * -ball->dir.x;
				paddle->x += c_paddle_size.x * ball->dir.x;
				paddle->y = rng->randf_range(c_paddle_size.y / 2, c_base_res.y - c_paddle_size.y / 2);
				ball->dir.x = -ball->dir.x;
				ball->speed += 100;
				g_platform_funcs.play_sound(game->jump_sound);
				game->score += 1;
				game->max_score = at_least(game->max_score, game->score);
				ball->hit_time = c_ball_hit_time;

				for(int i = 0; i < 100; i++)
				{
					s_particle p = zero;
					p.render_type = 0;
					p.pos = ball_pos_before_collision;
					p.duration = 0.5f * rng->randf32();
					p.render_type = 1;
					p.color = v4(0.5f, 0.25f, 0.05f, 1);
					p.radius = c_ball_radius * rng->randf32();
					p.dir.x = (float)rng->randf2();
					p.dir.y = (float)rng->randf2();
					p.dir = v2_normalized(p.dir);
					p.speed = 100 * rng->randf32();
					game->particles.add_checked(p);
				}

				if(rng->chance100(25))
				{
					s_pickup pickup = zero;
					float radius = c_half_res.x * (c_base_res.y / c_base_res.x);
					float angle = rng->randf32() * tau;
					pickup.x = cosf(angle) * radius * sqrtf(rng->randf32()) + c_half_res.x;
					pickup.y = sinf(angle) * radius * sqrtf(rng->randf32()) + c_half_res.y;
					game->pickups.add_checked(pickup);
				}

			}

			foreach_raw(pickup_i, pickup, game->pickups)
			{
				if(circle_collides_circle(ball->pos, c_ball_radius, pickup.pos, c_ball_radius))
				{
					ball->speed -= 100;
					game->pickups.remove_and_swap(pickup_i--);

					for(int i = 0; i < 100; i++)
					{
						s_particle p = zero;
						p.render_type = 0;
						p.pos = ball_pos_before_collision;
						p.duration = 0.5f * rng->randf32();
						p.render_type = 1;
						p.color = v4(0.1f, 0.7f, 0.1f, 1);
						p.radius = c_ball_radius * 2 * rng->randf32();
						p.dir.x = (float)rng->randf2();
						p.dir.y = (float)rng->randf2();
						p.dir = v2_normalized(p.dir);
						p.speed = 400 * rng->randf32();
						game->particles.add(p);
						g_platform_funcs.play_sound(game->jump2_sound);
					}
				}
			}

			ball->y = g_platform_data.mouse.y;
			// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^		update ball end		^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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
		} break;
	}
}

func void render(float dt)
{

	switch(game->state)
	{
		case e_state_game:
		{
			draw_circle(game->ball.pos, 5, c_ball_radius, get_ball_color(game->ball));
			draw_rect(game->paddle.pos, 10, c_paddle_size, v4(1));

			draw_text(format_text("%i", game->score), c_half_res * v2(1, 0.5f), 15, v4(1), e_font_big, true);

			foreach_raw(pickup_i, pickup, game->pickups)
			{
				draw_circle(pickup.pos, 4, c_ball_radius, v4(0, 1, 0, 1));
				draw_circle(pickup.pos, 4, c_ball_radius * 2, v4(0, 1, 0, 0.25f));
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

		} break;
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
			glUniform2fv(location, 1, &g_window.size.x);
		}
		{
			int location = glGetUniformLocation(game->programs[e_shader_default], "time");
			glUniform1f(location, game->total_time);
		}

		if(transforms.count > 0)
		{
			glBindVertexArray(game->default_vao);
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
	#define max_chars 128
	int bitmap_count = 0;
	u8* bitmap_arr[max_chars];
	const int padding = 10;
	int total_width = padding;
	int total_height = 0;
	for(int char_i = 0; char_i < max_chars; char_i++)
	{
		s_glyph glyph = zero;
		u8* bitmap = stbtt_GetCodepointBitmap(&info, 0, font.scale, char_i, &glyph.width, &glyph.height, 0, 0);
		stbtt_GetCodepointBox(&info, char_i, &glyph.x0, &glyph.y0, &glyph.x1, &glyph.y1);
		stbtt_GetGlyphHMetrics(&info, char_i, &glyph.advance_width, null);

		total_width += glyph.width + padding;
		total_height = max(glyph.height + padding * 2, total_height);

		font.glyph_arr[char_i] = glyph;
		bitmap_arr[bitmap_count++] = bitmap;
	}

	// @Fixme(tkap, 23/06/2023): Use arena
	u8* gl_bitmap = (u8*)calloc(1, sizeof(u8) * 4 * total_width * total_height);

	int current_x = padding;
	for(int char_i = 0; char_i < max_chars; char_i++)
	{
		s_glyph* glyph = &font.glyph_arr[char_i];
		u8* bitmap = bitmap_arr[char_i];
		for(int y = 0; y < glyph->height; y++)
		{
			for(int x = 0; x < glyph->width; x++)
			{
				u8 src_pixel = bitmap[x + y * glyph->width];
				u8* dst_pixel = &gl_bitmap[((current_x + x) + (padding + y) * total_width) * 4];
				dst_pixel[0] = src_pixel;
				dst_pixel[1] = src_pixel;
				dst_pixel[2] = src_pixel;
				dst_pixel[3] = src_pixel;
			}
		}

		glyph->uv_min.x = current_x / (float)total_width;
		glyph->uv_max.x = (current_x + glyph->width) / (float)total_width;

		glyph->uv_min.y = padding / (float)total_height;

		// @Note(tkap, 17/05/2023): For some reason uv_max.y is off by 1 pixel (checked the texture in renderoc), which causes the text to be slightly miss-positioned
		// in the Y axis. "glyph->height - 1" fixes it.
		glyph->uv_max.y = (padding + glyph->height - 1) / (float)total_height;

		// @Note(tkap, 17/05/2023): Otherwise the line above makes the text be cut off at the bottom by 1 pixel...
		glyph->uv_max.y += 0.01f;

		current_x += glyph->width + padding;
	}

	font.texture = load_texture_from_data(gl_bitmap, total_width, total_height, GL_LINEAR);

	for(int bitmap_i = 0; bitmap_i < bitmap_count; bitmap_i++)
	{
		stbtt_FreeBitmap(bitmap_arr[bitmap_i], null);
	}

	free(gl_bitmap);

	#undef max_chars

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
#ifdef _WIN32
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
#else
func void hot_reload_shaders(void)
{
	for(int shader_i = 0; shader_i < e_shader_count; shader_i++)
	{
		s_shader_paths* sp = &shader_paths[shader_i];
		struct stat s;
		int rc = stat(sp->fragment_path, &s);
		if(rc < 0) { continue; }
		if(s.st_mtime > sp->last_write_time)
		{
			u32 new_program = load_shader(sp->vertex_path, sp->fragment_path);
			if(new_program)
			{
				if(game->programs[shader_i])
				{
					glUseProgram(0);
					glDeleteProgram(game->programs[shader_i]);
				}
				game->programs[shader_i] = load_shader(sp->vertex_path, sp->fragment_path);
				sp->last_write_time = s.st_mtime;
			}
		}
	}
}
#endif // _WIN32
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