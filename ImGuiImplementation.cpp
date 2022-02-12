// This file implements imgui, it should only have to be build once
// and can then be continuesly be used through out the project
// VisualSingularity uses the glfw + opengl3 backends for rendering

#define _CRT_SECURE_NO_WARNINGS
#include <backends\imgui_impl_opengl3.cpp>
#include <backends\imgui_impl_glfw.cpp>

#include <imgui.cpp>
#include <imgui_demo.cpp>
#include <imgui_draw.cpp>
#include <imgui_tables.cpp>
#include <imgui_widgets.cpp>
