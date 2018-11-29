#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec2 attribs;

void main()
{
    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = barycentrics;
    
    if(gl_InstanceCustomIndexNV == 0)
    {
        hitValue = vec3(0.0, 0.8, 1.0);
    }

    if(gl_InstanceCustomIndexNV == 1)
    {
        hitValue = vec3(1.0, 0.3, 0.2);
    }
}