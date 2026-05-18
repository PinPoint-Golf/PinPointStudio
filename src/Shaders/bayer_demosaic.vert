/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#version 440

layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec2 a_uv;

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform UBO {
    mat4  corrMatrix;  // rhi->clipSpaceCorrMatrix() to handle Y-axis differences
    float pattern;     // float to avoid int/float mixing in HLSL cbuffer
    float texW;
    float texH;
    float opacity;
};

void main()
{
    v_uv = a_uv;
    gl_Position = corrMatrix * a_pos;
}
