#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV float hitValue;

void main()
{
    hitValue = -1.0f;
}