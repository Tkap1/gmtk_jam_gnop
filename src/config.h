

global constexpr s_v2 c_base_res = {1366, 768};
global constexpr s_v2 c_half_res = {1366 / 2.0f, 768 / 2.0f};

global constexpr int c_num_channels = 2;
global constexpr int c_sample_rate = 44100;
global constexpr int c_max_concurrent_sounds = 16;

#define c_updates_per_second (1000)
#define c_update_delay (1.0 / c_updates_per_second)

#define c_origin_topleft {1.0f, -1.0f}
#define c_origin_bottomleft {1.0f, 1.0f}
#define c_origin_center {0, 0}

#define c_num_threads (1)
#define c_max_entities (4096)
#define c_entities_per_thread (c_max_entities / c_num_threads)

global constexpr float c_delta = 1.0f / c_updates_per_second;

global constexpr float c_ball_hit_time = 0.5f;

global constexpr s_v2 c_boss_paddle_size = {32.0f, c_base_res.y};
global constexpr s_v4 c_score_pickup_color = make_color(0, 1, 0);
global constexpr s_v4 c_death_pickup_color = make_color(1, 0, 0);
global constexpr s_v4 c_portal_color = make_color(1, 0, 1);
global constexpr float c_ball_max_speed = 30000;

global constexpr int c_base_resolution_index = 5;
global constexpr s_v2i c_resolutions[] = {
	v2i(640, 360),
	v2i(854, 480),
	v2i(960, 540),
	v2i(1024, 576),
	v2i(1280, 720),
	v2i(1366, 768),
	v2i(1600, 900),
	v2i(1920, 1080),
	v2i(2560, 1440),
	v2i(3200, 1800),
	v2i(3840, 2160),
	v2i(5120, 2880),
	v2i(7680, 4320),
};