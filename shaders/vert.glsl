#version 460

// binding = 0 соответствует вашему VkDescriptorSetLayoutBinding
layout(std430, binding = 0) buffer VertexData {
    vec2 positions[];   // динамический массив позиций
};

layout(location = 0) out vec3 outColor;

void main() {
    // берём позицию из SSBO по индексу вершины
    vec2 p = positions[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);

    // задаём цвет (оранжевый)
    outColor = vec3(1.0, 0.5, 0.0);
}
