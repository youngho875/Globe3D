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

// Globe3DView.h: CGlobe3DView 클래스의 인터페이스
//

#pragma once
#include <afxwin.h>
#include <gl/GL.h>
#include "Geometry.h"
#include "../MapViewSync/MapViewSync.h"
#include "../MapViewSync/MapSceneSync.h"
#include "GlobeScene.h"
#include "BaseMapConfig.h"
#include "ShapeLayer.h"
#include <filesystem>
#include <string>
#include <memory>
#include <vector>
#include <atlconv.h>


class CGlobe3DView : public CView
{
protected: // serialization에서만 만들어집니다.
    CGlobe3DView() noexcept;
	DECLARE_DYNCREATE(CGlobe3DView)

// 특성입니다.
public:
	CGlobe3DDoc* GetDocument() const;

private:
    //ShapeLayer baseMapLayers_;
    //void OpenShapeFile();
    CMapViewSync viewSync_{1};
    CMapSceneSync sceneSync_;
    CGlobeScene scene_;
    std::vector<unsigned> baseMapLayerGroups_;
    bool baseMapGroupLoaded_[2] = {false,false};
    MapViewSyncState CaptureSyncState() const;
    void SetSyncBaseVisible(unsigned index,bool visible);
    void ShowSyncBaseMapMenu(CPoint point);
    afx_msg void OnTimer(UINT_PTR id);
    BaseMapConfig baseMapConfig_;
    std::vector<std::unique_ptr<ShapeLayer>> baseMapLayers_;

    void LoadBaseMapXml(const std::filesystem::path& filename);

private:
    HDC dc_ = nullptr;
    HGLRC rc_ = nullptr;
    GLuint globeList_ = 0;
    GLuint terrainList_ = 0;
    GLuint texture_ = 0;
    GLuint fontList_ = 0;
    bool textureEnabled_ = true;
    bool terrainVisible_ = false;
    bool mouseInside_ = false;
    CPoint mouse_;
    geo::Mesh terrainMesh_;
    CString textureStatus_ = L"NaturalEarthII 미설정 (T:폴더 선택)";
    std::string coordinateText_ = "Lon: --  Lat: --";
    int width_ = 1, height_ = 1;
    bool dragging_ = false;
    bool local_ = false;
    bool wire_ = false;
    bool grid_ = false;
    CPoint last_;
    bool middleDragging_ = false;
    CPoint middleLast_;
    double perspectivePitch_ = 0.0;
    double perspectiveHeading_ = 0.0;

    double lon_ = 128;
    double lat_ = 36;
    double distance_ = 20000;
    double azimuth_ = 0.0;
    double tilt_ = 30;
    double exaggeration_ = 20;
    geo::HeightGrid heights_;

private:
    bool InitGL();
    void ReleaseGL();
    void RebuildTerrain();
    static GLuint CompileMesh(const geo::Mesh& mesh);
    void Render();
    void DrawGrid();
    void UpdateTitle();
    void SelectTextureFolder();
    void LoadTexture(const std::filesystem::path& path, bool reportError);
    void AutoLoadTexture();
    void InitHudFont();
    void UpdateMouseCoordinates();
    void DrawHud();

public:

// 재정의입니다.
public:
	virtual void OnDraw(CDC* pDC);  // 이 뷰를 그리기 위해 재정의되었습니다.
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

// 구현입니다.
public:
	virtual ~CGlobe3DView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// 생성된 메시지 맵 함수
protected:
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg int OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
    afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnKeyDown(UINT key, UINT repeat, UINT flags);
    afx_msg void OnMouseLeave();
    afx_msg void OnCaptureChanged(CWnd* wnd);

	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // Globe3DView.cpp의 디버그 버전
inline CGlobe3DDoc* CGlobe3DView::GetDocument() const
   { return reinterpret_cast<CGlobe3DDoc*>(m_pDocument); }
#endif

