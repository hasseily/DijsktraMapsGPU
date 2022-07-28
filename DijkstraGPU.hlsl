//--------------------------------------------------------------------------------------
//
// This file contains the Compute Shader to perform calculations for a Dijkstra map
// Currently the map has to be 32x32
// 
// At start we need to initialize BufferOut with the weights/goals
//--------------------------------------------------------------------------------------

struct BufType
{
    int i;
};

StructuredBuffer<BufType> Buffer0 : register(t0);
RWStructuredBuffer<BufType> BufferOut : register(u0);

// ByteAddressBuffer Buffer0 : register(t0);
// RWByteAddressBuffer BufferOut : register(u0);

#define W_WALL 100
#define MWIDTH 32   // map width
#define MHEIGHT 32  // map height

[numthreads(8, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    int x = DTid.x % MWIDTH;
    int y = DTid.x / MWIDTH;
    int wOld = BufferOut[DTid.x].i;
    if (wOld == W_WALL)
        return;
    int w0 = wOld;
    // columns wrap
    if (x < MWIDTH)
        w0 = min(w0, BufferOut[y * MWIDTH + x + 1].i + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + 0].i + Buffer0[DTid.x].i + 1);
    if (x > 0)
        w0 = min(w0, BufferOut[y * MWIDTH + x - 1].i + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + MWIDTH - 1].i + Buffer0[DTid.x].i + 1);
    // rows don't wrap so don't do the edges
    if (y < (MHEIGHT - 1))
        w0 = min(w0, BufferOut[(y + 1) * MWIDTH + x].i + Buffer0[DTid.x].i + 1);
    if (y > 0)
        w0 = min(w0, BufferOut[(y - 1) * MWIDTH + x].i + Buffer0[DTid.x].i + 1);

    BufferOut[DTid.x].i = w0;
}
