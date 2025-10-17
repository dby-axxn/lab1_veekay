#include <cstdint>
#include <climits>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

#include <veekay/veekay.hpp>

#include <imgui.h>
#include <vulkan/vulkan_core.h>

namespace {

constexpr float camera_fov = 70.0f;
constexpr float camera_near_plane = 0.01f;
constexpr float camera_far_plane = 100.0f;

struct Matrix
{
	float m[4][4];
};

struct Vector
{
	float x, y, z;
};

struct Vertex
{
	Vector position;
	// NOTE: You can add more attributes
	Vector normal;
};

// NOTE: These variable will be available to shaders through push constant uniform
struct ShaderConstants
{
	Matrix projection;
	Matrix transform;
	Vector color;
};

struct VulkanBuffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
};

VkShaderModule vertex_shader_module;
VkShaderModule fragment_shader_module;
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// NOTE: Declare buffers and other variables here
VulkanBuffer vertex_buffer;
VulkanBuffer index_buffer;

Vector model_position = {0.0f, 0.0f, 5.0f};
Vector model_rotation;
Vector model_color = {0.5f, 1.0f, 0.7f};
bool model_spin = true;
Vector spin_velocity = {1.0f, 1.0f, 0.0f};

Matrix identity() {
	Matrix result{};

	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	return result;
}
// матрица проекции из 3D в 2D
// по умолчанию такая была
Matrix projection(const float aspect_ratio) {
	Matrix result{};

	constexpr float radians = camera_fov * M_PI / 180.0f;
	const float cot = 1.0f / tanf(radians / 2.0f);

	result.m[0][0] = cot / aspect_ratio;
	result.m[1][1] = cot;
	result.m[2][3] = 1.0f;

	result.m[2][2] = camera_far_plane / (camera_far_plane - camera_near_plane);
	result.m[3][2] = (-camera_near_plane * camera_far_plane) / (camera_far_plane - camera_near_plane);

	/*
	{
		{cot / aspect_ratio,   0,    0,																						0},
		{0,                    cot,  0,																						0},
	 	{0,                    0,    camera_far_plane / (camera_far_plane - camera_near_plane),								1.0f},
	 	{0,                    0,    (-camera_near_plane * camera_far_plane) / (camera_far_plane - camera_near_plane),      0}
	} */

	return result;
}

Matrix translation(const Vector vector) {
	Matrix result = identity();

	result.m[3][0] = vector.x;
	result.m[3][1] = vector.y;
	result.m[3][2] = vector.z;
	/*
	 *	{1, 0, 0, 0},
	 *	{0, 1, 0, 0},
	 *	{0, 0, 1, 0},
	 *	{x, y, z, 1}
	 */

	return result;
}

Matrix multiply(const Matrix &a, const Matrix &b) {
	Matrix result{};

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			for (int k = 0; k < 4; k++) {
				result.m[j][i] += a.m[j][k] * b.m[k][i];
			}
		}
	}

	return result;
}
/* поворот матриц
 * сначала поворот вокруг одной оси, потом вокруг другой
 * перемножение матриц поворота - матрица поворота вокруг 2х осей
 */
Matrix rotation(Vector angle) {
	Matrix result{};

	const Vector sin = {sinf(angle.x), sinf(angle.y), sinf(angle.z)};
	const Vector cos = {cosf(angle.x), cosf(angle.y), cosf(angle.z)};

	const Matrix Mx = {
		{
			{1.1f, 0.0f, 0.0f, 0.0f},
			{0.0f, cos.x, -sin.x, 0.0f},
			{0.0f, sin.x, cos.x, 0.0f},
			{0.0f, 0.0f, 0.0f, 1.0f}
		}
	};

	const Matrix My = {
		{
			{cos.y, 0, sin.y, 0},
			{0, 1, 0, 0},
			{-sin.y, 0, cos.y, 0},
			{0, 0, 0, 1}
		}
	};

	const Matrix Mz = {
		{
			{cos.z, -sin.z, 0, 0},
			{sin.z, cos.z, 0, 0},
			{0, 0, 1, 0},
			{0, 0, 0, 1}
		}
	};

	result = multiply(multiply(Mx, My), Mz);

	return result;
}

// NOTE: Loads shader byte code from file
// NOTE: Your shaders are compiled via CMake with this code too, look it up
VkShaderModule loadShaderModule(const char *path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	const size_t size = file.tellg();
	std::vector<uint32_t> buffer(size / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), size);
	file.close();

	const VkShaderModuleCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = buffer.data(),
	};

	VkShaderModule result;
	if (vkCreateShaderModule(veekay::app.vk_device,
							 &info,
							 nullptr,
							 &result) != VK_SUCCESS) {
		return nullptr;
	}

	return result;
}

VulkanBuffer createBuffer(size_t size, const void *data, const VkBufferUsageFlags usage) {
	const VkDevice &device = veekay::app.vk_device;
	const VkPhysicalDevice &physical_device = veekay::app.vk_physical_device;

	VulkanBuffer result{};

	{
		// NOTE: We create a buffer of specific usage with specified size
		const VkBufferCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		if (vkCreateBuffer(device, &info, nullptr, &result.buffer) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan buffer\n";
			return {};
		}
	}

	// NOTE: Creating a buffer does not allocate memory,
	//       only a buffer **object** was created.
	//       So, we allocate memory for the buffer

	{
		// NOTE: Ask buffer about its memory requirements
		VkMemoryRequirements requirements;
		vkGetBufferMemoryRequirements(device, result.buffer, &requirements);

		// NOTE: Ask GPU about types of memory it supports
		VkPhysicalDeviceMemoryProperties properties;
		vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

		// NOTE: We want type of memory which is visible to both CPU and GPU
		// NOTE: HOST is CPU, DEVICE is GPU; we are interested in "CPU" visible memory
		// NOTE: COHERENT means that CPU cache will be invalidated upon mapping memory region
		const VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		// NOTE: Linear search through types of memory until
		//       one type matches the requirements, that's the index of memory type
		uint32_t index = UINT_MAX;
		for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
			const VkMemoryType &type = properties.memoryTypes[i];

			if ((requirements.memoryTypeBits & (1 << i)) &&
				(type.propertyFlags & flags) == flags) {
				index = i;
				break;
			}
		}

		if (index == UINT_MAX) {
			std::cerr << "Failed to find required memory type to allocate Vulkan buffer\n";
			return {};
		}

		// NOTE: Allocate required memory amount in appropriate memory type
		VkMemoryAllocateInfo info{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = requirements.size,
			.memoryTypeIndex = index,
		};

		if (vkAllocateMemory(device, &info, nullptr, &result.memory) != VK_SUCCESS) {
			std::cerr << "Failed to allocate Vulkan buffer memory\n";
			return {};
		}

		// NOTE: Link allocated memory with a buffer
		if (vkBindBufferMemory(device, result.buffer, result.memory, 0) != VK_SUCCESS) {
			std::cerr << "Failed to bind Vulkan  buffer memory\n";
			return {};
		}

		// NOTE: Get pointer to allocated memory
		void *device_data;
		vkMapMemory(device, result.memory, 0, requirements.size, 0, &device_data);

		memcpy(device_data, data, size);

		vkUnmapMemory(device, result.memory);
	}

	return result;
}

void destroyBuffer(const VulkanBuffer &buffer) {
	const VkDevice &device = veekay::app.vk_device;

	vkFreeMemory(device, buffer.memory, nullptr);
	vkDestroyBuffer(device, buffer.buffer, nullptr);
}

/* инициализация движка
 * передает ей шейдеры
 * параметры отображения - треугольники
 * если вершины указаны по часовой стрелке, то треугольник отображается
 */

void initialize() {
	// VkPhysicalDevice &physical_device = veekay::app.vk_physical_device;

	{
		VkDevice &device = veekay::app.vk_device;
		// NOTE: Build graphics pipeline
		vertex_shader_module = loadShaderModule("./shaders/shader.vert.spv");
		if (!vertex_shader_module) {
			std::cerr << "Failed to load Vulkan vertex shader from file\n";
			veekay::app.running = false;
			return;
		}

		fragment_shader_module = loadShaderModule("./shaders/shader.frag.spv");
		if (!fragment_shader_module) {
			std::cerr << "Failed to load Vulkan fragment shader from file\n";
			veekay::app.running = false;
			return;
		}

		VkPipelineShaderStageCreateInfo stage_infos[2];

		// NOTE: Vertex shader stage
		stage_infos[0] = VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader_module,
			.pName = "main",
		};

		// NOTE: Fragment shader stage
		stage_infos[1] = VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader_module,
			.pName = "main",
		};

		// NOTE: How many bytes does a vertex take?
		VkVertexInputBindingDescription buffer_binding{
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		// NOTE: Declare vertex attributes
		// атрибуты, которые передаются в shader
		VkVertexInputAttributeDescription attributes[] = {
			{
				.location = 0, // NOTE: First attribute
				.binding = 0, // NOTE: First vertex buffer
				.format = VK_FORMAT_R32G32B32_SFLOAT, // NOTE: 3-component vector of floats
				.offset = offsetof(Vertex, position), // NOTE: Offset of "position" field in a Vertex struct
			},
			// NOTE: If you want more attributes per vertex, declare them here
#if 1
			{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, normal)
			}
#endif
		};

		// NOTE: Bring
		VkPipelineVertexInputStateCreateInfo input_state_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &buffer_binding,
			.vertexAttributeDescriptionCount = std::size(attributes),
			.pVertexAttributeDescriptions = attributes,
		};

		// NOTE: Every three vertices make up a triangle,
		//       so our vertex buffer contains a "list of triangles"
		VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		// NOTE: Declare clockwise triangle order as front-facing
		//       Discard triangles that are facing away
		//       Fill triangles, don't draw lines instead
		VkPipelineRasterizationStateCreateInfo raster_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		// NOTE: Use 1 sample per pixel
		VkPipelineMultisampleStateCreateInfo sample_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = false,
			.minSampleShading = 1.0f,
		};

		VkViewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(veekay::app.window_width),
			.height = static_cast<float>(veekay::app.window_height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		VkRect2D scissor{
			.offset = {0, 0},
			.extent = {veekay::app.window_width, veekay::app.window_height},
		};

		// NOTE: Let rasterizer draw on the entire window
		VkPipelineViewportStateCreateInfo viewport_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

			.viewportCount = 1,
			.pViewports = &viewport,

			.scissorCount = 1,
			.pScissors = &scissor,
		};

		// NOTE: Let rasterizer perform depth-testing and overwrite depth values on condition pass
		VkPipelineDepthStencilStateCreateInfo depth_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = true,
			.depthWriteEnable = true,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		};

		// NOTE: Let fragment shader write all the color channels
		VkPipelineColorBlendAttachmentState attachment_info{
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT,
		};

		// NOTE: Let rasterizer just copy resulting pixels onto a buffer, don't blend
		VkPipelineColorBlendStateCreateInfo blend_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

			.logicOpEnable = false,
			.logicOp = VK_LOGIC_OP_COPY,

			.attachmentCount = 1,
			.pAttachments = &attachment_info
		};

		// NOTE: Declare constant memory region visible to vertex and fragment shaders
		VkPushConstantRange push_constants{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
			VK_SHADER_STAGE_FRAGMENT_BIT,
			.size = sizeof(ShaderConstants),
		};

		// NOTE: Declare external data sources, only push constants this time
		VkPipelineLayoutCreateInfo layout_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &push_constants,
		};

		// NOTE: Create pipeline layout
		if (vkCreatePipelineLayout(device,
								   &layout_info,
								   nullptr,
								   &pipeline_layout) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan pipeline layout\n";
			veekay::app.running = false;
			return;
		}

		VkGraphicsPipelineCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = 2,
			.pStages = stage_infos,
			.pVertexInputState = &input_state_info,
			.pInputAssemblyState = &assembly_state_info,
			.pViewportState = &viewport_info,
			.pRasterizationState = &raster_info,
			.pMultisampleState = &sample_info,
			.pDepthStencilState = &depth_info,
			.pColorBlendState = &blend_info,
			.layout = pipeline_layout,
			.renderPass = veekay::app.vk_render_pass,
		};

		// NOTE: Create graphics pipeline
		if (vkCreateGraphicsPipelines(device,
									  nullptr,
									  1,
									  &info,
									  nullptr,
									  &pipeline) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan pipeline\n";
			veekay::app.running = false;
			return;
		}
	}

	// TODO: You define model vertices and create buffers here
	// TODO: Index buffer has to be created here too
	// NOTE: Look for createBuffer function

	// (v0)------(v1)
	//  |  \       |
	//  |   `--,   |
	//  |       \  |
	// (v3)------(v2)
	Vertex vertices[] = {
		{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}}, // bottom 0
		{{0.0f, -1.0f, 0.0f}, {0.7f, 0.0f, 1.0f}}, // up 1
		{{1.0f, 0.0f, 0.0f}, {0.2f, 0.0f, 1.0f}}, // right 2
		{{-1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 1.0f}}, // left 3
		{{0.0f, 0.0f, 1.0f}, {0.0f, 0.3f, 1.0f}}, // front 4
		{{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, -1.0f}}, // back 5
	}; // позиция              "цвет вершины"

	uint32_t indices[] = {
		0, 2, 4,
		0, 5, 2,
		0, 4, 3,
		0, 3, 5,
		1, 4, 2,
		1, 2, 5,
		1, 3, 4,
		1, 5, 3,
	};

	vertex_buffer = createBuffer(sizeof(vertices),
								 vertices,
								 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

	index_buffer = createBuffer(sizeof(indices),
								indices,
								VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void shutdown() {
	const VkDevice &device = veekay::app.vk_device;

	// NOTE: Destroy resources here, do not cause leaks in your program!
	destroyBuffer(index_buffer);
	destroyBuffer(vertex_buffer);

	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyShaderModule(device, fragment_shader_module, nullptr);
	vkDestroyShaderModule(device, vertex_shader_module, nullptr);
}

// обновляет данные за единицу времени
void update(const double time) {
	// параметры сверху (UI) - user interface
	ImGui::Begin("Controls:");
	ImGui::InputFloat3("Translation", reinterpret_cast<float*>(&model_position));
	ImGui::InputFloat3("Velocity", reinterpret_cast<float*>(&spin_velocity));
	// ImGui::SliderFloat("Rotation", , 0.0f, 2.0f * M_PI);
	ImGui::Checkbox("Spin?", &model_spin);
	// TODO: Your GUI stuff here
	ImGui::End();

	// NOTE: Animation code and other runtime variable updates go here
	if (model_spin) {
		model_rotation = {
			spin_velocity.x * static_cast<float>(time), spin_velocity.y * static_cast<float>(time),
			spin_velocity.z * static_cast<float>(time)
		};

		model_position = {2 * cosf(static_cast<float>(time)), sinf(static_cast<float>(time)), 5};
	}

	model_rotation = {
		fmodf(model_rotation.x, 2.0f * M_PI), fmodf(model_rotation.y, 2.0f * M_PI),
		fmodf(model_rotation.z, 2.0f * M_PI)
	};
}

/* отображение (рендеринг) фигуры
 * сначала все заливается одним тоном,
 * потом поверх рисуется фигура
 */
void render(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
	vkResetCommandBuffer(cmd, 0);

	{
		// NOTE: Start recording rendering commands
		constexpr VkCommandBufferBeginInfo info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		vkBeginCommandBuffer(cmd, &info);
	}

	{
		// NOTE: Use current swapchain framebuffer and clear it
		VkClearValue clear_color{.color = {{0.1f, 0.1f, 0.1f, 1.0f}}};
		VkClearValue clear_depth{.depthStencil = {1.0f, 0}};

		VkClearValue clear_values[] = {clear_color, clear_depth};

		const VkRenderPassBeginInfo info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = veekay::app.vk_render_pass,
			.framebuffer = framebuffer,
			.renderArea = {
				.extent = {
					veekay::app.window_width,
					veekay::app.window_height
				},
			},
			.clearValueCount = 2,
			.pClearValues = clear_values,
		};

		vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	// TODO: Vulkan rendering code here
	// NOTE: ShaderConstant updates, vkCmdXXX expected to be here
	{
		// NOTE: Use our new shiny graphics pipeline
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);


		// NOTE: Use our quad vertex buffer
		constexpr VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &offset);

		// NOTE: Use our quad index buffer
		vkCmdBindIndexBuffer(cmd, index_buffer.buffer, offset, VK_INDEX_TYPE_UINT32);

		// NOTE: Variables like model_XXX were declared globally
		const ShaderConstants constants{
			.projection = projection(
				static_cast<float>(veekay::app.window_width) / static_cast<float>(veekay::app.window_height)),

			.transform = multiply(rotation(model_rotation),
								  translation(model_position)),

			.color = model_color,
		};

		// NOTE: Update constant memory with new shader constants
		vkCmdPushConstants(cmd,
						   pipeline_layout,
						   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						   0,
						   sizeof(ShaderConstants),
						   &constants);

		// NOTE: Draw 6 indices (3 vertices * 8 triangles), 1 group, no offsets
		vkCmdDrawIndexed(cmd, 24, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace

int main() {
	return veekay::run({
		.init = initialize,
		.shutdown = shutdown,
		.update = update,
		.render = render,
	});
}
