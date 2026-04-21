#pragma once
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_KEYSTATE_BASED_INPUT

#define GLFW_INCLUDE_NONE          // важно: до glfw3.h
#include <glad/glad.h>             // glad.h — раньше любых GL header’ов
#include <GLFW/glfw3.h>

#include "nuklear.h"
#include "nuklear_glfw_gl3.h"
