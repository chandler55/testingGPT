
static void gi_on_gpu(u8* in_bitmap, int w, int h) {
    #define num_cascades 7
    static bool initialized;
    static gpu_bindgroup_t texture_bindgroup[2];
    static gpu_bindgroup_t cascade_uniform_bindgroup[num_cascades];
    static gpu_bindgroup_t render_uniform_bindgroup;
    static gpu_buffer_t vertex_buffer;
    static gpu_buffer_t uniform_buffer;
    static gpu_pipeline_t pipeline;
    static gpu_bindgroup_layout_t uniform_bindgroup_layout;
    static gpu_bindgroup_layout_t texture_bindgroup_layout;
    static lifetime_t texture_lifetime;
    static gpu_texture_t textures[2];
    static gpu_texture_t input_texture;
    lifetime_t* lifetime = g_platform->lifetime;

    f32 d0 = 1.f; // distance between probes in cascade 0
    int r0 = 4; // number of rays in cascade 0
    int n0 = (int)floorf(2*w/d0); // number of probes in cascade 0 per dimension
    int cn = num_cascades;

    typedef struct {
        f32 d0;
        int r0;
        int n0;
        int ci;

        int cn;
        int do_render;
        int add_sky_light;
        int padding;

        v2 resolution;
        v2 padding2;
    } uniform_t;

    if (!initialized) {
        lifetime_t temp_lifetime = {0};
        initialized = true;

        // create bindgroup layouts
        uniform_bindgroup_layout = gpu_bindgroup_layout_make(lifetime, &(gpu_bindgroup_layout_desc_t){
            .name = "gi uniform bgl",
            .entries = {
                {
                    .visibility = gpu_visibility_fragment,
                    .type = gpu_binding_type_buffer,
                    .buffer.type = gpu_buffer_binding_type_uniform,
                },
            },
        });

        texture_bindgroup_layout = gpu_bindgroup_layout_make(lifetime, &(gpu_bindgroup_layout_desc_t){
            .name = "gi texture bgl",
            .entries = {
                {
                    .visibility = gpu_visibility_fragment,
                    .type = gpu_binding_type_sampler,
                },
                {
                    .visibility = gpu_visibility_fragment,
                    .type = gpu_binding_type_sampler,
                },
            },
        });

        // create pipeline
        pipeline = gpu_pipeline_make(lifetime, &(gpu_pipeline_desc_t){
            .name = "gi render shader",
            .code = file_read("shaders/gi.glsl", &temp_lifetime).bytes,
            .bgls = {
                uniform_bindgroup_layout,
                texture_bindgroup_layout,
            },
        });

        // create uniform buffer (we pack all our different uniforms in one buffer), one per cascade and one for rendering
        {
            gpu_uniform_packer_t p = gpu_uniform_packer_begin(sizeof(uniform_t), num_cascades+1, lifetime);
            uniform_buffer = p.handle;
            // set cascade uniforms
            for (int i = 0; i < num_cascades; ++i) {
                *(uniform_t*)p.data = (uniform_t){
                    .d0 = d0,
                    .r0 = r0,
                    .n0 = n0,
                    .ci = i,
                    .cn = num_cascades,
                    .add_sky_light = 1,
                    .resolution = {(f32)w,(f32)h},
                };
                cascade_uniform_bindgroup[i] = gpu_bindgroup_make(lifetime, &(gpu_bindgroup_desc_t){
                    .name = "gi",
                    .layout = uniform_bindgroup_layout,
                    .entries = {gpu_uniform_packer_bindgroup_entry(&p)},
                });
                gpu_uniform_packer_next(&p);
            }

            // set render uniform
            *(uniform_t*)p.data = (uniform_t){
                .d0 = d0,
                .r0 = r0,
                .n0 = n0,
                .ci = 0,
                .cn = num_cascades,
                .do_render = 1,
                .resolution = {(f32)w,(f32)h},
            };
            render_uniform_bindgroup = gpu_bindgroup_make(lifetime, &(gpu_bindgroup_desc_t){
                .name = "gi",
                .layout = uniform_bindgroup_layout,
                .entries = {gpu_uniform_packer_bindgroup_entry(&p)},
            });

            gpu_uniform_packer_end(&p);
        }

        // create textures
        input_texture = gpu_texture_make(w, h, gpu_texture_format_rgb8, filter_type_nearest, false, lifetime);
        gpu_texture_set_border(input_texture, (color_t){1,1,1,1});
        textures[0] = gpu_texture_make(r0*n0, n0, gpu_texture_format_rgba8, filter_type_nearest, false, lifetime);
        textures[1] = gpu_texture_make(r0*n0, n0, gpu_texture_format_rgba8, filter_type_nearest, false, lifetime);

        texture_bindgroup[0] = gpu_bindgroup_make(lifetime, &(gpu_bindgroup_desc_t){
            .name = "gi",
            .layout = texture_bindgroup_layout,
            .entries = {
                {.sampler = {input_texture}},
                {.sampler = {textures[0]}},
            },
        });

        texture_bindgroup[1] = gpu_bindgroup_make(lifetime, &(gpu_bindgroup_desc_t){
            .name = "gi",
            .layout = texture_bindgroup_layout,
            .entries = {
                {.sampler = {input_texture}},
                {.sampler = {textures[1]}},
            },
        });

        lifetime_destroy(&temp_lifetime);
    }

    // update input texture
    gpu_texture_set_data(input_texture, in_bitmap);

    // clear texture for pingponging
    gpu_texture_clear(textures[(cn-1)%2], (color_t){0});

    // build cascades
    for (int i = cn-1; i >= 0; --i) {
        drawcall_render(&(drawcall_t){
            .pipeline = pipeline,
            .last_vertex = 6,
            .bindgroups = {cascade_uniform_bindgroup[i], texture_bindgroup[i%2]},
            .outputs = {textures[(i+1)%2]},
        });
    }

    // render
    drawcall_render(&(drawcall_t){
        .pipeline = pipeline,
        .last_vertex = 6,
        .bindgroups = {render_uniform_bindgroup, texture_bindgroup[cn%2]},
    });

    #undef num_cascades
}
