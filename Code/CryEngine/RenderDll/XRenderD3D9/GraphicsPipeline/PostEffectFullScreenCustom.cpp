#include "StdAfx.h"
#include "GraphicsPipeline/StandardGraphicsPipeline.h"
#include "Common/FullscreenPass.h"
#include "Common/RendererResources.h"
#include "Common/RenderView.h"
#include "../../Cry3DEngine/FullScreenShaderCtrl.h"
#include <CryString/CryName.h>
#include <CryRenderer/IShader.h>

void ExecuteFullScreenCustomShader()
{
	// Deprecated helper: pipeline version now handles the pass with a safe temp source.
	// Keep only if you explicitly wire this into a stage sequence.
	if (!gEnv || !gEnv->p3DEngine)
		return;

	float active = 0.f;
	gEnv->p3DEngine->GetPostEffectParam("FullScreenCustomShader_Active", active);
	if (active < 0.5f)
		return;

	auto& ctrl = CFullScreenShaderCtrl::Get();
	IShader* pISh = ctrl.GetShader();
	const int tech = ctrl.GetTechniqueIndex();
	if (!pISh || tech < 0)
		return;

	CShader* pShader = static_cast<CShader*>(pISh);
	if (!pShader)
		return;

	CTexture* pOut = nullptr;
#if defined(SCENE_TEXTURE_HDR)
	if (CRendererResources::s_ptexHDRTarget)
		pOut = CRendererResources::s_ptexHDRTarget;
#endif
#if defined(SCENE_TEXTURE_SCENE_TARGET)
	if (!pOut && CRendererResources::s_ptexSceneTarget)
		pOut = CRendererResources::s_ptexSceneTarget;
#endif
	if (!pOut)
		return;

	// NOTE: This version does an in-place write with no sampling of the current color.
	// Your shader must be written to NOT read t0 == output or you must keep the integrated pipeline path instead.
	CFullscreenPass pass;
	pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants);
	pass.SetPrimitiveType(CRenderPrimitive::ePrim_ProceduralTriangle);
	pass.SetRenderTarget(0, pOut);
	pass.SetState(GS_NODEPTHTEST);
	pass.SetTechnique(pShader, CCryNameTSCRC("Execute"), tech);
	pass.BeginConstantUpdate();
	pass.Execute();
}