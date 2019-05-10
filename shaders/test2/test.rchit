#version 460
#extension GL_NV_ray_tracing : require
#extension GL_KHR_shader_subgroup_basic : require

void main()
{
    uint test = gl_SubgroupInvocationID;
}