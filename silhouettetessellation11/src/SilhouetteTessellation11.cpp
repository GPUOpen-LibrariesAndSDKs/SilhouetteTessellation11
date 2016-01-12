//
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

//--------------------------------------------------------------------------------------
// File: SilhouetteTessellation11.cpp
//
// Demonstrates two tessellation techniques: PN Triangles and Phong Tessellation. 
// Also implements culling and adaptive tess factors to further optimize these techniques.
//--------------------------------------------------------------------------------------

// DXUT now sits one directory up
#include "..\\..\\DXUT\\Core\\DXUT.h"
#include "..\\..\\DXUT\\Core\\DXUTmisc.h"
#include "..\\..\\DXUT\\Core\\DDSTextureLoader.h"
#include "..\\..\\DXUT\\Optional\\DXUTgui.h"
#include "..\\..\\DXUT\\Optional\\DXUTCamera.h"
#include "..\\..\\DXUT\\Optional\\DXUTSettingsDlg.h"
#include "..\\..\\DXUT\\Optional\\SDKmisc.h"
#include "..\\..\\DXUT\\Optional\\SDKmesh.h"

// AMD SDK also sits one directory up
#include "..\\..\\AMD_SDK\\inc\\AMD_SDK.h"

// Project includes
#include "resource.h"
#include <map>

#pragma warning(disable: 4100)

//-----------------------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------------------
static const int TEXT_LINE_HEIGHT = 15;

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------

// The different meshes
typedef enum _MESH_TYPE
{
    MESH_TYPE_MUSHROOMS = 0,
    MESH_TYPE_TIGER     = 1,
    MESH_TYPE_TEAPOT    = 2,
	MESH_TYPE_ICOSPHERE = 3,
    MESH_TYPE_USER      = 4,
    MESH_TYPE_MAX       = 5,
}MESH_TYPE;

typedef enum _TESSELLATION_COMBO_METHOD_TYPE
{
	TESSELLATION_COMBO_NO_TESSELLATION = 0,
	TESSELLATION_COMBO_PN_TESSELLATION = 1,
	TESSELLATION_COMBO_PHONG_TESSELLATION = 2
}TESSELLATION_COMBO_METHOD_TYPE;

typedef enum _TESSELLATION_SETTING_TYPE
{
    // enables tessellation factors based on:
    SS_ADAPT        = 1,    // an ideat primitive size
	DIST_ADAPT      = 2,    // distance
	RES_ADAPT       = 4,    // screen resolution
	ORIENT_ADAPT    = 8,    // orientation with respect to the viewing vector

    // culling type
	BF_CULL         = 32,   // use back face culling
	FRUST_CULL      = 64,   // use view frustum culling

    // select tessellation technique
	PHONG           = 128,  // use phong 
	PNTRI           = 256,  // use PN triangles 
}
TESSELLATION_SETTING_TYPE;

CDXUTDialogResourceManager  g_DialogResourceManager;    // Manager for shared resources of dialogs
CFirstPersonCamera          g_Camera;    // A model viewing camera for each mesh scene
CDXUTDirectionWidget        g_Light;                    // Dynamic Light
CDXUTTextHelper*            g_pTxtHelper = NULL;

// The scene meshes 
CDXUTSDKMesh                g_SceneMesh[MESH_TYPE_MAX];
static ID3D11InputLayout*   g_pSceneVertexLayout = NULL;
static ID3D11InputLayout*   g_pSceneVertexLayoutTess = NULL;
MESH_TYPE                   g_eMeshType = MESH_TYPE_MUSHROOMS;
DirectX::XMMATRIX           g_m4x4MeshMatrix[MESH_TYPE_MAX];
DirectX::XMFLOAT3           g_v3AdaptiveTessParams[MESH_TYPE_MAX];

// Samplers
ID3D11SamplerState*         g_pSamplePoint = NULL;
ID3D11SamplerState*         g_pSampleLinear = NULL;

// Shaders
ID3D11VertexShader*         g_pSceneVS = NULL;
ID3D11VertexShader*         g_pSceneWithTessellationVS = NULL;

DWORD HullShaderHash = 0;
std::map<DWORD, ID3D11HullShader*> g_HullShaders;
std::map<DWORD, ID3D11DomainShader*> g_DomainShaders;

ID3D11DomainShader*         g_pPNTrianglesDS	= NULL;
ID3D11PixelShader*          g_pScenePS			= NULL;
ID3D11PixelShader*          g_pTexturedScenePS	= NULL;

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------

// Constant buffer layout for transfering data to the PN-Triangles HLSL functions
struct CB_PNTRIANGLES
{
    DirectX::XMMATRIX f4x4World;               // World matrix for object
    DirectX::XMMATRIX f4x4ViewProjection;      // View * Projection matrix
    DirectX::XMMATRIX f4x4WorldViewProjection; // World * View * Projection matrix  
    DirectX::XMVECTOR fLightDir;                 // Light direction vector
	DirectX::XMVECTOR fEye;
    DirectX::XMVECTOR fViewVector;               // View Vector
	float fEdgeTessFactors;
	float fInsideTessFactors;
	float fMinDistance;
	float fTessRange;    
	float fScreenSize[2];             // Screen params ( x=Current width, y=Current height )

	// GUI Params

	float fGUIBackFaceEpsilon;
	float fGUISilhouetteEpsilon;
	float fGUIRangeScale;
	float fGUIEdgeSize;    
	
	float fGUIScreenResolutionScale;
	float fGUIViewFrustrumEpsilon;

    DirectX::XMFLOAT4 f4ViewFrustumPlanes[4]; // View frustum planes
};

// slot where to bind the constant buffers
UINT                    g_iPNTRIANGLESCBBind = 0;

// Various Constant buffers
static ID3D11Buffer*    g_pcbPNTriangles = NULL;                 

// State objects
ID3D11RasterizerState*   g_pRasterizerStateWireframe = NULL;
ID3D11RasterizerState*   g_pRasterizerStateSolid = NULL;

// User supplied data
static bool g_bUserMesh = false;
static ID3D11ShaderResourceView* g_pDiffuseTextureSRV = NULL;

// Tess factor
static unsigned int g_uTessFactor = 5;

// Back face culling epsilon
static float g_fBackFaceCullEpsilon = 0.5f;

// Silhoutte epsilon
static float g_fSilhoutteEpsilon = 0.25f;

// Range scale (for distance adaptive tessellation)
static float g_fRangeScale = 1.0f;

// Edge scale (for screen space adaptive tessellation)
static unsigned int g_uEdgeSize = 16; 

// Edge scale (for screen space adaptive tessellation)
static float g_fResolutionScale = 1.0f; 

// View frustum culling epsilon
static float g_fViewFrustumCullEpsilon = 0.5f;

//--------------------------------------------------------------------------------------
// AMD helper classes defined here
//--------------------------------------------------------------------------------------
AMD::ShaderCache            g_ShaderCache(AMD::ShaderCache::SHADER_AUTO_RECOMPILE_ENABLED, AMD::ShaderCache::ERROR_DISPLAY_ON_SCREEN);
static AMD::MagnifyTool     g_MagnifyTool;
static AMD::HUD             g_HUD;
static CD3DSettingsDlg      g_SettingsDlg;           // Device settings dialog

// Global boolean for HUD rendering
bool                        g_bRenderHUD = true;

//--------------------------------------------------------------------------------------
// UI control IDs
//--------------------------------------------------------------------------------------
enum 
{
	IDC_TOGGLEFULLSCREEN = 1,
	IDC_TOGGLEREF,
	IDC_CHANGEDEVICE,

// Sample UI
     IDC_STATIC_MESH                         ,
     IDC_COMBOBOX_MESH                       ,
     IDC_CHECKBOX_WIREFRAME                  ,
     IDC_CHECKBOX_TEXTURED                   ,
     IDC_COMBO_TESSELLATION                  ,
     IDC_CHECKBOX_DISTANCE_ADAPTIVE          ,
     IDC_CHECKBOX_ORIENTATION_ADAPTIVE       ,
	 IDC_STATIC_TESS_FACTOR_TITLE			 ,
     IDC_STATIC_TESS_FACTOR                  ,
     IDC_SLIDER_TESS_FACTOR                  ,
     IDC_CHECKBOX_BACK_FACE_CULL             ,
     IDC_CHECKBOX_VIEW_FRUSTUM_CULL          ,
     IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE      ,
     IDC_STATIC_BACK_FACE_CULL_EPSILON       ,
     IDC_SLIDER_BACK_FACE_CULL_EPSILON       ,
     IDC_STATIC_SILHOUTTE_EPSILON            ,
     IDC_SLIDER_SILHOUTTE_EPSILON            ,
     IDC_STATIC_CULLING_TECHNIQUES           ,
     IDC_STATIC_ADAPTIVE_TECHNIQUES          ,
     IDC_STATIC_RANGE_SCALE                  ,
     IDC_SLIDER_RANGE_SCALE                  ,
     IDC_STATIC_EDGE_SIZE                    ,
     IDC_SLIDER_EDGE_SIZE                    ,
     IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE ,
     IDC_STATIC_SCREEN_RESOLUTION_SCALE      ,
     IDC_SLIDER_SCREEN_RESOLUTION_SCALE      ,
     IDC_STATIC_RENDER_SETTINGS              ,
     IDC_STATIC_VIEW_FRUSTUM_CULL_EPSILON    ,
     IDC_SLIDER_VIEW_FRUSTUM_CULL_EPSILON    ,
};


//--------------------------------------------------------------------------------------
// Forward declarations 
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing, void* pUserContext );
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext );
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext );
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext );
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext );

bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext );
void CALLBACK OnD3D11DestroyDevice( void* pUserContext );
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime, float fElapsedTime, void* pUserContext );

void InitApp();
void RenderText();
void RenderMesh( CDXUTSDKMesh* pDXUTMesh, UINT uMesh, 
                 D3D11_PRIMITIVE_TOPOLOGY PrimType = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED, 
                 UINT uDiffuseSlot = INVALID_SAMPLER_SLOT, UINT uNormalSlot = INVALID_SAMPLER_SLOT,
                 UINT uSpecularSlot = INVALID_SAMPLER_SLOT );
bool FileExists( WCHAR* pFileName );
void CreateHullShader();
void NormalizePlane( DirectX::XMVECTOR* pPlaneEquation );
void ExtractPlanesFromFrustum( DirectX::XMFLOAT4* pPlaneEquation, DirectX::XMMATRIX* pMatrix );
HRESULT AddShadersToCache();
void SetShaderFromUI();
//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // DXUT will create and use the best device (either D3D9 or D3D11) 
    // that is available on the system depending on which D3D callbacks are set below

    // Set DXUT callbacks
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackKeyboard( OnKeyboard );
    DXUTSetCallbackFrameMove( OnFrameMove );
    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings );

    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice );
	DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain );    
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender );

    InitApp();
    DXUTInit( true, true, NULL ); // Parse the command line, show msgboxes on error, no extra command line params
    DXUTSetCursorSettings( true, true );

	DXUTCreateWindow( L"SilhouetteTessellation11 v1.1");
    DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, 1920,1080);

    DXUTMainLoop(); // Enter into the DXUT render loop

	// Ensure the ShaderCache aborts if in a lengthy generation process
	g_ShaderCache.Abort();

    return DXUTGetExitCode();
}


//--------------------------------------------------------------------------------------
// Initialize the app 
//--------------------------------------------------------------------------------------
void InitApp()
{
    D3DCOLOR DlgColor = 0x88888888; // Semi-transparent background for the dialog

    g_SettingsDlg.Init( &g_DialogResourceManager );
    g_HUD.m_GUI.Init( &g_DialogResourceManager );
    g_HUD.m_GUI.SetBackgroundColors( DlgColor );
    g_HUD.m_GUI.SetCallback( OnGUIEvent );

    int iY = AMD::HUD::iElementDelta;

    g_HUD.m_GUI.AddButton( IDC_TOGGLEFULLSCREEN, L"Toggle full screen", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight );
    g_HUD.m_GUI.AddButton( IDC_TOGGLEREF, L"Toggle REF (F3)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F3 );
    g_HUD.m_GUI.AddButton( IDC_CHANGEDEVICE, L"Change device (F2)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F2 );

    iY += AMD::HUD::iGroupDelta;
  
    // Render Settings
    g_HUD.m_GUI.AddStatic( IDC_STATIC_RENDER_SETTINGS, L"-Render Settings-", AMD::HUD::iElementOffset + 5, iY, 108, 24 );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_MESH, L"Mesh:", AMD::HUD::iElementOffset, iY += 25, 55, 24 );
    CDXUTComboBox *pCombo;
    g_HUD.m_GUI.AddComboBox( IDC_COMBOBOX_MESH, 50 + AMD::HUD::iElementOffset, iY, 150, 24, 0, true, &pCombo );
    if( pCombo )
    {
        pCombo->SetDropHeight( 45 );
        pCombo->AddItem( L"Mushrooms", NULL );
        pCombo->AddItem( L"Tiger", NULL );
        pCombo->AddItem( L"Teapot", NULL );
		pCombo->AddItem( L"Icosphere", NULL );
        pCombo->SetSelectedByIndex( 0 );
    }
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_WIREFRAME, L"Wireframe", AMD::HUD::iElementOffset, iY += 25, 140, 24, false );
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_TEXTURED, L"Textured", AMD::HUD::iElementOffset, iY += 25, 140, 24, true );
    CDXUTComboBox *pComboTess;
	g_HUD.m_GUI.AddComboBox( IDC_COMBO_TESSELLATION, AMD::HUD::iElementOffset, iY+=25, 200, 24, 0, true, &pComboTess );
    if( pComboTess )
    {
        pComboTess->SetDropHeight( 34 );
        pComboTess->AddItem( L"No tessellation", NULL );
        pComboTess->AddItem( L"PN tessellation", NULL );
        pComboTess->AddItem( L"Phong tessellation", NULL );
        pComboTess->SetSelectedByIndex( 2 );
    }
    WCHAR szTemp[256];
    
    // Tess factor
	g_HUD.m_GUI.AddStatic( IDC_STATIC_TESS_FACTOR_TITLE, L"Global Tess Factor", AMD::HUD::iElementOffset + 5, iY += 50, 108, 24 );
    swprintf_s( szTemp, L"%d", g_uTessFactor );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_TESS_FACTOR, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_TESS_FACTOR, AMD::HUD::iElementOffset, iY, 120, 24, 1, 8, 1 + ( g_uTessFactor - 1 ) / 2, false );
    
    // Culling Techniques
    g_HUD.m_GUI.AddStatic( IDC_STATIC_CULLING_TECHNIQUES, L"-Culling Techniques-", AMD::HUD::iElementOffset + 5, iY += 50, 108, 24 );
    
    // Back face culling
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_BACK_FACE_CULL, L"Back Face Cull", AMD::HUD::iElementOffset, iY += 30, 140, 24, false );
    swprintf_s( szTemp, L"%.2f", g_fBackFaceCullEpsilon );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_BACK_FACE_CULL_EPSILON, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_BACK_FACE_CULL_EPSILON, AMD::HUD::iElementOffset, iY, 120, 24, 0, 100, (unsigned int)( g_fBackFaceCullEpsilon * 100.0f ), false );
    
    // View frustum culling
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_VIEW_FRUSTUM_CULL, L"View Frustum Cull", AMD::HUD::iElementOffset, iY += 30, 140, 24, false );
    swprintf_s( szTemp, L"%.2f", g_fViewFrustumCullEpsilon );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_VIEW_FRUSTUM_CULL_EPSILON, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_VIEW_FRUSTUM_CULL_EPSILON, AMD::HUD::iElementOffset, iY, 120, 24, 0, 100, (unsigned int)( g_fViewFrustumCullEpsilon * 100.0f ), false );
        
    // Adaptive Techniques
    g_HUD.m_GUI.AddStatic( IDC_STATIC_ADAPTIVE_TECHNIQUES, L"-Adaptive Techniques-", AMD::HUD::iElementOffset + 5, iY += 50, 108, 24 );
    
    // Screen space adaptive
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE, L"Screen Space Edge Size", AMD::HUD::iElementOffset, iY += 30, 140, 24, false ); 
    swprintf_s( szTemp, L"%d", g_uEdgeSize );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_EDGE_SIZE, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_EDGE_SIZE, AMD::HUD::iElementOffset, iY, 120, 24, 1, 100, (unsigned int)( g_uEdgeSize ), false );
    
    // Distance adaptive
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE, L"Distance", AMD::HUD::iElementOffset, iY += 30, AMD::HUD::iElementOffset + 140, 24, false ); 
    swprintf_s( szTemp, L"%.2f", g_fRangeScale );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_RANGE_SCALE, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_RANGE_SCALE, AMD::HUD::iElementOffset, iY, 120, 24, 0, 100, (unsigned int)( g_fRangeScale * 50.0f ), false );
    
    // Screen resolution adaptive
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE, L"Screen Resolution", AMD::HUD::iElementOffset, iY += 30, 140, 24, false );
    swprintf_s( szTemp, L"%.2f", g_fResolutionScale );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_SCREEN_RESOLUTION_SCALE, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_SCREEN_RESOLUTION_SCALE, AMD::HUD::iElementOffset, iY, 120, 24, 0, 100, (unsigned int)( g_fResolutionScale * 50.0f ), false );
    
    // Orientation adaptive
    g_HUD.m_GUI.AddCheckBox( IDC_CHECKBOX_ORIENTATION_ADAPTIVE, L"Orientation", AMD::HUD::iElementOffset, iY += 30, 140, 24, false );
    swprintf_s( szTemp, L"%.2f", g_fSilhoutteEpsilon );
    g_HUD.m_GUI.AddStatic( IDC_STATIC_SILHOUTTE_EPSILON, szTemp, AMD::HUD::iElementOffset + 140, iY += 25, 108, 24 );
    g_HUD.m_GUI.AddSlider( IDC_SLIDER_SILHOUTTE_EPSILON, AMD::HUD::iElementOffset, iY, 120, 24, 0, 100, (unsigned int)( g_fSilhoutteEpsilon * 100.0f ), false );

	SetShaderFromUI();

	iY += AMD::HUD::iGroupDelta;

    // Add the magnify tool UI to our HUD	
    g_MagnifyTool.InitApp( &g_HUD.m_GUI, iY, true );
}


//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont interface for 
// efficient text rendering.
//--------------------------------------------------------------------------------------
void RenderText()
{
    g_pTxtHelper->Begin();
    g_pTxtHelper->SetInsertionPos( 5, 5 );
    g_pTxtHelper->SetForegroundColor( DirectX::XMFLOAT4( 1.0f, 1.0f, 0.0f, 1.0f ) );
    g_pTxtHelper->DrawTextLine( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
    g_pTxtHelper->DrawTextLine( DXUTGetDeviceStats() );

    float fEffectTime = (float)TIMER_GetTime( Gpu, L"Effect" ) * 1000.0f;
	WCHAR wcbuf[256];
	swprintf_s( wcbuf, 256, L"Effect cost in miliseconds( Total = %.3f )", fEffectTime );
	g_pTxtHelper->DrawTextLine( wcbuf );

    g_pTxtHelper->SetInsertionPos( 5, DXUTGetDXGIBackBufferSurfaceDesc()->Height - AMD::HUD::iElementDelta );
	g_pTxtHelper->DrawTextLine( L"Toggle GUI    : F1" );

    g_pTxtHelper->End();
}


//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext )
{
    return true;
}

//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext )
{
    HRESULT hr = S_OK;
    
    ID3D11DeviceContext* pd3dImmediateContext = DXUTGetD3D11DeviceContext();
    V_RETURN( g_DialogResourceManager.OnD3D11CreateDevice( pd3dDevice, pd3dImmediateContext ) );
    V_RETURN( g_SettingsDlg.OnD3D11CreateDevice( pd3dDevice ) );
    g_pTxtHelper = new CDXUTTextHelper( pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, TEXT_LINE_HEIGHT );

    // Setup constant buffer
    D3D11_BUFFER_DESC Desc;
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Desc.MiscFlags = 0;    
    Desc.ByteWidth = sizeof( CB_PNTRIANGLES );
    V_RETURN( pd3dDevice->CreateBuffer( &Desc, NULL, &g_pcbPNTriangles ) );
	
    // Setup the mesh params for adaptive tessellation
    g_v3AdaptiveTessParams[MESH_TYPE_MUSHROOMS].x    = 1.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_MUSHROOMS].y    = 100.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_MUSHROOMS].z    = 100.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_TIGER].x   = 1.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_TIGER].y   = 10.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_TIGER].z    = 4.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_TEAPOT].x  = 1.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_TEAPOT].y  = 10.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_TEAPOT].z    = 4.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_ICOSPHERE].x  = 1.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_ICOSPHERE].y  = 10.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_ICOSPHERE].z    = 4.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_USER].x    = 1.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_USER].y    = 10.0f;
    g_v3AdaptiveTessParams[MESH_TYPE_USER].z    = 3.0f;

    // Setup the matrix for each mesh
    g_m4x4MeshMatrix[MESH_TYPE_MUSHROOMS] = DirectX::XMMatrixScaling(.1f,.1f,.1f);
    g_m4x4MeshMatrix[MESH_TYPE_TIGER] = DirectX::XMMatrixRotationX( -DirectX::XM_PI / 36 ) * DirectX::XMMatrixRotationY(  DirectX::XM_PI / 4 );
    g_m4x4MeshMatrix[MESH_TYPE_TEAPOT] = DirectX::XMMatrixIdentity();
    g_m4x4MeshMatrix[MESH_TYPE_ICOSPHERE] = DirectX::XMMatrixIdentity();	
    g_m4x4MeshMatrix[MESH_TYPE_USER] = DirectX::XMMatrixRotationX(  DirectX::XM_PI / 2 ); 
	
    // Load the standard scene meshes
    WCHAR str[MAX_PATH];
    V_RETURN( DXUTFindDXSDKMediaFileCch( str, MAX_PATH, L"mushrooms\\mushrooms.sdkmesh"));
    hr = g_SceneMesh[MESH_TYPE_MUSHROOMS].Create( pd3dDevice, str );
    assert( D3D_OK == hr );
	
    V_RETURN( DXUTFindDXSDKMediaFileCch( str, MAX_PATH,  L"tiger\\tiger.sdkmesh" ) );
    hr = g_SceneMesh[MESH_TYPE_TIGER].Create( pd3dDevice, str );
    assert( D3D_OK == hr );

    V_RETURN( DXUTFindDXSDKMediaFileCch( str, MAX_PATH, L"teapot\\teapot.sdkmesh" ) );
    hr = g_SceneMesh[MESH_TYPE_TEAPOT].Create( pd3dDevice, str );
    assert( D3D_OK == hr );

    V_RETURN( DXUTFindDXSDKMediaFileCch( str, MAX_PATH, L"icosphere\\icosphere.sdkmesh" ) );
    hr = g_SceneMesh[MESH_TYPE_ICOSPHERE].Create( pd3dDevice, str );
    assert( D3D_OK == hr );

    // Load a user mesh and textures if present
    g_bUserMesh = false;
    g_pDiffuseTextureSRV = NULL;
    // The mesh

    str[0] = 0;
    hr = DXUTFindDXSDKMediaFileCch( str, MAX_PATH, L"user\\user.sdkmesh" ) ;
    if( FileExists( str ) )
    {
        hr = g_SceneMesh[MESH_TYPE_USER].Create( pd3dDevice, str );
        assert( D3D_OK == hr );
        g_bUserMesh = true;

        // add the User choice to the dropdown combo box
        CDXUTComboBox *pCombo = g_HUD.m_GUI.GetComboBox( IDC_COMBOBOX_MESH );
        if( pCombo )
        {
            int index = pCombo->GetSelectedIndex();
            pCombo->AddItem( L"User", NULL );
            pCombo->SetSelectedByIndex( index );
        }
    }
	
	// The user textures
    if( FileExists( L"..\\media\\user\\diffuse.dds" ) )
    {
		hr = DirectX::CreateDDSTextureFromFile(pd3dDevice, L"..\\media\\user\\diffuse.dds", NULL, &g_pDiffuseTextureSRV );
        assert( D3D_OK == hr );
    }
        
    // Create sampler states for point and linear
    // Point
    D3D11_SAMPLER_DESC SamDesc;
    SamDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    SamDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamDesc.MipLODBias = 0.0f;
    SamDesc.MaxAnisotropy = 1;
    SamDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    SamDesc.BorderColor[0] = SamDesc.BorderColor[1] = SamDesc.BorderColor[2] = SamDesc.BorderColor[3] = 0;
    SamDesc.MinLOD = 0;
    SamDesc.MaxLOD = D3D11_FLOAT32_MAX;
    V_RETURN( pd3dDevice->CreateSamplerState( &SamDesc, &g_pSamplePoint ) );
    // Linear
    SamDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    SamDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    SamDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    SamDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    V_RETURN( pd3dDevice->CreateSamplerState( &SamDesc, &g_pSampleLinear ) );

    // Set the raster state
    // Wireframe
    D3D11_RASTERIZER_DESC RasterizerDesc;
    RasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
    RasterizerDesc.CullMode = D3D11_CULL_NONE;
    RasterizerDesc.FrontCounterClockwise = FALSE;
    RasterizerDesc.DepthBias = 0;
    RasterizerDesc.DepthBiasClamp = 0.0f;
    RasterizerDesc.SlopeScaledDepthBias = 0.0f;
    RasterizerDesc.DepthClipEnable = TRUE;
    RasterizerDesc.ScissorEnable = FALSE;
    RasterizerDesc.MultisampleEnable = FALSE;
    RasterizerDesc.AntialiasedLineEnable = FALSE;
    V_RETURN( pd3dDevice->CreateRasterizerState( &RasterizerDesc, &g_pRasterizerStateWireframe ) );
    // Solid
    RasterizerDesc.FillMode = D3D11_FILL_SOLID;
    V_RETURN( pd3dDevice->CreateRasterizerState( &RasterizerDesc, &g_pRasterizerStateSolid ) );


    // Create AMD_SDK resources here
    g_HUD.OnCreateDevice( pd3dDevice );
    g_MagnifyTool.OnCreateDevice( pd3dDevice );
    TIMER_Init( pd3dDevice )
	
	// Generate shaders ( this is an async operation - call AMD::ShaderCache::ShadersReady() to find out if they are complete ) 
    static bool bFirstPass = true;
    if( bFirstPass )
    {
		// Setup the camera
		g_Camera.SetRotateButtons( true, false, false );
		g_Camera.SetEnablePositionMovement( true );
		g_Camera.SetViewParams( DirectX::XMVectorSet( 0.0f, 0.0f, -3.5f, 1.0f ), DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ) );
		g_Camera.SetScalers( 0.005f, 10.0f );

		// Create light object
		g_Light.StaticOnD3D11CreateDevice( pd3dDevice, pd3dImmediateContext );

		// Add the applications shaders to the cache
		AddShadersToCache();
		g_ShaderCache.GenerateShaders( AMD::ShaderCache::CREATE_TYPE_COMPILE_CHANGES );    // Only compile shaders that have changed (development mode)
		bFirstPass = false;
	}

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    HRESULT hr = S_OK;

    V_RETURN( g_DialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
    V_RETURN( g_SettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
	
    // Setup the camera's projection parameters
    float fAspectRatio = pBackBufferSurfaceDesc->Width / ( FLOAT )pBackBufferSurfaceDesc->Height;
	g_Camera.SetProjParams( DirectX::XM_PI / 4, fAspectRatio, 0.1f, 1000.0f );

	// Setup the light
    g_Light.SetRadius( 10.0f );
    g_Light.SetLightDirection( DirectX::XMFLOAT3( 0.0067f, 0.067f, -0.79f ) );
    g_Light.SetButtonMask( MOUSE_RIGHT_BUTTON );

    // Set the location and size of the AMD standard HUD
    g_HUD.m_GUI.SetLocation( pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0 );
    g_HUD.m_GUI.SetSize( AMD::HUD::iDialogWidth, pBackBufferSurfaceDesc->Height );
    g_HUD.OnResizedSwapChain( pBackBufferSurfaceDesc );

    // Magnify tool will capture from the color buffer
    g_MagnifyTool.OnResizedSwapChain( pd3dDevice, pSwapChain, pBackBufferSurfaceDesc, pUserContext, pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0 );
    D3D11_RENDER_TARGET_VIEW_DESC RTDesc;
    ID3D11Resource* pTempRTResource;
    DXUTGetD3D11RenderTargetView()->GetResource( &pTempRTResource );
    DXUTGetD3D11RenderTargetView()->GetDesc( &RTDesc );
    g_MagnifyTool.SetSourceResources( pTempRTResource, RTDesc.Format, DXUTGetDXGIBackBufferSurfaceDesc()->Width, DXUTGetDXGIBackBufferSurfaceDesc()->Height, DXUTGetDXGIBackBufferSurfaceDesc()->SampleDesc.Count );
    g_MagnifyTool.SetPixelRegion( 128 );
    g_MagnifyTool.SetScale( 5 );
    SAFE_RELEASE( pTempRTResource );

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Helper function that allows the app to render individual meshes of an sdkmesh
// and override the primitive topology
//--------------------------------------------------------------------------------------
void RenderMesh( CDXUTSDKMesh* pDXUTMesh, UINT uMesh, D3D11_PRIMITIVE_TOPOLOGY PrimType, 
                UINT uDiffuseSlot, UINT uNormalSlot, UINT uSpecularSlot )
{
    #define MAX_D3D11_VERTEX_STREAMS D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT

    assert( NULL != pDXUTMesh );

	SDKMESH_MESH* pMesh = pDXUTMesh->GetMesh( uMesh );

    UINT Strides[MAX_D3D11_VERTEX_STREAMS];
    UINT Offsets[MAX_D3D11_VERTEX_STREAMS];
    ID3D11Buffer* pVB[MAX_D3D11_VERTEX_STREAMS];

    if( pMesh->NumVertexBuffers > MAX_D3D11_VERTEX_STREAMS )
    {
        return;
    }

    for( UINT64 i = 0; i < pMesh->NumVertexBuffers; i++ )
    {
        pVB[i] = pDXUTMesh->GetVB11( uMesh, (UINT)i );
        Strides[i] = pDXUTMesh->GetVertexStride( uMesh, (UINT)i );
        Offsets[i] = 0;
    }

    ID3D11Buffer* pIB;
    pIB = pDXUTMesh->GetIB11( uMesh );
    DXGI_FORMAT ibFormat = pDXUTMesh->GetIBFormat11( uMesh );
    
    DXUTGetD3D11DeviceContext()->IASetVertexBuffers( 0, pMesh->NumVertexBuffers, pVB, Strides, Offsets );
    DXUTGetD3D11DeviceContext()->IASetIndexBuffer( pIB, ibFormat, 0 );

    SDKMESH_SUBSET* pSubset = NULL;
    SDKMESH_MATERIAL* pMat = NULL;

    for( UINT uSubset = 0; uSubset < pMesh->NumSubsets; uSubset++ )
    {
        pSubset = pDXUTMesh->GetSubset( uMesh, uSubset );

        if( D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED == PrimType )
        {
            PrimType = pDXUTMesh->GetPrimitiveType11( ( SDKMESH_PRIMITIVE_TYPE )pSubset->PrimitiveType );
        }
        
        DXUTGetD3D11DeviceContext()->IASetPrimitiveTopology( PrimType );

        pMat = pDXUTMesh->GetMaterial( pSubset->MaterialID );
        if( uDiffuseSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pDiffuseRV11 ) )
        {
            DXUTGetD3D11DeviceContext()->PSSetShaderResources( uDiffuseSlot, 1, &pMat->pDiffuseRV11 );
        }

        if( uNormalSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pNormalRV11 ) )
        {
            DXUTGetD3D11DeviceContext()->PSSetShaderResources( uNormalSlot, 1, &pMat->pNormalRV11 );
        }

        if( uSpecularSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pSpecularRV11 ) )
        {
            DXUTGetD3D11DeviceContext()->PSSetShaderResources( uSpecularSlot, 1, &pMat->pSpecularRV11 );
        }

        UINT IndexCount = ( UINT )pSubset->IndexCount;
        UINT IndexStart = ( UINT )pSubset->IndexStart;
        UINT VertexStart = ( UINT )pSubset->VertexStart;
        
        DXUTGetD3D11DeviceContext()->DrawIndexed( IndexCount, IndexStart, VertexStart );
    }
}

//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext )
{
    // Reset the timer at start of frame
    TIMER_Reset()

    // If the settings dialog is being shown, then render it instead of rendering the app's scene
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.OnRender( fElapsedTime );
        return;
    }       

    // Clear the backbuffer and depth stencil
    float ClearColor[4] = { 0.176f, 0.196f, 0.667f, 0.0f };
    ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
	ID3D11DepthStencilView* pDSV = DXUTGetD3D11DepthStencilView();
    pd3dImmediateContext->ClearRenderTargetView( pRTV, ClearColor );
    pd3dImmediateContext->ClearDepthStencilView( pDSV, D3D11_CLEAR_DEPTH, 1.0, 0 );
	pd3dImmediateContext->OMSetRenderTargets( 1, (ID3D11RenderTargetView *const *)&pRTV, DXUTGetD3D11DepthStencilView() );

    if( g_ShaderCache.ShadersReady() )
    {
		// Array of our samplers
		ID3D11SamplerState* ppSamplerStates[2] = { g_pSamplePoint, g_pSampleLinear };

        TIMER_Begin( 0, L"Effect" )
		
		// Get the projection & view matrix from the camera class
		DirectX::XMMATRIX mWorld =  g_m4x4MeshMatrix[g_eMeshType];
		DirectX::XMMATRIX mView = g_Camera.GetViewMatrix();
		DirectX::XMMATRIX mProj = g_Camera.GetProjMatrix();
		DirectX::XMMATRIX mWorldViewProjection = mWorld * mView * mProj;
		DirectX::XMMATRIX mViewProjection = mView * mProj;
    
		// Get the view vector.
		DirectX::XMVECTOR v3ViewVector = DirectX::XMVectorSubtract( g_Camera.GetEyePt(), g_Camera.GetLookAtPt());
		v3ViewVector = DirectX::XMVector3Normalize( v3ViewVector );

		// Calculate the plane equations of the frustum in world space
		DirectX::XMFLOAT4 f4ViewFrustumPlanes[6];
		ExtractPlanesFromFrustum( f4ViewFrustumPlanes, &mViewProjection );

		// Setup the constant buffer for the scene vertex shader
		D3D11_MAPPED_SUBRESOURCE MappedResource;
		pd3dImmediateContext->Map( g_pcbPNTriangles, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
		CB_PNTRIANGLES* pPNTrianglesCB = ( CB_PNTRIANGLES* )MappedResource.pData;
		pPNTrianglesCB->f4x4World = DirectX::XMMatrixTranspose(mWorld);
		pPNTrianglesCB->f4x4ViewProjection= DirectX::XMMatrixTranspose(mViewProjection);
		pPNTrianglesCB->f4x4WorldViewProjection= DirectX::XMMatrixTranspose(mWorldViewProjection);
		pPNTrianglesCB->fLightDir = g_Light.GetLightDirection(); 
		pPNTrianglesCB->fEye = g_Camera.GetEyePt();
		pPNTrianglesCB->fViewVector = v3ViewVector;
		pPNTrianglesCB->fEdgeTessFactors = (float)g_uTessFactor;
		pPNTrianglesCB->fInsideTessFactors = (float)g_uTessFactor;
		pPNTrianglesCB->fMinDistance = (float)g_v3AdaptiveTessParams[g_eMeshType].x;
		pPNTrianglesCB->fTessRange   = (float)g_v3AdaptiveTessParams[g_eMeshType].y;
		pPNTrianglesCB->fScreenSize[0] = (float)DXUTGetDXGIBackBufferSurfaceDesc()->Width;
		pPNTrianglesCB->fScreenSize[1] = (float)DXUTGetDXGIBackBufferSurfaceDesc()->Height;
		pPNTrianglesCB->fGUIBackFaceEpsilon = g_fBackFaceCullEpsilon;
		pPNTrianglesCB->fGUISilhouetteEpsilon = ( g_fSilhoutteEpsilon > 0.99f ) ? ( 0.99f ) : ( g_fSilhoutteEpsilon );
		pPNTrianglesCB->fGUIRangeScale = g_fRangeScale;
		pPNTrianglesCB->fGUIEdgeSize = (float)g_uEdgeSize;
		pPNTrianglesCB->fGUIScreenResolutionScale = g_fResolutionScale;
		pPNTrianglesCB->fGUIViewFrustrumEpsilon = ( ( g_fViewFrustumCullEpsilon * 2.0f ) - 1.0f ) * g_v3AdaptiveTessParams[g_eMeshType].z;
		pPNTrianglesCB->f4ViewFrustumPlanes[0] = f4ViewFrustumPlanes[0]; 
		pPNTrianglesCB->f4ViewFrustumPlanes[1] = f4ViewFrustumPlanes[1]; 
		pPNTrianglesCB->f4ViewFrustumPlanes[2] = f4ViewFrustumPlanes[2]; 
		pPNTrianglesCB->f4ViewFrustumPlanes[3] = f4ViewFrustumPlanes[3]; 
		pd3dImmediateContext->Unmap( g_pcbPNTriangles, 0 );

		pd3dImmediateContext->VSSetConstantBuffers( g_iPNTRIANGLESCBBind, 1, &g_pcbPNTriangles );
		pd3dImmediateContext->PSSetConstantBuffers( g_iPNTRIANGLESCBBind, 1, &g_pcbPNTriangles );

		// Based on app and GUI settings set a bunch of bools that guide the render
		bool bTextured = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_TEXTURED )->GetChecked() && g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_TEXTURED )->GetEnabled();
		bool bTessellation = g_HUD.m_GUI.GetComboBox( IDC_COMBO_TESSELLATION )->GetSelectedIndex() != TESSELLATION_COMBO_NO_TESSELLATION;
		        
		// VS
		pd3dImmediateContext->VSSetShader( bTessellation?g_pSceneWithTessellationVS:g_pSceneVS, NULL, 0 );
		pd3dImmediateContext->IASetInputLayout( g_pSceneVertexLayout );

		// HS
		ID3D11HullShader* pHS = NULL;
		if( bTessellation )
		{
			pd3dImmediateContext->HSSetConstantBuffers( g_iPNTRIANGLESCBBind, 1, &g_pcbPNTriangles );
			pHS = g_HullShaders[HullShaderHash];
		}
		pd3dImmediateContext->HSSetShader( pHS, NULL, 0 );    
    
		// DS
		ID3D11DomainShader* pDS = NULL;
		if( bTessellation )
		{
			pd3dImmediateContext->DSSetConstantBuffers( g_iPNTRIANGLESCBBind, 1, &g_pcbPNTriangles );
			pDS = g_DomainShaders[HullShaderHash];
		}
		pd3dImmediateContext->DSSetShader( pDS, NULL, 0 );
    
		// GS
		pd3dImmediateContext->GSSetShader( NULL, NULL, 0 );

		// PS
		ID3D11PixelShader* pPS = NULL;
		if( bTextured )
		{
			pd3dImmediateContext->PSSetSamplers( 0, 2, ppSamplerStates );
			pd3dImmediateContext->PSSetShaderResources( 0, 1, &g_pDiffuseTextureSRV );
			pPS = g_pTexturedScenePS;
		}
		else
		{
			pPS = g_pScenePS;
		}
		pd3dImmediateContext->PSSetShader( pPS, NULL, 0 );		

		// Set the rasterizer state
		pd3dImmediateContext->RSSetState( g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_WIREFRAME )->GetChecked()?g_pRasterizerStateWireframe:g_pRasterizerStateSolid );

		// Render the scene and optionally override the mesh topology and diffuse texture slot
		// Decide whether to use the user diffuse.dds
		UINT uDiffuseSlot = 0;
		if( ( g_eMeshType == MESH_TYPE_USER ) && ( NULL != g_pDiffuseTextureSRV ) )
		{
			uDiffuseSlot = INVALID_SAMPLER_SLOT;
		}
		// Decide which prim topology to use
		D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		if( bTessellation )
		{
			PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		}
		// Render the meshes    
		for( int iMesh = 0; iMesh < (int)g_SceneMesh[g_eMeshType].GetNumMeshes(); iMesh++ )
		{
			RenderMesh( &g_SceneMesh[g_eMeshType], (UINT)iMesh, PrimitiveTopology, uDiffuseSlot );
		}
		
		TIMER_End() // Effect
	}

	DXUT_BeginPerfEvent( DXUT_PERFEVENTCOLOR, L"HUD / Stats" );

    if( g_ShaderCache.ShadersReady() )
    {		
		
		// Render the HUD
        if( g_bRenderHUD )
        {
            g_MagnifyTool.Render();
            g_HUD.OnRender( fElapsedTime );
        }

        RenderText();
    }
    else
    {
        // Render shader cache progress if still processing
        g_ShaderCache.RenderProgress( g_pTxtHelper, TEXT_LINE_HEIGHT, DirectX::XMVectorSet( 1.0f, 1.0f, 0.0f, 1.0f ) );
    }
    
    DXUT_EndPerfEvent();

    static DWORD dwTimefirst = GetTickCount();
    if ( GetTickCount() - dwTimefirst > 5000 )
    {    
        OutputDebugString( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
        OutputDebugString( L"\n" );
        dwTimefirst = GetTickCount();
    }
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11ReleasingSwapChain();
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
	g_DialogResourceManager.OnD3D11DestroyDevice();
    g_SettingsDlg.OnD3D11DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();
    SAFE_DELETE( g_pTxtHelper );

	g_Light.StaticOnD3D11DestroyDevice();

    SAFE_RELEASE( g_pSceneVS );
    SAFE_RELEASE( g_pSceneWithTessellationVS );

	g_SceneMesh[MESH_TYPE_MUSHROOMS].Destroy();
	g_SceneMesh[MESH_TYPE_TIGER].Destroy();
	g_SceneMesh[MESH_TYPE_TEAPOT].Destroy();
	g_SceneMesh[MESH_TYPE_ICOSPHERE].Destroy();
	if (g_bUserMesh)
		g_SceneMesh[MESH_TYPE_USER].Destroy();

	for(auto it=g_HullShaders.begin();it!=g_HullShaders.end(); it++)
	{
		SAFE_RELEASE( it->second );
	}

	for(auto it=g_DomainShaders.begin();it!=g_DomainShaders.end(); it++)
	{
		SAFE_RELEASE( it->second );
	}

    SAFE_RELEASE( g_pPNTrianglesDS );
    SAFE_RELEASE( g_pScenePS );
    SAFE_RELEASE( g_pTexturedScenePS );
        
    SAFE_RELEASE( g_pcbPNTriangles );

    SAFE_RELEASE( g_pSceneVertexLayout );
    SAFE_RELEASE( g_pSceneVertexLayoutTess );

    SAFE_RELEASE( g_pSamplePoint );
    SAFE_RELEASE( g_pSampleLinear );

    SAFE_RELEASE( g_pRasterizerStateWireframe );
    SAFE_RELEASE( g_pRasterizerStateSolid );
    SAFE_RELEASE( g_pDiffuseTextureSRV );

    // Destroy AMD_SDK resources here
	g_ShaderCache.OnDestroyDevice();
	g_HUD.OnDestroyDevice();
    g_MagnifyTool.OnDestroyDevice();
    TIMER_Destroy()

}


//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    // For the first device created if its a REF device, optionally display a warning dialog box
    static bool s_bFirstTime = true;
    if( s_bFirstTime )
    {
        s_bFirstTime = false;

		// Disable vsync
		pDeviceSettings->d3d11.SyncInterval = 0;
    }

    // Multisample quality is always zero
    pDeviceSettings->d3d11.sd.SampleDesc.Quality = 0;

    return true;
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is used
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
    // Update the camera's position based on user input 
    g_Camera.FrameMove( fElapsedTime );
}


//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext )
{
    // Pass messages to dialog resource manager calls so GUI state is updated correctly
    *pbNoFurtherProcessing = g_DialogResourceManager.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass messages to settings dialog if its active
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.MsgProc( hWnd, uMsg, wParam, lParam );
        return 0;
    }

    // Give the dialogs a chance to handle the message first
    *pbNoFurtherProcessing = g_HUD.m_GUI.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass all remaining windows messages to camera so it can respond to user input
    g_Camera.HandleMessages( hWnd, uMsg, wParam, lParam );

	// Pass all remaining windows messages to the light Object
	g_Light.HandleMessages( hWnd, uMsg, wParam, lParam );

    return 0;
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext )
{
    if( bKeyDown )
    {
        switch( nChar )
        {
			case VK_F1:
				g_bRenderHUD = !g_bRenderHUD;
				break;
		}
    }
}

//--------------------------------------------------------------------------------------
// Handles the GUI events
//--------------------------------------------------------------------------------------
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext )
{
    WCHAR szTemp[256];
    bool bEnable;

	switch( nControlID )
    {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen();
            break;
        case IDC_TOGGLEREF:
            DXUTToggleREF();
            break;
        case IDC_CHANGEDEVICE:
            g_SettingsDlg.SetActive( !g_SettingsDlg.IsActive() );
            break;
        case IDC_SLIDER_TESS_FACTOR:
            g_uTessFactor = ( (unsigned int)((CDXUTSlider*)pControl)->GetValue() - 1 ) * 2 + 1;
            swprintf_s( szTemp, L"%d", g_uTessFactor );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_TESS_FACTOR )->SetText( szTemp );
            break;

        case IDC_COMBOBOX_MESH:
            g_eMeshType = (MESH_TYPE)((CDXUTComboBox*)pControl)->GetSelectedIndex();

			if ( g_bUserMesh && g_eMeshType == MESH_TYPE_USER )
			{
				if ( ( g_SceneMesh[g_eMeshType].GetMaterial(0)->pDiffuseRV11 == NULL || IsErrorResource( g_SceneMesh[g_eMeshType].GetMaterial(0)->pDiffuseRV11) ) && g_pDiffuseTextureSRV == NULL )
				{
					g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_TEXTURED )->SetEnabled(false);
				}
				else
				{
					g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_TEXTURED )->SetEnabled(true);
				}
			}
			else if ( g_SceneMesh[g_eMeshType].GetMaterial(0)->pDiffuseRV11 == NULL || IsErrorResource( g_SceneMesh[g_eMeshType].GetMaterial(0)->pDiffuseRV11) )
			{
				g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_TEXTURED )->SetEnabled(false);
			}
			else
			{
				g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_TEXTURED )->SetEnabled(true);
			}
            break;

        case IDC_COMBO_TESSELLATION:
			bEnable = ((CDXUTComboBox*)pControl)->GetSelectedIndex()>0;
			g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_ORIENTATION_ADAPTIVE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_TESS_FACTOR_TITLE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_TESS_FACTOR )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_TESS_FACTOR )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_BACK_FACE_CULL )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_VIEW_FRUSTUM_CULL )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_BACK_FACE_CULL_EPSILON )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_BACK_FACE_CULL_EPSILON )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_SILHOUTTE_EPSILON )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_SILHOUTTE_EPSILON )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_CULLING_TECHNIQUES )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_ADAPTIVE_TECHNIQUES )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_RANGE_SCALE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_RANGE_SCALE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_EDGE_SIZE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_EDGE_SIZE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetStatic( IDC_STATIC_SCREEN_RESOLUTION_SCALE )->SetEnabled( bEnable );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_SCREEN_RESOLUTION_SCALE )->SetEnabled( bEnable );
			SetShaderFromUI();
			break;
        case IDC_CHECKBOX_BACK_FACE_CULL:
        case IDC_CHECKBOX_VIEW_FRUSTUM_CULL:        
        case IDC_CHECKBOX_ORIENTATION_ADAPTIVE:
            SetShaderFromUI();
            break;

        case IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE:
            if( g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE )->GetChecked() )
            {
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE )->SetChecked( false );
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE )->SetChecked( false );
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE )->SetEnabled( false );
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE )->SetEnabled( false );
                g_HUD.m_GUI.GetSlider( IDC_SLIDER_RANGE_SCALE )->SetEnabled( false );
                g_HUD.m_GUI.GetSlider( IDC_SLIDER_SCREEN_RESOLUTION_SCALE )->SetEnabled( false );
                g_HUD.m_GUI.GetStatic( IDC_STATIC_RANGE_SCALE )->SetEnabled( false );
                g_HUD.m_GUI.GetStatic( IDC_STATIC_SCREEN_RESOLUTION_SCALE )->SetEnabled( false );
            }
            else
            {
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE )->SetEnabled( true );
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE )->SetEnabled( true );
                g_HUD.m_GUI.GetSlider( IDC_SLIDER_RANGE_SCALE )->SetEnabled( true );
                g_HUD.m_GUI.GetSlider( IDC_SLIDER_SCREEN_RESOLUTION_SCALE )->SetEnabled( true );
                g_HUD.m_GUI.GetStatic( IDC_STATIC_RANGE_SCALE )->SetEnabled( true );
                g_HUD.m_GUI.GetStatic( IDC_STATIC_SCREEN_RESOLUTION_SCALE )->SetEnabled( true );
            }
            SetShaderFromUI();
            break;

       	case IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE:
		case IDC_CHECKBOX_DISTANCE_ADAPTIVE:
            if( g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE )->GetChecked() ||
			    g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE )->GetChecked() )
            {
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE )->SetChecked( false );
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE )->SetEnabled( false );
                g_HUD.m_GUI.GetSlider( IDC_SLIDER_EDGE_SIZE )->SetEnabled( false );
                g_HUD.m_GUI.GetStatic( IDC_STATIC_EDGE_SIZE )->SetEnabled( false );
            }
            else
            {
                g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE )->SetEnabled( true );
                g_HUD.m_GUI.GetSlider( IDC_SLIDER_EDGE_SIZE )->SetEnabled( true );
                g_HUD.m_GUI.GetStatic( IDC_STATIC_EDGE_SIZE )->SetEnabled( true );
            }
            SetShaderFromUI();
            break;
    
        case IDC_SLIDER_BACK_FACE_CULL_EPSILON:
            g_fBackFaceCullEpsilon = (float)((CDXUTSlider*)pControl)->GetValue() / 100.0f;
            swprintf_s( szTemp, L"%.2f", g_fBackFaceCullEpsilon );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_BACK_FACE_CULL_EPSILON )->SetText( szTemp );
            break;

        case IDC_SLIDER_SILHOUTTE_EPSILON:
            g_fSilhoutteEpsilon = (float)((CDXUTSlider*)pControl)->GetValue() / 100.0f;
            swprintf_s( szTemp, L"%.2f", g_fSilhoutteEpsilon );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_SILHOUTTE_EPSILON )->SetText( szTemp );
            break;

        case IDC_SLIDER_RANGE_SCALE:
            g_fRangeScale = (float)((CDXUTSlider*)pControl)->GetValue() / 50.0f;
            swprintf_s( szTemp, L"%.2f", g_fRangeScale );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_RANGE_SCALE )->SetText( szTemp );
            break;

        case IDC_SLIDER_EDGE_SIZE:
            g_uEdgeSize = ((CDXUTSlider*)pControl)->GetValue();
            swprintf_s( szTemp, L"%d", g_uEdgeSize );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_EDGE_SIZE )->SetText( szTemp );
            break;

        case IDC_SLIDER_SCREEN_RESOLUTION_SCALE:
            g_fResolutionScale = (float)((CDXUTSlider*)pControl)->GetValue() / 50.0f;
            swprintf_s( szTemp, L"%.2f", g_fResolutionScale );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_SCREEN_RESOLUTION_SCALE )->SetText( szTemp );
            break;

        case IDC_SLIDER_VIEW_FRUSTUM_CULL_EPSILON:
            g_fViewFrustumCullEpsilon = (float)((CDXUTSlider*)pControl)->GetValue() / 50.0f;
            swprintf_s( szTemp, L"%.2f", g_fViewFrustumCullEpsilon );
            g_HUD.m_GUI.GetStatic( IDC_STATIC_VIEW_FRUSTUM_CULL_EPSILON )->SetText( szTemp );
            break;
    }

    // Call the MagnifyTool gui event handler
    g_MagnifyTool.OnGUIEvent( nEvent, nControlID, pControl, pUserContext );
}

//--------------------------------------------------------------------------------------
// Helper function to check for file existance
//--------------------------------------------------------------------------------------
bool FileExists( WCHAR* pFileName )
{
    DWORD fileAttr;    
    fileAttr = GetFileAttributes(pFileName);    
    if (0xFFFFFFFF == fileAttr)        
        return false;    
    return true;
}

//--------------------------------------------------------------------------------------
// Helper function to normalize a plane
//--------------------------------------------------------------------------------------
void NormalizePlane( DirectX::XMFLOAT4* pPlaneEquation )
{
    float mag;
    
    mag = sqrt( pPlaneEquation->x * pPlaneEquation->x + 
                pPlaneEquation->y * pPlaneEquation->y + 
                pPlaneEquation->z * pPlaneEquation->z );
    
    pPlaneEquation->x = pPlaneEquation->x / mag;
    pPlaneEquation->y = pPlaneEquation->y / mag;
    pPlaneEquation->z = pPlaneEquation->z / mag;
    pPlaneEquation->w = pPlaneEquation->w / mag;
}


//--------------------------------------------------------------------------------------
// Extract all 6 plane equations from frustum denoted by supplied matrix
//--------------------------------------------------------------------------------------
void ExtractPlanesFromFrustum( DirectX::XMFLOAT4* pPlaneEquation, DirectX::XMMATRIX* pMatrix )
{
	DirectX::XMFLOAT4X4 TempMat;
	DirectX::XMStoreFloat4x4( &TempMat, *pMatrix);


    // Left clipping plane
    pPlaneEquation[0].x = TempMat._14 + TempMat._11;
    pPlaneEquation[0].y = TempMat._24 + TempMat._21;
    pPlaneEquation[0].z = TempMat._34 + TempMat._31;
    pPlaneEquation[0].w = TempMat._44 + TempMat._41;
    
    // Right clipping plane
    pPlaneEquation[1].x = TempMat._14 - TempMat._11;
    pPlaneEquation[1].y = TempMat._24 - TempMat._21;
    pPlaneEquation[1].z = TempMat._34 - TempMat._31;
    pPlaneEquation[1].w = TempMat._44 - TempMat._41;
    
    // Top clipping plane
    pPlaneEquation[2].x = TempMat._14 - TempMat._12;
    pPlaneEquation[2].y = TempMat._24 - TempMat._22;
    pPlaneEquation[2].z = TempMat._34 - TempMat._32;
    pPlaneEquation[2].w = TempMat._44 - TempMat._42;
    
    // Bottom clipping plane
    pPlaneEquation[3].x = TempMat._14 + TempMat._12;
    pPlaneEquation[3].y = TempMat._24 + TempMat._22;
    pPlaneEquation[3].z = TempMat._34 + TempMat._32;
    pPlaneEquation[3].w = TempMat._44 + TempMat._42;
    
    // Near clipping plane
    pPlaneEquation[4].x = TempMat._13;
    pPlaneEquation[4].y = TempMat._23;
    pPlaneEquation[4].z = TempMat._33;
    pPlaneEquation[4].w = TempMat._43;
    
    // Far clipping plane
    pPlaneEquation[5].x = TempMat._14 - TempMat._13;
    pPlaneEquation[5].y = TempMat._24 - TempMat._23;
    pPlaneEquation[5].z = TempMat._34 - TempMat._33;
    pPlaneEquation[5].w = TempMat._44 - TempMat._43;
    
    // Normalize the plane equations, if requested
    NormalizePlane( &pPlaneEquation[0] );
    NormalizePlane( &pPlaneEquation[1] );
    NormalizePlane( &pPlaneEquation[2] );
    NormalizePlane( &pPlaneEquation[3] );
    NormalizePlane( &pPlaneEquation[4] );
    NormalizePlane( &pPlaneEquation[5] );
}

//--------------------------------------------------------------------------------------
// Selects the right shader given the UI settings
//--------------------------------------------------------------------------------------
void SetShaderFromUI()
{
	bool bEnable;
	HullShaderHash = 0;

	switch(g_HUD.m_GUI.GetComboBox( IDC_COMBO_TESSELLATION )->GetSelectedIndex() )
	{
		case TESSELLATION_COMBO_NO_TESSELLATION:
			HullShaderHash = 0;			
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_BACK_FACE_CULL_EPSILON )->SetEnabled( false );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_VIEW_FRUSTUM_CULL_EPSILON )->SetEnabled( false );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_EDGE_SIZE )->SetEnabled( false );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_RANGE_SCALE )->SetEnabled( false );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_SCREEN_RESOLUTION_SCALE )->SetEnabled( false );
			g_HUD.m_GUI.GetSlider( IDC_SLIDER_SILHOUTTE_EPSILON )->SetEnabled( false );
			break;

		case TESSELLATION_COMBO_PN_TESSELLATION:
			HullShaderHash |= PNTRI; break;
		case TESSELLATION_COMBO_PHONG_TESSELLATION:	
			HullShaderHash |= PHONG; break;
	}


	bEnable = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_SPACE_ADAPTIVE )->GetChecked();
	g_HUD.m_GUI.GetSlider( IDC_SLIDER_EDGE_SIZE )->SetEnabled( bEnable );
	if( bEnable )
	{
		HullShaderHash |= SS_ADAPT;
	}

	bEnable = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_DISTANCE_ADAPTIVE )->GetChecked();
	g_HUD.m_GUI.GetSlider( IDC_SLIDER_RANGE_SCALE )->SetEnabled( bEnable );
	if( bEnable )
	{
		HullShaderHash |= DIST_ADAPT;
	}

	bEnable = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_SCREEN_RESOLUTION_ADAPTIVE )->GetChecked();
	g_HUD.m_GUI.GetSlider( IDC_SLIDER_SCREEN_RESOLUTION_SCALE )->SetEnabled( bEnable );
	if ( bEnable )
	{
		HullShaderHash |= RES_ADAPT;
	}	

	bEnable = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_ORIENTATION_ADAPTIVE )->GetChecked();	
	g_HUD.m_GUI.GetSlider( IDC_SLIDER_SILHOUTTE_EPSILON )->SetEnabled( bEnable );
	if ( bEnable )
	{
		HullShaderHash |= ORIENT_ADAPT;
	}

	bEnable = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_BACK_FACE_CULL )->GetChecked();
	g_HUD.m_GUI.GetSlider( IDC_SLIDER_BACK_FACE_CULL_EPSILON )->SetEnabled( bEnable );
	if( bEnable )
	{
		HullShaderHash |= BF_CULL;
	}
	
	bEnable = g_HUD.m_GUI.GetCheckBox( IDC_CHECKBOX_VIEW_FRUSTUM_CULL )->GetChecked();
	g_HUD.m_GUI.GetSlider( IDC_SLIDER_VIEW_FRUSTUM_CULL_EPSILON )->SetEnabled( bEnable );
	if( bEnable )
	{
		HullShaderHash |= FRUST_CULL;
	}

}

//--------------------------------------------------------------------------------------
// Convert the flags into shader macros and request shader cache to compile the shader
//--------------------------------------------------------------------------------------
void Cache(DWORD flags)
{
    // PNTriangles HS
	AMD::ShaderCache::Macro ShaderMacros[] = { {L"",1}, {L"",1}, {L"",1}, {L"",1}, {L"",1}, {L"",1}, {L"",1}, {L"",1}, {L"",1} };
	int flagCount = 0;

    if (flags & SS_ADAPT)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"SS_ADAPT");

	if (flags & DIST_ADAPT)		
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"DIST_ADAPT");

	if (flags & RES_ADAPT)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"RES_ADAPT");
	
	if (flags & ORIENT_ADAPT)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"ORIENT_ADAPT");

	if (flags & BF_CULL)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"BF_CULL");
	
	if (flags & FRUST_CULL)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"FRUST_CULL");

	if (flags & PHONG)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"PHONG");	

	if (flags & PNTRI)
		wcscpy_s(ShaderMacros[flagCount++].m_wsName, L"PNTRI");		

	g_HullShaders[flags] = NULL;
	g_DomainShaders[flags] = NULL;
	auto itHull =  g_HullShaders.find(flags);
	auto itDomain =  g_DomainShaders.find(flags);
	
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&(itHull->second), AMD::ShaderCache::SHADER_TYPE_HULL, L"hs_5_0", L"HS_PNTriangles",	L"SilhouetteTessellation11.hlsl", flagCount, ShaderMacros, NULL, NULL, 0 );	
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&(itDomain->second), AMD::ShaderCache::SHADER_TYPE_DOMAIN, L"ds_5_0", L"DS_PNTriangles",	L"SilhouetteTessellation11.hlsl", flagCount, ShaderMacros, NULL, NULL, 0 );	
}

//--------------------------------------------------------------------------------------
// Adds all shaders to the shader cache
//--------------------------------------------------------------------------------------
HRESULT AddShadersToCache()
{
    HRESULT hr = S_OK;
    // Ensure all shaders (and input layouts) are released

    const D3D11_INPUT_ELEMENT_DESC Layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
	
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pSceneVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_4_0", L"VS_RenderScene",
        L"SilhouetteTessellation11.hlsl", 0, NULL, &g_pSceneVertexLayout, (D3D11_INPUT_ELEMENT_DESC*)Layout, ARRAYSIZE( Layout ) );

	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pSceneWithTessellationVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_4_0", L"VS_RenderSceneWithTessellation",
        L"SilhouetteTessellation11.hlsl", 0, NULL, &g_pSceneVertexLayoutTess, (D3D11_INPUT_ELEMENT_DESC*)Layout, ARRAYSIZE( Layout ) );

	DWORD culling[] = {0, BF_CULL, FRUST_CULL, FRUST_CULL|BF_CULL };
	DWORD tessellation[] = {PNTRI, PHONG};
	DWORD orientation[] = {0, ORIENT_ADAPT};

	for(int o=0;o<2;o++)
	{
		for(int t=0;t<2;t++)
		{
			for(int c=0;c<4;c++)
			{
				DWORD common = tessellation[t] | culling[c] | orientation[o];

				Cache(common);

				Cache(common | SS_ADAPT);

				Cache(common | DIST_ADAPT);
				Cache(common | DIST_ADAPT | RES_ADAPT);
				Cache(common | RES_ADAPT);
			}	
		}
	}

    // Main scene PS (no textures)
    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pScenePS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_4_0", L"PS_RenderScene",
        L"SilhouetteTessellation11.hlsl", 0, NULL, NULL, NULL, 0 );

    // Main scene PS (textured)
    g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pTexturedScenePS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_4_0", L"PS_RenderSceneTextured",
        L"SilhouetteTessellation11.hlsl", 0, NULL, NULL, NULL, 0 );

	return hr;
}


//--------------------------------------------------------------------------------------
// EOF.
//--------------------------------------------------------------------------------------