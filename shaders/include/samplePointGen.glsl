vec3 generatePointOnSphericalLight(in vec3 position, in float radius)
{
    float theta = rand() * 2.0f * PI;
    float u = (rand() * 2.0f) - 1.0f;
    float x = sqrt(1-(u*u)) * cos(theta);
    float y = sqrt(1-(u*u)) * sin(theta);
    float z = u;
    return position + (radius * vec3(x, y, z));
}

vec3 generatePointOnDiskLight(in vec3 position, in float radius, in vec3 normal)
{
    float r = rand();
    float theta = rand() * 2.0f * PI;
    float x = sqrt(r) * cos(theta);
    float y = sqrt(r) * sin(theta);

    vec3 up = vec3(0, 1, 0);

    // check if up == normal
    if(dot(normal, up) == 1.0f) up = vec3(1, 0, 0);

    vec3 a = cross(normal, up);
    vec3 b = cross(a, normal);

    return position + radius * a * x + radius * b * y;
}

vec3 sampleCosineHemisphere(float u, float v) { //u, v are random numbers
	float sinTheta = sqrt(u);
	float phi = 2.0f * PI * v;

	float x = sinTheta * cos(phi);
	float y = sinTheta * sin(phi);	

	// Project point up to the unit sphere
    float z = sqrt(max(0.f, 1.f - x * x - y * y));
    return vec3(x, y, z);
}

vec3 sampleUniformHemisphere(float u, float v) { //u, v are random numbers
	float sinTheta = sqrt(2.0f * u - u * u);  //sin(arccos(x)) = sqrt(1-x^2)
	float phi = 2.0f * PI * v;

	float x = sinTheta * cos(phi);
	float y = sinTheta * sin(phi);	

	// Project point up to the unit sphere
    float z = sqrt(max(0.f, 1.f - x * x - y * y));
    return vec3(x, y, z);
}

vec3 rotateToNormal(in vec3 dir, in vec3 normal) {
	/*vec3 tangent = normalize(abs(normal.x) > abs(normal.y) ? vec3(normal.z, 0.f, -normal.x) : vec3(0.f, -normal.z, normal.y));
	vec3 bitangent = cross(normal, tangent);
	mat3 rotToNormal = mat3(tangent, bitangent, normal);

	return rotToNormal * dir;*/
	vec3 pseudo_perpendicular = (abs(normal.x) <= 0.6f) ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 u = normalize(cross(normal, pseudo_perpendicular));
    vec3 v = normalize(cross(normal, u));
    vec3 w = normal;

    return mat3(u,v,w) * dir;
}

vec3 sampleRotatedHemisphere(in vec3 normal)
{
    vec3 hemiPoint = sampleUniformHemisphere(rand(), rand());
    return rotateToNormal(hemiPoint, normal);
}

vec3 sampleRotatedCosineHemisphere(in vec3 normal)
{
    vec3 hemiPoint = sampleCosineHemisphere(rand(), rand());
    return rotateToNormal(hemiPoint, normal);
}