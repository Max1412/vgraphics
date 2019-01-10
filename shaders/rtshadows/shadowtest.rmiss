#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV int hitValue;

void main()
{
    hitValue = 0; // no shadow!!
}