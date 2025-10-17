#version 450 core

// NOTE: Attributes must match the declaration of VkVertexInputAttribute array
layout (location = 0) in vec3 v_position;
layout (location = 1) in vec3 v_color;
// layout (location = 1) in type name;
layout(location = 0) out vec3 position;
layout(location = 1) out vec3 dcolor;

// NOTE: Must match declaration order of a C struct
layout (push_constant, std430) uniform ShaderConstants {
	mat4 projection;
	mat4 transform;
	vec3 color;
};

void main() {
	vec4 point = vec4(v_position, 1.0f);
	vec4 transformed = transform * point;
	vec4 projected = projection * transformed;

	// NOTE: Write our projected point out
	gl_Position = projected;
	position = v_position;
	dcolor = v_color;
}