#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV uint hitValue;

void main()
{
    hitValue = 0U; // no shadow!!
}