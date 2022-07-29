//--------------------------------------------------------------------------------------
// Dijkstra Maps using a GPU compute shader.
// Based off of the DX11 Microsoft sample BasicCompute11.cpp.
// A lot of the code in here can be removed and is unnecessary. This is test code.
//--------------------------------------------------------------------------------------

#include <stdio.h>
#include <iostream>
#include <crtdbg.h>
#include <d3dcommon.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <ctime>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p)=nullptr; } }
#endif

#define USE_STRUCTURED_BUFFERS

// Map size
constexpr int MAPW = 32;
constexpr int MAPH = 32;

using MapMatrix = char[MAPW][MAPH];

// The number of elements in a buffer to be tested
constexpr UINT NUM_ELEMENTS = MAPW * MAPH;

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
RBuf g_vBuf0[NUM_ELEMENTS];
RWBuf g_vBuf1[NUM_ELEMENTS];

// Goals are set to 0 on the map
#define SetGoalMap1(x, y) g_vBuf1[y * MAPW + x].map1 = 0
#define SetGoalMap2(x, y) g_vBuf1[y * MAPW + x].map2 = 0
#define SetGoalMap3(x, y) g_vBuf1[y * MAPW + x].map3 = 0

#define W_WALL 10000    // Value of the wall tile

//--------------------------------------------------------------------------------------
// Forward declarations 
//--------------------------------------------------------------------------------------
HRESULT CreateComputeDevice( _Outptr_ ID3D11Device** ppDeviceOut, _Outptr_ ID3D11DeviceContext** ppContextOut, _In_ bool bForceRef );
HRESULT CreateComputeShader( _In_z_ LPCWSTR pSrcFile, _In_z_ LPCSTR pFunctionName, 
                             _In_ ID3D11Device* pDevice, _Outptr_ ID3D11ComputeShader** ppShaderOut );
HRESULT CreateStructuredBuffer( _In_ ID3D11Device* pDevice, _In_ UINT uElementSize, _In_ UINT uCount,
                                _In_reads_(uElementSize*uCount) void* pInitData,
                                _Outptr_ ID3D11Buffer** ppBufOut );
HRESULT CreateRawBuffer( _In_ ID3D11Device* pDevice, _In_ UINT uSize, _In_reads_(uSize) void* pInitData, _Outptr_ ID3D11Buffer** ppBufOut );
HRESULT CreateBufferSRV( _In_ ID3D11Device* pDevice, _In_ ID3D11Buffer* pBuffer, _Outptr_ ID3D11ShaderResourceView** ppSRVOut );
HRESULT CreateBufferUAV( _In_ ID3D11Device* pDevice, _In_ ID3D11Buffer* pBuffer, _Outptr_ ID3D11UnorderedAccessView** pUAVOut );
ID3D11Buffer* CreateAndCopyToDebugBuf( _In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext* pd3dImmediateContext, _In_ ID3D11Buffer* pBuffer );
void RunComputeShader( _In_ ID3D11DeviceContext* pd3dImmediateContext,
                       _In_ ID3D11ComputeShader* pComputeShader,
                       _In_ UINT nNumViews, _In_reads_(nNumViews) ID3D11ShaderResourceView** pShaderResourceViews, 
                       _In_opt_ ID3D11Buffer* pCBCS, _In_reads_opt_(dwNumDataBytes) void* pCSData, _In_ DWORD dwNumDataBytes,
                       _In_ ID3D11UnorderedAccessView* pUnorderedAccessView,
                       _In_ UINT X, _In_ UINT Y, _In_ UINT Z );
HRESULT FindDXSDKShaderFileCch( _Out_writes_(cchDest) WCHAR* strDestPath,
                                _In_ int cchDest, 
                                _In_z_ LPCWSTR strFilename );

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
ID3D11Device*               g_pDevice = nullptr;
ID3D11DeviceContext*        g_pContext = nullptr;
ID3D11ComputeShader*        g_pCS = nullptr;

ID3D11Buffer*               g_pBuf0 = nullptr;
ID3D11Buffer*               g_pBuf1 = nullptr;
ID3D11Buffer*               g_pBufResult = nullptr;

ID3D11ShaderResourceView*   g_pBuf0SRV = nullptr;
ID3D11ShaderResourceView*   g_pBuf1SRV = nullptr;
ID3D11UnorderedAccessView*  g_pBufResultUAV = nullptr;

//--------------------------------------------------------------------------------------
// Entry point to the program
//--------------------------------------------------------------------------------------
int __cdecl main()
{
    // Enable run-time memory check for debug builds.
#ifdef _DEBUG
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif    

	// Set output mode to handle virtual terminal sequences
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
	{
		return GetLastError();
	}
	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode))
	{
		return GetLastError();
	}
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode))
	{
		return GetLastError();
	}
	// Now the console is ready for virtual terminal sequences

    printf( "Creating device..." );
    if ( FAILED( CreateComputeDevice( &g_pDevice, &g_pContext, false ) ) )
        return 1;
    printf( "done\n" );

    printf( "Creating Compute Shader..." );
    if ( FAILED( CreateComputeShader( L"DijkstraGPU.hlsl", "CSMain", g_pDevice, &g_pCS ) ) )
        return 1;
    printf( "done\n" );

    printf("Creating initial map...");
	srand(clock());
    // Matrix of additional step costs
    // Base step cost is 1
    // This matrix adds to the step costs
    // Here we have doors that add 1 to the step costs,
    // A firewall that adds 4 to the step costs,
    // and impassable walls that are set at 9.
    // Walls are hard-coded to be value 9.
	MapMatrix tmp_m = {
			{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {4, 4, 4, 4, 4, 4, 4, 9, 1, 9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {4, 4, 4, 4, 4, 4, 4, 9, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 1, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 1, 9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 1, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 9, 0, 1, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 9, 0, 9, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 1, 0, 9, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 9, 9, 9, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	};
	printf("done\n");

	printf("Creating buffers and filling them with initial data...");

	// Reverse the matrix so it is correct
	// and generate from it both the read-only movement buffer and the initial weights result buffer
	// MapMatrix m, dmmap;
	int dmapVal = 0;
	for (int j = 0; j < MAPH; j++)
	{
		for (int i = 0; i < MAPW; i++)
		{
			g_vBuf0[j * MAPW + i].i = tmp_m[j][i];  // read-only movement buffer
			switch (g_vBuf0[j * MAPW + i].i)
			{
			case 9:		// wall
				dmapVal = W_WALL;
				break;
			case 1:		// door
			case 4:		// firewall
			default:	// normal
				dmapVal = W_WALL - 1;   // very high number
				break;
			}
            // Now set the values of the maps.
            // Here the maps are exactly the same for testing
            g_vBuf1[j * MAPW + i].map1 = dmapVal;
            g_vBuf1[j * MAPW + i].map2 = dmapVal;
            g_vBuf1[j * MAPW + i].map3 = dmapVal;
		}
	};
    // Set goals on the map by entering value 0 on a tile
    // You can set as many as you want
    SetGoalMap1(28, 9);
    SetGoalMap1(15, 23);
    SetGoalMap1(10, 5);
	
	SetGoalMap2(28, 9);
	SetGoalMap2(15, 23);
	SetGoalMap2(10, 5);
	SetGoalMap3(28, 9);
	SetGoalMap3(15, 23);
	SetGoalMap3(10, 5);

    /*
    SetGoalMap2(13, 7);
	SetGoalMap2(1, 30);

	SetGoalMap3(10, 6);
	SetGoalMap3(11, 5);
	SetGoalMap3(11, 6);
	SetGoalMap3(10, 5);
	*/
    printf("done\n");


/////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start Benchmarking code
/////////////////////////////////////////////////////////////////////////////////////////////////////////
	{
		clock_t start;
		clock_t diff;
		int msec = 0;
		start = clock();
		constexpr int timingIters = 1000;   // Number of complete iterations for benchmarking

		for (int a = 0; a < timingIters; a++)
		{
			CreateStructuredBuffer(g_pDevice, sizeof(RBuf), NUM_ELEMENTS, &g_vBuf0[0], &g_pBuf0);
			// Initialize result buffer with the starting goals, max weights and walls.
			CreateStructuredBuffer(g_pDevice, sizeof(RWBuf), NUM_ELEMENTS, &g_vBuf1[0], &g_pBufResult);

#if defined(_DEBUG) || defined(PROFILE)
			if (g_pBuf0)
				g_pBuf0->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("Buffer0") - 1, "Buffer0");
			if (g_pBufResult)
				g_pBufResult->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("Result") - 1, "Result");
#endif
			CreateBufferSRV(g_pDevice, g_pBuf0, &g_pBuf0SRV);
			CreateBufferUAV(g_pDevice, g_pBufResult, &g_pBufResultUAV);

#if defined(_DEBUG) || defined(PROFILE)
			if (g_pBuf0SRV)
				g_pBuf0SRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("Buffer0 SRV") - 1, "Buffer0 SRV");
			if (g_pBufResultUAV)
				g_pBufResultUAV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("Result UAV") - 1, "Result UAV");
#endif

			ID3D11ShaderResourceView* aRViews[1] = { g_pBuf0SRV };
			for (size_t iters = 0; iters < 30; iters++)
			{
				RunComputeShader(g_pContext, g_pCS, 1, aRViews, nullptr, nullptr, 0, g_pBufResultUAV, NUM_ELEMENTS, 1, 1);
			}

			// Read back the result from GPU
			{
				ID3D11Buffer* debugbuf = CreateAndCopyToDebugBuf(g_pDevice, g_pContext, g_pBufResult);
				D3D11_MAPPED_SUBRESOURCE MappedResource;
				RWBuf* p;
				g_pContext->Map(debugbuf, 0, D3D11_MAP_READ, 0, &MappedResource);

				// Set a break point here and put down the expression "p, 1024" in your watch window to see what has been written out by our CS
				// This is also a common trick to debug CS programs.
				p = (RWBuf*)MappedResource.pData;

				g_pContext->Unmap(debugbuf, 0);

				SAFE_RELEASE(debugbuf);
			}

			SAFE_RELEASE(g_pBufResultUAV);
			SAFE_RELEASE(g_pBufResult);
			SAFE_RELEASE(g_pBuf0SRV);
			SAFE_RELEASE(g_pBuf0);
		}


		diff = clock() - start;
		msec = diff * 1000 / CLOCKS_PER_SEC;
		printf("Time taken for %d Dijkstra Maps: %d seconds %d milliseconds\n", timingIters, msec / 1000, msec % 1000);
	}
/////////////////////////////////////////////////////////////////////////////////////////////////////////
// End Benchmarking code
/////////////////////////////////////////////////////////////////////////////////////////////////////////




// Single run here with output

    printf("Assigning GPU buffers...");
    // Initialize read-only steps weights buffer
    CreateStructuredBuffer( g_pDevice, sizeof(RBuf), NUM_ELEMENTS, &g_vBuf0[0], &g_pBuf0 );
	// Initialize result buffer with the starting goals, max weights and walls.
    CreateStructuredBuffer( g_pDevice, sizeof(RWBuf), NUM_ELEMENTS, &g_vBuf1[0], &g_pBufResult );

#if defined(_DEBUG) || defined(PROFILE)
    if ( g_pBuf0 )
        g_pBuf0->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Buffer0" ) - 1, "Buffer0" );
    if ( g_pBufResult )
        g_pBufResult->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Result" ) - 1, "Result" );
#endif

    printf( "done\n" );

    printf( "Creating buffer views..." );
    CreateBufferSRV( g_pDevice, g_pBuf0, &g_pBuf0SRV );
    CreateBufferUAV( g_pDevice, g_pBufResult, &g_pBufResultUAV );

#if defined(_DEBUG) || defined(PROFILE)
    if ( g_pBuf0SRV )
        g_pBuf0SRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Buffer0 SRV" ) - 1, "Buffer0 SRV" );
    if ( g_pBufResultUAV )
        g_pBufResultUAV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Result UAV" ) - 1, "Result UAV" );
#endif

    printf( "done\n" );

    printf( "Running Compute Shader..." );
    ID3D11ShaderResourceView* aRViews[1] = { g_pBuf0SRV };
    for (size_t iters = 0; iters < 30; iters++)
    {
		RunComputeShader(g_pContext, g_pCS, 1, aRViews, nullptr, nullptr, 0, g_pBufResultUAV, NUM_ELEMENTS, 1, 1);
    }
    printf( "done\n" );

    // Read back the result from GPU
    {
        ID3D11Buffer* debugbuf = CreateAndCopyToDebugBuf( g_pDevice, g_pContext, g_pBufResult );
        D3D11_MAPPED_SUBRESOURCE MappedResource; 
        RWBuf *p;
        g_pContext->Map( debugbuf, 0, D3D11_MAP_READ, 0, &MappedResource );

        // Set a break point here and put down the expression "p, 1024" in your watch window to see what has been written out by our CS
        // This is also a common trick to debug CS programs.
        p = (RWBuf*)MappedResource.pData;

        // Draw results
        std::cout << " ===< MAP 1 >===" << std::endl;
		for (int y = -1; y <= MAPH; y++) {
			// for (int x = -1; x <= MAPW; x++) {	// to add vertical walls
			for (int x = 0; x < MAPW; x++) {		// no vertical walls
                const int xy = y * MAPW + x;
				if (x < 0 || y < 0 || x >(MAPW - 1) || y >(MAPH - 1) || p[xy].map1 == W_WALL)
					std::cout << char(0xdb) << char(0xdb) << char(0xdb) << char(0xdb);	// Draw walls
				else if (p[xy].map1 > 0)
				{
					char buf[30];
					sprintf_s(buf, "%4d", p[xy].map1);
					if (g_vBuf0[xy].i == 1)	// door
						std::cout << "\033[1;44m" << buf << "\033[0m";      // bg blue
					else if (g_vBuf0[xy].i == 4)	// fire wall
						std::cout << "\033[1;41m" << buf << "\033[0m";      // bg red
                    else if (g_vBuf0[xy].i > 0)     // Something else that we don't know about
                        std::cout << "\033[1;45m" << buf << "\033[0m";      // bg purple
					else
					{
						if (p[xy].map1 > 14)
							std::cout << "\033[1;31m" << buf << "\033[0m";  // red
						else if (p[xy].map1 > 9)
							std::cout << "\033[1;33m" << buf << "\033[0m";  // yellow
						else
							std::cout << "\033[1;32m" << buf << "\033[0m";  // green
					}
				}
				else {	// goal
					std::cout << "\033[1;42m" << "GOAL" << "\033[0m";       // bg green
				}
			}
			std::cout << "\n";
		}
		std::cout << " ===< MAP 2 >===" << std::endl;
		for (int y = -1; y <= MAPH; y++) {
			// for (int x = -1; x <= MAPW; x++) {	// to add vertical walls
			for (int x = 0; x < MAPW; x++) {		// no vertical walls
				const int xy = y * MAPW + x;
				if (x < 0 || y < 0 || x >(MAPW - 1) || y >(MAPH - 1) || p[xy].map2 == W_WALL)
					std::cout << char(0xdb) << char(0xdb) << char(0xdb) << char(0xdb);	// Draw walls
				else if (p[xy].map2 > 0)
				{
					char buf[30];
					sprintf_s(buf, "%4d", p[xy].map2);
					if (g_vBuf0[xy].i == 1)	// door
						std::cout << "\033[1;44m" << buf << "\033[0m";      // bg blue
					else if (g_vBuf0[xy].i == 4)	// fire wall
						std::cout << "\033[1;41m" << buf << "\033[0m";      // bg red
					else if (g_vBuf0[xy].i > 0)     // Something else that we don't know about
						std::cout << "\033[1;45m" << buf << "\033[0m";      // bg purple
					else
					{
						if (p[xy].map2 > 14)
							std::cout << "\033[1;31m" << buf << "\033[0m";  // red
						else if (p[xy].map2 > 9)
							std::cout << "\033[1;33m" << buf << "\033[0m";  // yellow
						else
							std::cout << "\033[1;32m" << buf << "\033[0m";  // green
					}
				}
				else {	// goal
					std::cout << "\033[1;42m" << "GOAL" << "\033[0m";       // bg green
				}
			}
			std::cout << "\n";
		}
		std::cout << " ===< MAP 3 >===" << std::endl;
		for (int y = -1; y <= MAPH; y++) {
			// for (int x = -1; x <= MAPW; x++) {	// to add vertical walls
			for (int x = 0; x < MAPW; x++) {		// no vertical walls
				const int xy = y * MAPW + x;
				if (x < 0 || y < 0 || x >(MAPW - 1) || y >(MAPH - 1) || p[xy].map3 == W_WALL)
					std::cout << char(0xdb) << char(0xdb) << char(0xdb) << char(0xdb);	// Draw walls
				else if (p[xy].map3 > 0)
				{
					char buf[30];
					sprintf_s(buf, "%4d", p[xy].map3);
					if (g_vBuf0[xy].i == 1)	// door
						std::cout << "\033[1;44m" << buf << "\033[0m";      // bg blue
					else if (g_vBuf0[xy].i == 4)	// fire wall
						std::cout << "\033[1;41m" << buf << "\033[0m";      // bg red
					else if (g_vBuf0[xy].i > 0)     // Something else that we don't know about
						std::cout << "\033[1;45m" << buf << "\033[0m";      // bg purple
					else
					{
						if (p[xy].map3 > 14)
							std::cout << "\033[1;31m" << buf << "\033[0m";  // red
						else if (p[xy].map3 > 9)
							std::cout << "\033[1;33m" << buf << "\033[0m";  // yellow
						else
							std::cout << "\033[1;32m" << buf << "\033[0m";  // green
					}
				}
				else {	// goal
					std::cout << "\033[1;42m" << "GOAL" << "\033[0m";       // bg green
				}
			}
			std::cout << "\n";
		}

        g_pContext->Unmap( debugbuf, 0 );

        SAFE_RELEASE( debugbuf );
    }
    
    printf( "Cleaning up...\n" );
    SAFE_RELEASE( g_pBuf0SRV );
    SAFE_RELEASE( g_pBuf1SRV );
    SAFE_RELEASE( g_pBufResultUAV );
    SAFE_RELEASE( g_pBuf0 );
    SAFE_RELEASE( g_pBuf1 );
    SAFE_RELEASE( g_pBufResult );
    SAFE_RELEASE( g_pCS );
    SAFE_RELEASE( g_pContext );
    SAFE_RELEASE( g_pDevice );

    return 0;
}

// Everything below is D3D11 setup

//--------------------------------------------------------------------------------------
// Create the D3D device and device context suitable for running Compute Shaders(CS)
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateComputeDevice( ID3D11Device** ppDeviceOut, ID3D11DeviceContext** ppContextOut, bool bForceRef )
{    
    *ppDeviceOut = nullptr;
    *ppContextOut = nullptr;
    
    HRESULT hr = S_OK;

    UINT uCreationFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#ifdef _DEBUG
    uCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL flOut;
    static const D3D_FEATURE_LEVEL flvl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    
    bool bNeedRefDevice = false;
    if ( !bForceRef )
    {
        hr = D3D11CreateDevice( nullptr,                        // Use default graphics card
                                D3D_DRIVER_TYPE_HARDWARE,    // Try to create a hardware accelerated device
                                nullptr,                        // Do not use external software rasterizer module
                                uCreationFlags,              // Device creation flags
                                flvl,
                                sizeof(flvl) / sizeof(D3D_FEATURE_LEVEL),
                                D3D11_SDK_VERSION,           // SDK version
                                ppDeviceOut,                 // Device out
                                &flOut,                      // Actual feature level created
                                ppContextOut );              // Context out
        
        if ( SUCCEEDED( hr ) )
        {
            // A hardware accelerated device has been created, so check for Compute Shader support

            // If we have a device >= D3D_FEATURE_LEVEL_11_0 created, full CS5.0 support is guaranteed, no need for further checks
            if ( flOut < D3D_FEATURE_LEVEL_11_0 )            
            {
#ifdef TEST_DOUBLE
                bNeedRefDevice = true;
                printf( "No hardware Compute Shader 5.0 capable device found (required for doubles), trying to create ref device.\n" );
#else
                // Otherwise, we need further check whether this device support CS4.x (Compute on 10)
                D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS hwopts;
                (*ppDeviceOut)->CheckFeatureSupport( D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &hwopts, sizeof(hwopts) );
                if ( !hwopts.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x )
                {
                    bNeedRefDevice = true;
                    printf( "No hardware Compute Shader capable device found, trying to create ref device.\n" );
                }
#endif
            }

#ifdef TEST_DOUBLE
            else
            {
                // Double-precision support is an optional feature of CS 5.0
                D3D11_FEATURE_DATA_DOUBLES hwopts;
                (*ppDeviceOut)->CheckFeatureSupport( D3D11_FEATURE_DOUBLES, &hwopts, sizeof(hwopts) );
                if ( !hwopts.DoublePrecisionFloatShaderOps )
                {
                    bNeedRefDevice = true;
                    printf( "No hardware double-precision capable device found, trying to create ref device.\n" );
                }
            }
#endif
        }
    }
    
    if ( bForceRef || FAILED(hr) || bNeedRefDevice )
    {
        // Either because of failure on creating a hardware device or hardware lacking CS capability, we create a ref device here

        SAFE_RELEASE( *ppDeviceOut );
        SAFE_RELEASE( *ppContextOut );
        
        hr = D3D11CreateDevice( nullptr,                        // Use default graphics card
                                D3D_DRIVER_TYPE_REFERENCE,   // Try to create a hardware accelerated device
                                nullptr,                        // Do not use external software rasterizer module
                                uCreationFlags,              // Device creation flags
                                flvl,
                                sizeof(flvl) / sizeof(D3D_FEATURE_LEVEL),
                                D3D11_SDK_VERSION,           // SDK version
                                ppDeviceOut,                 // Device out
                                &flOut,                      // Actual feature level created
                                ppContextOut );              // Context out
        if ( FAILED(hr) )
        {
            printf( "Reference rasterizer device create failure\n" );
            return hr;
        }
    }

    return hr;
}

//--------------------------------------------------------------------------------------
// Compile and create the CS
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateComputeShader( LPCWSTR pSrcFile, LPCSTR pFunctionName, 
                             ID3D11Device* pDevice, ID3D11ComputeShader** ppShaderOut )
{
    if ( !pDevice || !ppShaderOut )
        return E_INVALIDARG;

    // Finds the correct path for the shader file.
    // This is only required for this sample to be run correctly from within the Sample Browser,
    // in your own projects, these lines could be removed safely
    WCHAR str[MAX_PATH];
    HRESULT hr = FindDXSDKShaderFileCch( str, MAX_PATH, pSrcFile );
    if ( FAILED(hr) )
        return hr;
    
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3DCOMPILE_DEBUG;

    // Disable optimizations to further improve shader debugging
    dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    const D3D_SHADER_MACRO defines[] = 
    {
        "USE_STRUCTURED_BUFFERS", "1",
        nullptr, nullptr
    };

    // We generally prefer to use the higher CS shader profile when possible as CS 5.0 is better performance on 11-class hardware
    LPCSTR pProfile = ( pDevice->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ) ? "cs_5_0" : "cs_4_0";

    ID3DBlob* pErrorBlob = nullptr;
    ID3DBlob* pBlob = nullptr;
    hr = D3DCompileFromFile( str, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, pFunctionName, pProfile, 
                             dwShaderFlags, 0, &pBlob, &pErrorBlob );
    if ( FAILED(hr) )
    {
        if ( pErrorBlob )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );

        SAFE_RELEASE( pErrorBlob );
        SAFE_RELEASE( pBlob );    

        return hr;
    }    

    hr = pDevice->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, ppShaderOut );

    SAFE_RELEASE( pErrorBlob );
    SAFE_RELEASE( pBlob );

#if defined(_DEBUG) || defined(PROFILE)
    if ( SUCCEEDED(hr) )
    {
        (*ppShaderOut)->SetPrivateData( WKPDID_D3DDebugObjectName, lstrlenA(pFunctionName), pFunctionName );
    }
#endif

    return hr;
}

//--------------------------------------------------------------------------------------
// Create Structured Buffer
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateStructuredBuffer( ID3D11Device* pDevice, UINT uElementSize, UINT uCount, void* pInitData, ID3D11Buffer** ppBufOut )
{
    *ppBufOut = nullptr;

    D3D11_BUFFER_DESC desc = {};
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.ByteWidth = uElementSize * uCount;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = uElementSize;

    if ( pInitData )
    {
        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem = pInitData;
        return pDevice->CreateBuffer( &desc, &InitData, ppBufOut );
    } else
        return pDevice->CreateBuffer( &desc, nullptr, ppBufOut );
}

//--------------------------------------------------------------------------------------
// Create Raw Buffer
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateRawBuffer( ID3D11Device* pDevice, UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut )
{
    *ppBufOut = nullptr;

    D3D11_BUFFER_DESC desc = {};
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = uSize;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    if ( pInitData )
    {
        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem = pInitData;
        return pDevice->CreateBuffer( &desc, &InitData, ppBufOut );
    } else
        return pDevice->CreateBuffer( &desc, nullptr, ppBufOut );
}

//--------------------------------------------------------------------------------------
// Create Shader Resource View for Structured or Raw Buffers
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateBufferSRV( ID3D11Device* pDevice, ID3D11Buffer* pBuffer, ID3D11ShaderResourceView** ppSRVOut )
{
    D3D11_BUFFER_DESC descBuf = {};
    pBuffer->GetDesc( &descBuf );

    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    desc.BufferEx.FirstElement = 0;

    if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS )
    {
        // This is a Raw Buffer

        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        desc.BufferEx.NumElements = descBuf.ByteWidth / 4;
    } else
    if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED )
    {
        // This is a Structured Buffer

        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
    } else
    {
        return E_INVALIDARG;
    }

    return pDevice->CreateShaderResourceView( pBuffer, &desc, ppSRVOut );
}

//--------------------------------------------------------------------------------------
// Create Unordered Access View for Structured or Raw Buffers
//-------------------------------------------------------------------------------------- 
_Use_decl_annotations_
HRESULT CreateBufferUAV( ID3D11Device* pDevice, ID3D11Buffer* pBuffer, ID3D11UnorderedAccessView** ppUAVOut )
{
    D3D11_BUFFER_DESC descBuf = {};
    pBuffer->GetDesc( &descBuf );
        
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;

    if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS )
    {
        // This is a Raw Buffer

        desc.Format = DXGI_FORMAT_R32_TYPELESS; // Format must be DXGI_FORMAT_R32_TYPELESS, when creating Raw Unordered Access View
        desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        desc.Buffer.NumElements = descBuf.ByteWidth / 4; 
    } else
    if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED )
    {
        // This is a Structured Buffer

        desc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
        desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride; 
    } else
    {
        return E_INVALIDARG;
    }
    
    return pDevice->CreateUnorderedAccessView( pBuffer, &desc, ppUAVOut );
}

//--------------------------------------------------------------------------------------
// Create a CPU accessible buffer and download the content of a GPU buffer into it
// This function is very useful for debugging CS programs
//-------------------------------------------------------------------------------------- 
_Use_decl_annotations_
ID3D11Buffer* CreateAndCopyToDebugBuf( ID3D11Device* pDevice, ID3D11DeviceContext* pd3dImmediateContext, ID3D11Buffer* pBuffer )
{
    ID3D11Buffer* debugbuf = nullptr;

    D3D11_BUFFER_DESC desc = {};
    pBuffer->GetDesc( &desc );
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    if ( SUCCEEDED(pDevice->CreateBuffer(&desc, nullptr, &debugbuf)) )
    {
#if defined(_DEBUG) || defined(PROFILE)
        debugbuf->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Debug" ) - 1, "Debug" );
#endif

        pd3dImmediateContext->CopyResource( debugbuf, pBuffer );
    }

    return debugbuf;
}

//--------------------------------------------------------------------------------------
// Run CS
//-------------------------------------------------------------------------------------- 
_Use_decl_annotations_
void RunComputeShader( ID3D11DeviceContext* pd3dImmediateContext,
                      ID3D11ComputeShader* pComputeShader,
                      UINT nNumViews, ID3D11ShaderResourceView** pShaderResourceViews, 
                      ID3D11Buffer* pCBCS, void* pCSData, DWORD dwNumDataBytes,
                      ID3D11UnorderedAccessView* pUnorderedAccessView,
                      UINT X, UINT Y, UINT Z )
{
    pd3dImmediateContext->CSSetShader( pComputeShader, nullptr, 0 );
    pd3dImmediateContext->CSSetShaderResources( 0, nNumViews, pShaderResourceViews );
    pd3dImmediateContext->CSSetUnorderedAccessViews( 0, 1, &pUnorderedAccessView, nullptr );
    if ( pCBCS && pCSData )
    {
        D3D11_MAPPED_SUBRESOURCE MappedResource;
        pd3dImmediateContext->Map( pCBCS, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
        memcpy( MappedResource.pData, pCSData, dwNumDataBytes );
        pd3dImmediateContext->Unmap( pCBCS, 0 );
        ID3D11Buffer* ppCB[1] = { pCBCS };
        pd3dImmediateContext->CSSetConstantBuffers( 0, 1, ppCB );
    }

    pd3dImmediateContext->Dispatch( X, Y, Z );

    pd3dImmediateContext->CSSetShader( nullptr, nullptr, 0 );

    ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
    pd3dImmediateContext->CSSetUnorderedAccessViews( 0, 1, ppUAViewnullptr, nullptr );

    ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
    pd3dImmediateContext->CSSetShaderResources( 0, 2, ppSRVnullptr );

    ID3D11Buffer* ppCBnullptr[1] = { nullptr };
    pd3dImmediateContext->CSSetConstantBuffers( 0, 1, ppCBnullptr );
}

//--------------------------------------------------------------------------------------
// Tries to find the location of the shader file
// This is a trimmed down version of DXUTFindDXSDKMediaFileCch.
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT FindDXSDKShaderFileCch( WCHAR* strDestPath,
                                int cchDest, 
                                LPCWSTR strFilename )
{
    if( !strFilename || strFilename[0] == 0 || !strDestPath || cchDest < 10 )
        return E_INVALIDARG;

    // Get the exe name, and exe path
    WCHAR strExePath[MAX_PATH] =
    {
        0
    };
    WCHAR strExeName[MAX_PATH] =
    {
        0
    };
    WCHAR* strLastSlash = nullptr;
    GetModuleFileName( nullptr, strExePath, MAX_PATH );
    strExePath[MAX_PATH - 1] = 0;
    strLastSlash = wcsrchr( strExePath, TEXT( '\\' ) );
    if( strLastSlash )
    {
        wcscpy_s( strExeName, MAX_PATH, &strLastSlash[1] );

        // Chop the exe name from the exe path
        *strLastSlash = 0;

        // Chop the .exe from the exe name
        strLastSlash = wcsrchr( strExeName, TEXT( '.' ) );
        if( strLastSlash )
            *strLastSlash = 0;
    }

    // Search in directories:
    //      .\
    //      %EXE_DIR%\..\..\%EXE_NAME%

    wcscpy_s( strDestPath, cchDest, strFilename );
    if( GetFileAttributes( strDestPath ) != 0xFFFFFFFF )
        return S_OK;

    swprintf_s( strDestPath, cchDest, L"%s\\..\\..\\%s\\%s", strExePath, strExeName, strFilename );
    if( GetFileAttributes( strDestPath ) != 0xFFFFFFFF )
        return S_OK;    

    // On failure, return the file as the path but also return an error code
    wcscpy_s( strDestPath, cchDest, strFilename );

    return E_FAIL;
}