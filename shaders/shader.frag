#version 450

// NOTE: out attributes of vertex shader must be in's
// layout (location = 0) in type name;

// NOTE: Pixel color
layout (location = 0) out vec4 final_color;
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 v_color;

// NOTE: Must match declaration order of a C struct
layout (push_constant, std430) uniform ShaderConstants {
    mat4 projection;
    mat4 transform;
    vec3 color;
};

void main() {

//    if (-abs(position.x) - abs(position.z) + 0.98 <= 0 || -abs(position.x) - abs(position.y) + 0.98 <= 0 || -abs(position.y) - abs(position.z) + 0.98 <= 0) {
//        final_color = vec4(vec3(0.0f), 1.0f);
//    }
//    else {
//        final_color = vec4(color, 1.0f);
//    }
    final_color = vec4(v_color * color, 1.0f);
}