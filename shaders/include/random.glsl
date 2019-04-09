uint TauswortheStep(uint z, int S1, int S2, int S3, uint M)
{
    uint b = (((z << S1) ^ z) >> S2);
    return (((z & M) << S3) ^ b);
}

uint LCGStep(uint z, uint A, uint C)
{
    return (A * z + C);
}

uvec4 seed;
uint urand(){
    seed.x = TauswortheStep(seed.x, 13, 19, 12, 4294967294);
    seed.y = TauswortheStep(seed.y, 2, 25, 4, 4294967288);
    seed.z = TauswortheStep(seed.z, 3, 11, 17, 4294967280);
    seed.w = LCGStep(seed.w, 1664525, 1013904223);
    return seed.x ^ seed.y ^ seed.z ^ seed.w;
}

float rand()
{
    return 2.3283064365387e-10 * float(urand());
}

void randomInit(ivec2 position){
    seed = imageLoad(randTex, position);
}

void randomFinish(ivec2 position){
    imageStore(randTex, position, seed);
}