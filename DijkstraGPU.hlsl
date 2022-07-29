//--------------------------------------------------------------------------------------
//
// This file contains the Compute Shader to perform calculations for a Dijkstra map
// Currently the map has to be 32x32
// 
// At start we need to initialize BufferOut with the weights/goals
//--------------------------------------------------------------------------------------

struct RBuf
{
    int i;
};

struct RWBuf
{
    int map1;
    int map2;
    int map3;
};

StructuredBuffer<RBuf> Buffer0 : register(t0);
RWStructuredBuffer<RWBuf> BufferOut : register(u0);

// ByteAddressBuffer Buffer0 : register(t0);
// RWByteAddressBuffer BufferOut : register(u0);

#define W_WALL 10000
#define MWIDTH 32   // map width
#define MHEIGHT 32  // map height

[numthreads(8, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    int x = DTid.x % MWIDTH;
    int y = DTid.x / MWIDTH;
    int w0;
    int wOld;
    
    // ---- Map 1 ----
    wOld = BufferOut[DTid.x].map1;
    if (wOld == W_WALL)
        return;
    w0 = wOld;
    // columns wrap
    if (x < MWIDTH)
        w0 = min(w0, BufferOut[y * MWIDTH + x + 1].map1 + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + 0].map1 + Buffer0[DTid.x].i + 1);
    if (x > 0)
        w0 = min(w0, BufferOut[y * MWIDTH + x - 1].map1 + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + MWIDTH - 1].map1 + Buffer0[DTid.x].i + 1);
    // rows don't wrap so don't do the edges
    if (y < (MHEIGHT - 1))
        w0 = min(w0, BufferOut[(y + 1) * MWIDTH + x].map1 + Buffer0[DTid.x].i + 1);
    if (y > 0)
        w0 = min(w0, BufferOut[(y - 1) * MWIDTH + x].map1 + Buffer0[DTid.x].i + 1);

    BufferOut[DTid.x].map1 = w0;

    // ---- Map 2 ----
    wOld = BufferOut[DTid.x].map2;
    if (wOld == W_WALL)
        return;
    w0 = wOld;
    // columns wrap
    if (x < MWIDTH)
        w0 = min(w0, BufferOut[y * MWIDTH + x + 1].map2 + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + 0].map2 + Buffer0[DTid.x].i + 1);
    if (x > 0)
        w0 = min(w0, BufferOut[y * MWIDTH + x - 1].map2 + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + MWIDTH - 1].map2 + Buffer0[DTid.x].i + 1);
    // rows don't wrap so don't do the edges
    if (y < (MHEIGHT - 1))
        w0 = min(w0, BufferOut[(y + 1) * MWIDTH + x].map2 + Buffer0[DTid.x].i + 1);
    if (y > 0)
        w0 = min(w0, BufferOut[(y - 1) * MWIDTH + x].map2 + Buffer0[DTid.x].i + 1);

    BufferOut[DTid.x].map2 = w0;

    // ---- Map 3 ----
    wOld = BufferOut[DTid.x].map3;
    if (wOld == W_WALL)
        return;
    w0 = wOld;
    // columns wrap
    if (x < MWIDTH)
        w0 = min(w0, BufferOut[y * MWIDTH + x + 1].map3 + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + 0].map3 + Buffer0[DTid.x].i + 1);
    if (x > 0)
        w0 = min(w0, BufferOut[y * MWIDTH + x - 1].map3 + Buffer0[DTid.x].i + 1);
    else
        w0 = min(w0, BufferOut[y * MWIDTH + MWIDTH - 1].map3 + Buffer0[DTid.x].i + 1);
    // rows don't wrap so don't do the edges
    if (y < (MHEIGHT - 1))
        w0 = min(w0, BufferOut[(y + 1) * MWIDTH + x].map3 + Buffer0[DTid.x].i + 1);
    if (y > 0)
        w0 = min(w0, BufferOut[(y - 1) * MWIDTH + x].map3 + Buffer0[DTid.x].i + 1);

    BufferOut[DTid.x].map3 = w0;
}
