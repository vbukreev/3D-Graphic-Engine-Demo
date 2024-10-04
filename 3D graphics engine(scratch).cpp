#include <iostream>
#include <vector>
#include <glew.h>
#include <glfw3.h>
#include <CL/cl2.hpp>

// Vertex Shader Source
const char* vertexShaderSource = R"(
    #version 330 core
    layout(location = 0) in vec3 position;
    void main() {
        gl_Position = vec4(position, 1.0);
    }
)";

// Fragment Shader Source
const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 color;
    void main() {
        color = vec4(1.0, 0.0, 0.0, 1.0); // Red color
    }
)";

// OpenCL Kernel Source
const char* clKernelSource = R"(
    __kernel void simpleKernel(__global float4* positions) {
        int id = get_global_id(0);
        positions[id].x += 0.01f; // Simple operation: move vertices along x-axis
    }
)";

GLuint compileShader(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char infoLog[512];
		glGetShaderInfoLog(shader, 512, nullptr, infoLog);
		std::cerr << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	return shader;
}

GLuint createShaderProgram() {
	GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
	GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);

	GLint success;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
	if (!success) {
		char infoLog[512];
		glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
		std::cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
	}

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return shaderProgram;
}

int main() {
	// Initialize GLFW
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW" << std::endl;
		return -1;
	}

	// Create OpenGL window and context
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(800, 600, "OpenCL-OpenGL Interop", nullptr, nullptr);
	if (!window) {
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);

	// Initialize GLEW
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		std::cerr << "Failed to initialize GLEW" << std::endl;
		return -1;
	}

	// Define vertices for a triangle
	std::vector<float> vertices = {
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.0f,  0.5f, 0.0f
	};

	GLuint VAO, VBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), vertices.data(), GL_DYNAMIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Create Shader Program
	GLuint shaderProgram = createShaderProgram();

	// Initialize OpenCL context and queue
	cl::Platform platform;
	cl::Device device;
	std::vector<cl::Platform> platforms;

	cl::Platform::get(&platforms);
	if (platforms.empty()) {
		std::cerr << "No OpenCL platforms found." << std::endl;
		return -1;
	}

	platform = platforms.front();
	std::vector<cl::Device> devices;

	platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
	if (devices.empty()) {
		std::cerr << "No GPU devices found." << std::endl;
		return -1;
	}

	device = devices.front();

	cl_context_properties properties[] =
	{
		CL_GL_CONTEXT_KHR,
		reinterpret_cast<cl_context_properties>(glfwGetWGLContext(window)),
		CL_WGL_HDC_KHR,
		reinterpret_cast<cl_context_properties>(wglGetCurrentDC()),
		CL_CONTEXT_PLATFORM,
		reinterpret_cast<cl_context_properties>(platform()),
		0
	};

	cl::Context context(device);

	cl::CommandQueue queue(context, device);

	cl::Program program(context,
		clKernelSource,
		true);

	cl_int err;

	cl::Kernel kernel(program,
		"simpleKernel",
		&err);

	if (err != CL_SUCCESS) {
		std::cerr << "Failed to create kernel." << std::endl;
		return -1;
	}

	cl_mem clBuffer = clCreateFromGLBuffer(context(),
		CL_MEM_READ_WRITE,
		VBO,
		&err);

	if (err != CL_SUCCESS) {
		std::cerr << "Failed to create CL buffer from GL buffer." << std::endl;
		return -1;
	}

	kernel.setArg(0,
		clBuffer);

	while (!glfwWindowShouldClose(window)) {
		// Input handling here

		// Clear the screen
		glClear(GL_COLOR_BUFFER_BIT);

		// Use the shader program
		glUseProgram(shaderProgram);

		// Execute OpenCL kernel
		clEnqueueAcquireGLObjects(queue(),
			1,
			&clBuffer,
			0,
			nullptr,
			nullptr);

		queue.enqueueNDRangeKernel(kernel,
			cl::NullRange,
			cl::NDRange(vertices.size() / 3),
			cl::NullRange);

		clEnqueueReleaseGLObjects(queue(),
			1,
			&clBuffer,
			0,
			nullptr,
			nullptr);

		queue.finish();

		// Bind VAO and draw the triangle
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES,
			0,
			vertices.size() / 3);

		// Swap buffers and poll IO events
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}