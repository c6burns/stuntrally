#include "pch.h"
#include "../common/RenderConst.h"
#include "../common/Def_Str.h"
#include "../common/data/SceneXml.h"
#include "../common/QTimer.h"
#ifdef SR_EDITOR
	#include "../../editor/CApp.h"
#else
	#include "../CGame.h"
#endif
#include <OgreRoot.h>
#include <OgreTerrain.h>
#include <OgreHardwarePixelBuffer.h>
#include "../../shiny/Main/Factory.hpp"
using namespace Ogre;


//  common rtt setup
void App::RenderToTex::Setup(String sName, TexturePtr pTex, String sMtr)
{
	if (!scm)  return;  // once-
	//  destroy old
	if (cam)  scm->destroyCamera(cam);
	if (nd)  scm->destroySceneNode(nd);
	delete rect;
	
	cam = scm->createCamera(sName+"C");
	cam->setPosition(Vector3(0,10,0));  cam->setOrientation(Quaternion(0.5,-0.5,0.5,0.5));
	cam->setNearClipDistance(0.5f);     cam->setFarClipDistance(500.f);
	cam->setAspectRatio(1.f);   cam->setProjectionType(PT_ORTHOGRAPHIC);
	cam->setOrthoWindow(1.f,1.f);

	rnd = pTex->getBuffer()->getRenderTarget();
	rnd->setAutoUpdated(false);  //rnd->addListener(this);
	vp = rnd->addViewport(cam);
	vp->setClearEveryFrame(true);   vp->setBackgroundColour(ColourValue(0,0,0,0));
	vp->setOverlaysEnabled(false);  vp->setSkiesEnabled(false);
	vp->setShadowsEnabled(false);   //vp->setVisibilityMask();
	//vp->setMaterialScheme("reflection");

	rect = new Rectangle2D(true);   rect->setCorners(-1,1,1,-1);
	AxisAlignedBox aab;  aab.setInfinite();
	rect->setBoundingBox(aab);  rect->setCastShadows(false);
	rect->setMaterial( sMtr );

	nd = scm->getRootSceneNode()->createChildSceneNode(sName+"N");
	nd->attachObject(rect);
}

///  blendmap setup
//  every time terrain hmap size changes
//----------------------------------------------------------------------------------------------------
const String App::sHmap = "HmapTex",
	App::sAng = "AnglesRTT", App::sBlend = "blendmapRTT",
	App::sAngMat = "anglesMat", App::sBlendMat = "blendMat";
void App::CreateBlendTex()
{
	uint size = sc->td.iTerSize-1;
	TextureManager& texMgr = TextureManager::getSingleton();
	texMgr.remove(sHmap);
	texMgr.remove(sAng);
	texMgr.remove(sBlend);

	//  Hmap tex
	hMap = texMgr.createManual( sHmap, rgDef, TEX_TYPE_2D,
		size, size, 0, PF_FLOAT32_R, TU_DYNAMIC_WRITE_ONLY); //TU_STATIC_WRITE_ONLY?
	
	//  Angles rtt
	angRT = texMgr.createManual( sAng, rgDef, TEX_TYPE_2D,
		size, size, 0, PF_FLOAT32_R, TU_RENDERTARGET);
	if (angRT.isNull())  LogO("Can't create Float32 Render Target!");
	
	//  blendmap rtt
	blRT = texMgr.createManual(	sBlend, rgDef, TEX_TYPE_2D,
		size, size, 0, PF_R8G8B8A8, TU_RENDERTARGET);

	//  rtt copy  (not needed?)
	//blMap = texMgr.createManual(
	//	"blendmapT", rgDef, TEX_TYPE_2D,
	//	size, size, 0, PF_R8G8B8A8, TU_DEFAULT);
	
	if (!bl.scm)  bl.scm = mRoot->createSceneManager(ST_GENERIC);
	if (!ang.scm)  ang.scm = mRoot->createSceneManager(ST_GENERIC);
	//ang.scm = bl.scm;

	bl.Setup("bl", blRT, sBlendMat);
	ang.Setup("ang", angRT, sAngMat);
	
	UpdBlendmap();  //
}


///  update, fill hmap texture from cpu floats
//  every terrain hmap edit
//--------------------------------------------------------------------------
void App::UpdBlendmap()
{
	//QTimer ti;  ti.update();  /// time

	size_t size = sc->td.iTerSize-1;  //!^ same as in create
	float* fHmap = terrain ? terrain->getHeightData() : sc->td.hfHeight;

	//  fill hmap  (copy to tex, full is fast)
	HardwarePixelBufferSharedPtr pt = hMap->getBuffer();
	pt->lock(HardwareBuffer::HBL_DISCARD);

	const PixelBox& pb = pt->getCurrentLock();
	float* pD = static_cast<float*>(pb.data);
	size_t aD = pb.getRowSkip() * PixelUtil::getNumElemBytes(pb.format);
	 
	register size_t j,i,a=0;
	for (j = 0; j < size; ++j)
	{
		for (i = 0; i < size; ++i)
		{	
			*pD++ = fHmap[a++];
		}
		pD += aD;  a++;  //Hmap is size+1
	}
	pt->unlock();

	//  rtt
	if (ang.rnd && bl.rnd)
	{	
		UpdLayerPars();
		
		ang.rnd->update();
		bl.rnd->update();
		//  copy from rtt to normal texture
		//HardwarePixelBufferSharedPtr b = blMap->getBuffer();
		//b->blit(pt);
		//bl.rnd->writeContentsToFile(/*PATHMANAGER::DataUser()+*/ "blend.png");
	}

	//ti.update();  /// time (1ms on 512, 4ms on 1k)
	//float dt = ti.dt * 1000.f;
	//LogO(String("::: Time Upd blendmap: ") + fToStr(dt,3,5) + " ms");
}


///  for game, wheel contacts
//--------------------------------------------------------------------------
#ifndef SR_EDITOR
void App::GetTerMtrIds()
{
	//QTimer ti;  ti.update();  /// time

	size_t size = sc->td.iTerSize-1;  //!^ same as in create
	size_t size2 = size*size;
	//  new
	delete[] blendMtr;
	blendMtr = new char[size2];
	memset(blendMtr,0,size2);  // zero

	blendMapSize = size;
	uint8* pd = new uint8[size2*4];  // temp

	HardwarePixelBufferSharedPtr pt = blRT->getBuffer();
	PixelBox pb(pt->getWidth(), pt->getHeight(), pt->getDepth(), pt->getFormat(), pd);
	assert(pt->getWidth() == size && pt->getHeight() == size);

	pt->blitToMemory(pb);
	//RenderTexture* pTex = pt->getRenderTarget();
	//pTex->copyContentsToMemory(pb, RenderTarget::FB_AUTO);

	register size_t aa = pb.getRowSkip() * PixelUtil::getNumElemBytes(pb.format);
	register uint8* p = pd, v, h;
	register size_t j,i,a=0;
	register char mtr;
	for (j = 0; j < size; ++j)
	{
		for (i = 0; i < size; ++i)
		{
			mtr = 0;  h = 0;  // layers B,G,R,A
			v = *p++;  if (v > h) {  h = v;  mtr = 2;  }
			v = *p++;  if (v > h) {  h = v;  mtr = 1;  }
			v = *p++;  if (v > h) {  h = v;  mtr = 0;  }
			v = *p++;  if (v > h) {  h = v;  mtr = 3;  }
			blendMtr[a++] = mtr;
		}
		pd += aa;
	}
	delete[] pd;

	//ti.update();  /// time (10ms on 1k)
	//float dt = ti.dt * 1000.f;
	//LogO(String("::: Time Ter Ids: ") + fToStr(dt,3,5) + " ms");
}
#endif


///  update blendmap layer params in shader
//--------------------------------------------------------------------------
void App::UpdLayerPars()
{
	//  angles
	sh::MaterialInstance* mat = sh::Factory::getInstance().getMaterialInstance(sAngMat);
	mat->setProperty("InvTerSize", sh::makeProperty<sh::FloatValue>(new sh::FloatValue( 1.f / float(sc->td.iTerSize) )));
	mat->setProperty("TriSize",    sh::makeProperty<sh::FloatValue>(new sh::FloatValue( 2.f * sc->td.fTriangleSize )));

	//  blendmap
	mat = sh::Factory::getInstance().getMaterialInstance(sBlendMat);
	//  copy
	float Hmin[4],Hmax[4],Hsmt[4], Amin[4],Amax[4],Asmt[4], Fnoise[4];
	int nl = std::min(4, (int)sc->td.layers.size());
	for (int i=0; i < nl; ++i)
	{
		const TerLayer& l = sc->td.layersAll[sc->td.layers[i]];
		Hmin[i] = l.hMin;	Hmax[i] = l.hMax;	Hsmt[i] = l.hSm;
		Amin[i] = l.angMin;	Amax[i] = l.angMax;	Asmt[i] = l.angSm;
		Fnoise[i] = l.noise;
	}
	#define SetV(s,v)  mat->setProperty(s, sh::makeProperty<sh::Vector4>(new sh::Vector4(v[0], v[1], v[2], v[3])))
	SetV("Hmin", Hmin);  SetV("Hmax", Hmax);  SetV("Hsmt", Hsmt);
	SetV("Amin", Amin);  SetV("Amax", Amax);  SetV("Asmt", Asmt);
	//SetV("Fnoise", Fnoise);
}
