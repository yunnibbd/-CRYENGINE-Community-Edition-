#pragma once
#include "Common/GraphicsPipelineStage.h"
#include "Common/FullscreenPass.h"

class CFullScreenCustomStage : public CGraphicsPipelineStage
{
public:
	static const EGraphicsPipelineStage StageID = eStage_FullScreenCustom;

	CFullScreenCustomStage(CGraphicsPipeline& graphicsPipeline)
		: CGraphicsPipelineStage(graphicsPipeline)
		, m_pass(&graphicsPipeline) {
	}

	bool IsStageActive(EShaderRenderingFlags renderingFlags) const;
	void Execute();



private:
	CFullscreenPass m_pass;
	bool            m_initialized = false;
};