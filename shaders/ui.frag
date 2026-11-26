#version 450
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec2 v_texcoord;
layout(location = 0) out vec4 o_color;
layout(binding = 0) uniform sampler2D u_artwork;

void main()
{
    if (v_texcoord.x >= 0.0) {
        o_color = texture(u_artwork, v_texcoord);
    } else {
        o_color = vec4(v_color, 1.0);
    }
}
