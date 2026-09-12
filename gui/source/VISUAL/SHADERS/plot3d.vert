#version 440

// Position and colour of one vertex of the 3D peak map. Every primitive the
// canvas draws (ground, axes, ticks, grid, sticks, top-view points) uses this
// same layout, so one pipeline per topology covers the whole view.
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float pointSize;
};

void main()
{
    v_color = color;
    gl_Position = mvp * vec4(position, 1.0);
    gl_PointSize = pointSize;
}
