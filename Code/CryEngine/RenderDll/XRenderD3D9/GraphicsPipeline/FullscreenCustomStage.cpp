#include "StdAfx.h"
#include "FullscreenCustomStage.h"

#include "StandardGraphicsPipeline.h"
#include <CryRenderer/IShader.h>
#include <Cry3DEngine/I3DEngine.h>
#include "Common/RendererResources.h"
#include "../../Cry3DEngine/FullScreenShaderCtrl.h"

bool CFullScreenCustomStage::IsStageActive(EShaderRenderingFlags renderingFlags) const
{
	if (!gEnv || !gEnv->p3DEngine)
		return false;

	if ((renderingFlags & SHDF_ALLOWPOSTPROCESS) == 0)
		return false;

	float active = 0.f;
	gEnv->p3DEngine->GetPostEffectParam("FullScreenCustomShader_Active", active);
	return active >= 0.5f;
}

void CFullScreenCustomStage::Execute()
{
	if (!gEnv || !gEnv->p3DEngine)
		return;

	auto& ctrl = CFullScreenShaderCtrl::Get();
	IShader* pIShader = ctrl.GetShader();
	if (!pIShader || ctrl.GetTechniqueIndex() < 0)
		return;

	CShader* pShader = static_cast<CShader*>(pIShader);
	if (!pShader)
		return;

	if (!m_initialized)
	{
		m_pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants);
		m_pass.SetPrimitiveType(CRenderPrimitive::ePrim_ProceduralTriangle);
		m_initialized = true;
	}

	// Prefer pipeline HDR target (participates in subsequent bloom/exposure)
	CTexture* pOut = m_graphicsPipelineResources.m_pTexHDRTarget;

#if defined(SCENE_TEXTURE_HDR)
	if (!pOut && CRendererResources::s_ptexHDRTarget)
		pOut = CRendererResources::s_ptexHDRTarget;
#endif
#if defined(SCENE_TEXTURE_SCENE_TARGET)
	if (!pOut && CRendererResources::s_ptexSceneTarget)
		pOut = CRendererResources::s_ptexSceneTarget;
#endif

	if (!pOut)
		return;

	m_pass.SetRenderTarget(0, pOut);
	m_pass.SetState(GS_NODEPTHTEST);

	static CCryNameTSCRC techName("Execute");
	m_pass.SetTechnique(pShader, techName, 0);

	// Optional input binding example:
	// CTexture* pScene = CRendererResources::s_ptexSceneTarget ? CRendererResources::s_ptexSceneTarget : pOut;
	// m_pass.SetTextureSamplerPair(0, pScene, EDefaultSamplerStates::LinearClamp);

	m_pass.BeginConstantUpdate();
	m_pass.Execute();
}