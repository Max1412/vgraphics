#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV float hitValue;

void main()
{
    hitValue = gl_HitTNV; // ||S - P||
}