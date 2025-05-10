#version 460

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outFragColor;

void main() {
    // просто выводим цвет, полученный из вершинного шейдера
    outFragColor = vec4(inColor, 1.0);
}
