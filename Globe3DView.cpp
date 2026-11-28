// 이 MFC 샘플 소스 코드는 MFC Microsoft Office Fluent 사용자 인터페이스("Fluent UI")를
// 사용하는 방법을 보여 주며, MFC C++ 라이브러리 소프트웨어에 포함된
// Microsoft Foundation Classes Reference 및 관련 전자 문서에 대해
// 추가적으로 제공되는 내용입니다.
// Fluent UI를 복사, 사용 또는 배포하는 데 대한 사용 약관은 별도로 제공됩니다.
// Fluent UI 라이선싱 프로그램에 대한 자세한 내용은
// https://go.microsoft.com/fwlink/?LinkId=238214.
//
// Copyright (C) Microsoft Corporation
// All rights reserved.

// Globe3DView.cpp: CGlobe3DView 클래스의 구현
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS는 미리 보기, 축소판 그림 및 검색 필터 처리기를 구현하는 ATL 프로젝트에서 정의할 수 있으며
// 해당 프로젝트와 문서 코드를 공유하도록 해 줍니다.
#ifndef SHARED_HANDLERS
#include "Globe3D.h"
#endif

#include "Globe3DDoc.h"
#include "Globe3DView.h"
#include <gl/GLU.h>
#include <exception>
#include <afxdlgs.h>
#include <cstdio>
#include "TextureLoader.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CGlobe3DView

IMPLEMENT_DYNCREATE(CGlobe3DView, CView)

BEGIN_MESSAGE_MAP(CGlobe3DView, CView)
	// 표준 인쇄 명령입니다.
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CGlobe3DView::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
    ON_WM_RBUTTONDOWN()
	ON_WM_CREATE()
    ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_KEYDOWN()
	ON_WM_CAPTURECHANGED()
	ON_WM_MOUSELEAVE()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()

END_MESSAGE_MAP()

// CGlobe3DView 생성/소멸

CGlobe3DView::CGlobe3DView() noexcept
{
	// TODO: 여기에 생성 코드를 추가합니다.

}

CGlobe3DView::~CGlobe3DView()
{
	ReleaseGL();
}

BOOL CGlobe3DView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: CREATESTRUCT cs를 수정하여 여기에서
	//  Window 클래스 또는 스타일을 수정합니다.

	if (!CView::PreCreateWindow(cs))
		return FALSE;

	cs.style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	cs.lpszClass = AfxRegisterWndClass(CS_OWNDC | CS_HREDRAW | CS_VREDRAW,
		::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);

	return cs.lpszClass != nullptr;
}

// CGlobe3DView 그리기

void CGlobe3DView::OnDraw(CDC* pDC)
{
	CGlobe3DDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	if (!pDoc)
		return;

	// TODO: 여기에 원시 데이터에 대한 그리기 코드를 추가합니다.
    if (!pDC->IsPrinting())
        Render();

}


// CGlobe3DView 인쇄


void CGlobe3DView::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

BOOL CGlobe3DView::OnPreparePrinting(CPrintInfo* pInfo)
{
	// 기본적인 준비
	return DoPreparePrinting(pInfo);
}

void CGlobe3DView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: 인쇄하기 전에 추가 초기화 작업을 추가합니다.
}

void CGlobe3DView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: 인쇄 후 정리 작업을 추가합니다.
}

void CGlobe3DView::OnRButtonDown(UINT flags, CPoint point)
{
    BeginNavigation(MK_RBUTTON, flags, point);
}

void CGlobe3DView::OnRButtonUp(UINT, CPoint point)
{
    const bool menu = mouseButton_ == MK_RBUTTON && !mouseMoved_;
    EndNavigation(MK_RBUTTON);
    if (menu)
    {
        ClientToScreen(&point);
        OnContextMenu(this, point);
    }
}

void CGlobe3DView::OnContextMenu(CWnd*, CPoint point)
{
    ShowSyncBaseMapMenu(point);
}


// CGlobe3DView 진단

#ifdef _DEBUG
void CGlobe3DView::AssertValid() const
{
	CView::AssertValid();
}

void CGlobe3DView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CGlobe3DDoc* CGlobe3DView::GetDocument() const // 디버그되지 않은 버전은 인라인으로 지정됩니다.
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CGlobe3DDoc)));
	return (CGlobe3DDoc*)m_pDocument;
}
#endif //_DEBUG


// CGlobe3DView 메시지 처리기

int CGlobe3DView::OnCreate(LPCREATESTRUCT cs)
{
    if (CView::OnCreate(cs) == -1)
        return -1;
    try
    {
        if (!InitGL())
            throw std::runtime_error("OpenGL 초기화 실퍠");

        heights_ = geo::syntheticGrid();
        globeList_ = CompileMesh(geo::globe());
        RebuildTerrain();
        InitHudFont();
        wchar_t executable[32768]={};
        GetModuleFileNameW(nullptr,executable,32768);
        LoadBaseMapXml(std::filesystem::path(executable).parent_path()/L"base-map.xml");
        // Base-map imagery is loaded exclusively from base-map.xml.
        UpdateTitle();
    }
    catch (const std::exception&)
    {
        AfxMessageBox(L"OpenGL 초기화 또는 메시 생성 실패. 그래픽 드라이버와 픽셀 포맷을 확인하세요.");
        ReleaseGL(); return -1;
    }
    SetTimer(0x4731,150,nullptr);
    return 0;
}

bool CGlobe3DView::InitGL()
{
    dc_ = ::GetDC(m_hWnd);

    if (!dc_)
        return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int format = ChoosePixelFormat(dc_, &pfd);
    if (!format || !SetPixelFormat(dc_, format, &pfd))
        return false;

    PIXELFORMATDESCRIPTOR actual{};
    if (!DescribePixelFormat(dc_, format, sizeof(actual), &actual) ||
        actual.cDepthBits < 16 || !(actual.dwFlags & PFD_DOUBLEBUFFER))
        return false;

    rc_ = wglCreateContext(dc_);
    if (!rc_ || !wglMakeCurrent(dc_, rc_))
        return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    const GLfloat ambient[] = { 0.32f,0.32f,0.32f,1 };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glClearColor(0.018f, 0.025f, 0.055f, 1);

    return true;
}

void CGlobe3DView::ReleaseGL()
{
    if (rc_)
    {
        if (dc_ && wglMakeCurrent(dc_, rc_))
        {
            scene_.ReleaseGL();
            if (texture_)
                glDeleteTextures(1, &texture_);
            if (fontList_)
                glDeleteLists(fontList_, 96);
            if (globeList_)
                glDeleteLists(globeList_, 1);
            if (terrainList_)
                glDeleteLists(terrainList_, 1);
            wglMakeCurrent(nullptr, nullptr);
        }
        wglDeleteContext(rc_);
        rc_ = nullptr;
        globeList_ = terrainList_ = texture_ = fontList_ = 0;
    }
    if (dc_)
    {
        if (m_hWnd)
            ::ReleaseDC(m_hWnd, dc_);
        dc_ = nullptr;
    }
}

void CGlobe3DView::OnDestroy()
{
    KillTimer(0x4731);
    StopNavigation();
    ReleaseGL();

    CView::OnDestroy();
}

GLuint CGlobe3DView::CompileMesh(const geo::Mesh& mesh)
{
    GLuint id = glGenLists(1);
    if (!id)
        throw std::runtime_error("glGenLists failed");

    glNewList(id, GL_COMPILE);
    glBegin(GL_TRIANGLES);
    for (auto idx : mesh.indices)
    {
        const auto& v = mesh.vertices[idx];
        glColor3f(v.r, v.g, v.b);
        glTexCoord2d(v.u, v.v);
        glNormal3d(v.normal.x, v.normal.y, v.normal.z);
        glVertex3d(v.position.x, v.position.y, v.position.z);
    }
    glEnd();
    glEndList();

    return id;
}

void CGlobe3DView::RebuildTerrain()
{
    auto mesh = geo::terrain(heights_, exaggeration_);
    GLuint next = CompileMesh(mesh);
    if (terrainList_)
        glDeleteLists(terrainList_, 1);

    terrainList_ = next;
    terrainMesh_ = std::move(mesh);
}

BOOL CGlobe3DView::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

void CGlobe3DView::OnSize(UINT type, int cx, int cy)
{
    CView::OnSize(type, cx, cy);

    width_ = std::max(1, cx);
    height_ = std::max(1, cy);

    if (rc_)
        Invalidate(FALSE);
}

void CGlobe3DView::DrawGrid()
{
    glDisable(GL_LIGHTING); glColor3f(0.18f, 0.42f, 0.60f);
    auto point = [](double lon, double lat)
    {
        auto p = geo::position(lon, lat, 1000);
        glVertex3d(p.x, p.y, p.z);
    };

    for (int lat = -75; lat <= 75; lat += 15)
    {
        glBegin(GL_LINE_STRIP);
        for (int lon = -180; lon <= 180; ++lon)
            point(lon, lat);
        glEnd();
    }
    for (int lon = -180; lon < 180; lon += 15)
    {
        glBegin(GL_LINE_STRIP);
        for (int lat = -90; lat <= 90; ++lat)
            point(lon, lat);
        glEnd();
    }
    glEnable(GL_LIGHTING);
}

void CGlobe3DView::Render()
{
    if (!rc_ || !wglMakeCurrent(dc_, rc_))
        return;

    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Tighten depth range in globe mode; local mode requires a smaller near plane.
    //double nearPlane=local_ ? std::max(0.5,distance_*0.001) : std::max(1.0,(distance_-geo::RadiusKm)*0.25);
    //gluPerspective(45,double(width_)/height_,nearPlane,distance_+geo::RadiusKm*3);
    const double rangeKm = local_ ? distance_ : std::max(0.001, distance_ - geo::RadiusKm);
    // Keep sufficient depth precision for coincident map surfaces on a globe.
    const double nearPlane = std::max(0.0001, rangeKm * 0.1);
    const double farPlane = rangeKm + geo::RadiusKm * 3.0;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, static_cast<double>(width_) / height_, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const GLfloat light[] = { -0.3f,0.6f,1.0f,0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light); // headlight in eye coordinates

    navigationHeightM_ = scene_.ModelsVisible() ? scene_.ModelGroundHeight(lon_,lat_) :
        (local_ && !scene_.Active() ? 1000.0 * exaggeration_ : 0.0);
    const auto camera = navigation::MakeFrame(NavigationCamera());
    gluLookAt(camera.eye.x, camera.eye.y, camera.eye.z,
              camera.target.x, camera.target.y, camera.target.z,
              camera.up.x, camera.up.y, camera.up.z);
    bool textured = texture_ && textureEnabled_;
    if (textured)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture_);
        // Replace ignores per-vertex height colors and keeps the map readable.
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    }
    glCallList(globeList_);
    glPolygonMode(GL_FRONT_AND_BACK, wire_ ? GL_LINE : GL_FILL);
    if (terrainVisible_ && !scene_.Active())
        glCallList(terrainList_);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_TEXTURE_2D);
    if (grid_)
        DrawGrid();

    for(size_t i=0;i<baseMapLayers_.size();++i)
    {
        const unsigned group=baseMapLayerGroups_[i];
        if(group==0?baseMapConfig_.smallScale.visible:baseMapConfig_.world.visible)
            baseMapLayers_[i]->Draw();
    }

    scene_.DrawMaps();
    scene_.DrawModels(width_,height_);
    scene_.DrawObjects(width_,height_);
    UpdateMouseCoordinates();
    DrawHud();
    SwapBuffers(dc_);
}

navigation::Camera CGlobe3DView::NavigationCamera() const
{
    navigation::Camera camera;
    camera.longitude = lon_;
    camera.latitude = lat_;
    camera.rangeKm = local_ ? distance_ : distance_ - geo::RadiusKm;
    camera.heightM = navigationHeightM_;
    camera.heading = (local_ ? azimuth_ : 0) + perspectiveHeading_;
    camera.pitch = std::clamp((local_ ? tilt_ : 0) + perspectivePitch_, 0., 65.);
    camera.width = width_;
    camera.height = height_;
    return camera;
}

void CGlobe3DView::ApplyNavigation(const navigation::Camera& camera)
{
    lon_ = camera.longitude;
    lat_ = camera.latitude;
    distance_ = local_ ? camera.rangeKm : geo::RadiusKm + camera.rangeKm;
    perspectiveHeading_ = std::remainder(camera.heading - (local_ ? azimuth_ : 0), 360.);
    perspectivePitch_ = camera.pitch - (local_ ? tilt_ : 0);
    Invalidate(FALSE);
}

void CGlobe3DView::StopNavigation()
{
    wheelRemaining_ = 0;
    KillTimer(0x4732);
}

void CGlobe3DView::BeginNavigation(UINT button, UINT flags, CPoint point)
{
    StopNavigation();
    SetFocus();
    SetCapture();
    mouseButton_ = button;
    mouseDown_ = last_ = point;
    mouseMoved_ = false;
    mouseAction_ = button == MK_MBUTTON || (flags & MK_CONTROL) ? MouseAction::Orbit :
        button == MK_RBUTTON ? MouseAction::Zoom : MouseAction::Pan;
}

void CGlobe3DView::EndNavigation(UINT button)
{
    if(mouseButton_ != button) return;
    mouseAction_ = MouseAction::None;
    mouseButton_ = 0;
    if(GetCapture() == this) ReleaseCapture();
}

void CGlobe3DView::OnLButtonDown(UINT flags, CPoint point)
{
    BeginNavigation(MK_LBUTTON, flags, point);
}

void CGlobe3DView::OnLButtonUp(UINT, CPoint)
{
    EndNavigation(MK_LBUTTON);
}

void CGlobe3DView::OnCaptureChanged(CWnd* wnd)
{
    mouseAction_ = MouseAction::None;
    mouseButton_ = 0;
    StopNavigation();
    CView::OnCaptureChanged(wnd);
}

void CGlobe3DView::OnMouseMove(UINT flags, CPoint point)
{
    mouse_ = point;
    if(mouseAction_ != MouseAction::None && (flags & mouseButton_))
    {
        mouseMoved_ = mouseMoved_ || std::abs(point.x-mouseDown_.x)>3 || std::abs(point.y-mouseDown_.y)>3;
        if(mouseMoved_)
        {
            auto camera = NavigationCamera();
            const double dx = point.x-last_.x, dy = point.y-last_.y;
            if(mouseAction_ == MouseAction::Pan)
                navigation::Pan(camera,last_.x,last_.y,point.x,point.y);
            else if(mouseAction_ == MouseAction::Orbit)
            {
                camera.heading = std::remainder(camera.heading+dx*.25,360.);
                camera.pitch = std::clamp(camera.pitch+dy*.25,0.,65.);
            }
            else
                navigation::Zoom(camera,std::exp(dy*4./std::max(1,height_)),mouseDown_.x,mouseDown_.y);
            ApplyNavigation(camera);
            last_ = point;
        }
    }
    if(!mouseInside_)
    {
        TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT),TME_LEAVE,m_hWnd,0};
        ::TrackMouseEvent(&track);
    }
    mouseInside_ = point.x>=0 && point.y>=0 && point.x<width_ && point.y<height_;
    Invalidate(FALSE);
    CView::OnMouseMove(flags,point);
}

BOOL CGlobe3DView::OnMouseWheel(UINT, short delta, CPoint point)
{
    ScreenToClient(&point);
    wheelPoint_ = point;
    wheelRemaining_ = std::clamp(wheelRemaining_ + std::log(.8)*double(delta)/WHEEL_DELTA,-3.,3.);
    wheelTick_ = GetTickCount64();
    SetTimer(0x4732,16,nullptr);
    return TRUE;
}

void CGlobe3DView::AnimateNavigation()
{
    const auto now = GetTickCount64();
    const double dt = std::clamp(double(now-wheelTick_)/1000.,.001,.05);
    wheelTick_ = now;
    double step = wheelRemaining_ * (1-std::exp(-dt/0.075));
    if(std::abs(wheelRemaining_)<.001) step = wheelRemaining_;
    auto camera = NavigationCamera();
    navigation::Zoom(camera,std::exp(step),wheelPoint_.x,wheelPoint_.y);
    ApplyNavigation(camera);
    wheelRemaining_ -= step;
    if(std::abs(wheelRemaining_)<1e-8) StopNavigation();
}

void CGlobe3DView::OnKeyDown(UINT key, UINT repeat, UINT flags)
{
    StopNavigation();
    switch (key)
    {
    case 'M':
        GoToFirstModel();
        return;
    case '1':
        local_ = false;
        distance_ = 20000;
        lon_ = 128;
        lat_ = 36;
        break;
    case '2':
        local_ = true;
        distance_ = 1500;
        azimuth_ = 0.0;
        tilt_ = 30;
        break;
    case 'W':
        wire_ = !wire_;
        break;
    case 'G':
        grid_ = !grid_;
        break;
    case 'T':
        SelectTextureFolder();
        break;
    case 'I':
        textureEnabled_ = !textureEnabled_;
        break;
    case 'H':
        terrainVisible_ = !terrainVisible_;
        break;
    case 'E':
        if (wglMakeCurrent(dc_, rc_))
        {
            double previous = exaggeration_;
            exaggeration_ = (exaggeration_ == 1 ? 20 : 1);
            try
            {
                RebuildTerrain();
            }
            catch (const std::exception&)
            {
                exaggeration_ = previous;
                AfxMessageBox(L"지형 재생성 실패");
            }
        }
        break;
    default:
        CView::OnKeyDown(key, repeat, flags);
        return;
    }

    UpdateTitle();
    Invalidate(FALSE);
}

void CGlobe3DView::UpdateTitle()
{
    if (auto* frame = GetParentFrame())
    {
        CString title;
        title.Format(L"OpenGL GIS | %s | 지형 x%.0f | %s | T:영상폴더 I:영상 H:지형 1/2:시점",
            local_ ? L"지형 확대" : L"지구 전체", exaggeration_, textureStatus_.GetString());
        frame->SetWindowText(title);
    }
}

void CGlobe3DView::OnMouseLeave()
{
    mouseInside_ = false;
    Invalidate(FALSE);

    CView::OnMouseLeave();
}

void CGlobe3DView::InitHudFont()
{
    fontList_ = glGenLists(96);
    if (!fontList_)
        throw std::runtime_error("HUD display list creation failed");

    HFONT font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
    if (!font)
        throw std::runtime_error("HUD font creation failed");

    HGDIOBJ old = SelectObject(dc_, font);
    BOOL ok = wglUseFontBitmapsW(dc_, 32, 96, fontList_);
    SelectObject(dc_, old);
    DeleteObject(font);
    if (!ok)
        throw std::runtime_error("HUD glyph creation failed");
}

void CGlobe3DView::UpdateMouseCoordinates()
{
    coordinateText_ = "Lon: --  Lat: --  (outside globe)";
    if (!mouseInside_)
        return;

    GLdouble model[16];
    GLdouble projection[16];
    GLint viewport[4];

    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);

    geo::Vec3 nearPoint, farPoint;
    double x = mouse_.x + 0.5;
    double y = height_ - mouse_.y - 0.5;
    if (!gluUnProject(x, y, 0, model, projection, viewport, &nearPoint.x, &nearPoint.y, &nearPoint.z) ||
        !gluUnProject(x, y, 1, model, projection, viewport, &farPoint.x, &farPoint.y, &farPoint.z))
        return;

    static const geo::Mesh empty;
    auto pick = geo::pickSurface(nearPoint, geo::unit(farPoint - nearPoint), (terrainVisible_ && !scene_.Active()) ? terrainMesh_ : empty);
    if (!pick.hit)
        return;

    auto ll = geo::geographic(pick.position);
    char text[192];
    std::snprintf(text, sizeof(text), "Lon: %+.6f deg   Lat: %+.6f deg   [%s]",
        ll.lon, ll.lat, pick.terrain ? "terrain mesh" : "reference sphere");
    coordinateText_ = text;
}

void CGlobe3DView::DrawHud()
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LIST_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width_, height_, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();
    glColor3f(0.025f, 0.035f, 0.065f);
    glBegin(GL_QUADS);
    glVertex2i(0, 0);
    glVertex2i(width_, 0);
    glVertex2i(width_, 104);
    glVertex2i(0, 104);
    glEnd();
    glColor3f(0.90f, 0.96f, 1.0f);
    auto print = [this](int y, const std::string& text)
    {
        glRasterPos2i(10, y);
        glListBase(fontList_ - 32);
        glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, text.data());
    };
    print(22, coordinateText_);
    print(92, scene_.ModelStatus());
    print(70, viewSync_.IsEnabled()?"C4IMap sync: ON":"C4IMap sync: OFF");
    print(46, "T: imagery folder  I: imagery  H: terrain  1/2: view  W: wire  G: grid  E: height");
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

void CGlobe3DView::LoadTexture(const std::filesystem::path& root, bool reportError)
{
    GLuint next = 0;
    try
    {
        if (!wglMakeCurrent(dc_, rc_))
            throw std::runtime_error("No current GL context");

        GLint maxSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        auto image = LoadNaturalEarthII(root, maxSize, 1);
        while (glGetError() != GL_NO_ERROR) {}

        glGenTextures(1, &next);
        glBindTexture(GL_TEXTURE_2D, next);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());

        if (!next || glGetError() != GL_NO_ERROR)
            throw std::runtime_error("Texture upload failed");

        if (texture_)
            glDeleteTextures(1, &texture_);

        texture_ = next;
        next = 0;
        textureEnabled_ = true;
        textureStatus_.Format(L"L%d (%ux%u)", image.level, image.width, image.height);
    }
    catch (const std::exception&)
    {
        if (next)
            glDeleteTextures(1, &next);

        if (reportError)
            AfxMessageBox(L"NaturalEarthII 로드 실패. 0/0/0.jpg, 0/1/0.jpg 등을 포함하는 NaturalEarthII 폴더를 선택하세요. \
                            JPG는 256x256이어야 합니다. 기존 영상은 유지됩니다.");
    }
    UpdateTitle();
    Invalidate(FALSE);
}

void CGlobe3DView::SelectTextureFolder()
{
    CFolderPickerDialog dialog(nullptr, OFN_PATHMUSTEXIST, this, 0);
    if (dialog.DoModal() == IDOK)
        LoadTexture(std::filesystem::path(dialog.GetPathName().GetString()), true);
}

void CGlobe3DView::AutoLoadTexture()
{
    //wchar_t exe[32768];
    //DWORD count=GetModuleFileNameW(nullptr,exe,32768);
    //if(!count || count>=32768)
    //    return;

    //auto directory=std::filesystem::path(exe).parent_path();
    //for(const auto& root:{directory/L"NaturalEarthII", directory/L"Assets"/L"Textures"/L"NaturalEarthII"})
    //{
    //    std::error_code error;
    //    if(std::filesystem::is_directory(root,error))
    //        LoadTexture(root,false);

    //    if(texture_)
    //        break;
    //}

    const std::filesystem::path texturePath = L"D:/mapdata/maptile";

    std::error_code ec;
    if (!std::filesystem::is_directory(texturePath, ec))
    {
        CString message;
        message.Format(L"폴더를 찾을 수 없습니다.\n%s", texturePath.c_str());
        AfxMessageBox(message);

        return;
    }

    LoadTexture(texturePath, true);
}

/*
void CGlobe3DView::OpenShapeFile()
{
    CFileDialog dialog(
        TRUE,
        L"shp",
        nullptr,
        OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
        L"Shapefile (*.shp)|*.shp||",
        this);

    if (dialog.DoModal() != IDOK)
        return;

    try
    {
        const CString filename = dialog.GetPathName();

        // 한글 경로를 GDAL에 전달하기 위한 UTF-8 변환
        CW2A utf8(filename.GetString(), CP_UTF8);

        shapeLayer_.Load(
            std::string(static_cast<const char*>(utf8)));

        Invalidate(FALSE);
    }
    catch (const std::exception& e)
    {
        CA2W message(e.what(), CP_UTF8);

        AfxMessageBox(
            static_cast<const wchar_t*>(message));
    }
}
*/

void CGlobe3DView::LoadBaseMapXml(const std::filesystem::path& filename)
{
    try
    {
        const BaseMapConfig config = basemap::Load(filename);

        std::vector<std::unique_ptr<ShapeLayer>> loaded;
        std::vector<unsigned> groups;
        unsigned groupIndex=0;

        auto loadShape = [&loaded,&groups,&groupIndex](const std::filesystem::path& path, int sourceEpsg)
        {
            if (path.empty())
                return;

            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("Shape file not found: " + path.u8string());
            }

            auto layer = std::make_unique<ShapeLayer>();

            // C++17: u8string()은 std::string 반환
            layer->Load(path.u8string(), sourceEpsg);
            loaded.push_back(std::move(layer));
            groups.push_back(groupIndex);
        };

        auto loadGroup = [&loadShape](const MapGroupConfig& group)
        {
            if (!group.visible)
                return;

            if (group.path.empty())
            {
                throw std::runtime_error("Visible map group has no Path");
            }

            loadShape(group.path, group.sourceEpsg);
            loadShape(group.boundaryPath, group.sourceEpsg);
            loadShape(group.cityPath, group.sourceEpsg);
        };

        // visual=true인 그룹만 로딩
        loadGroup(config.smallScale);
        groupIndex=1;
        loadGroup(config.world);

        // Shape 로드 성공 후 기존 레이어 교체
        baseMapLayers_.swap(loaded);
        baseMapLayerGroups_.swap(groups);
        baseMapGroupLoaded_[0]=config.smallScale.visible;
        baseMapGroupLoaded_[1]=config.world.visible;
        baseMapConfig_ = config;

        // 영상 표시 여부 적용
        textureEnabled_ = false;

        if (config.naturalEarth.visible)
        {
            if (config.naturalEarth.path.empty())
            {
                AfxMessageBox(L"NaturalEarth의 Path가 비어 있습니다.");
            }
            else
            {
                // 기존 함수: 성공하면 textureEnabled_=true 설정
                LoadTexture(config.naturalEarth.path, true);
            }
        }

        Invalidate(FALSE);
    }
    catch (const std::exception& e)
    {
        CA2W message(e.what(), CP_UTF8);
        AfxMessageBox(static_cast<const wchar_t*>(message));
    }
}


void CGlobe3DView::OnMButtonDown(UINT flags, CPoint point)
{
    BeginNavigation(MK_MBUTTON, flags, point);
}

void CGlobe3DView::OnMButtonUp(UINT, CPoint)
{
    EndNavigation(MK_MBUTTON);
}
