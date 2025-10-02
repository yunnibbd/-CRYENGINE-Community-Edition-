// Copyright 2007-2021 Crytek GmbH / Crytek Group. All rights reserved.

/**********************************************************************************
**                             HPi Source File                                   **
**    Copyright (C) 2020-2025 HPiStudio. All rights Reserved.                    **
** ********************************************************************************
**                                                                               **
** Description:                                                                  **
** Enable Dyn.Tex. Rendering   Follow Graph Nodes.                               **
**                                                                               **
** Created in fri 1404/03/16 06:20 PM By Hosein Pirani                           **
**                                                                               **
** Modified In     1404/  /   00:00 PM To 00:00 PM by   .                        **
** :	                         												 **
** TODO:									                                     **
** ..                                                                            **
** ...                                                                           **
** ....                                                                          **
** .....       													     			 **
** ........                                                                      **
** ...........                                                                   **
** ...............  #_#                                                          **
**********************************************************************************/

#include "StdAfx.h"

#include <CrySystem/Scaleform/IFlashPlayer.h>
//#include <CrySystem/ILocalizationManager.h>
#include <CryFlowGraph/IFlowBaseNode.h>


class CFlowFlashEnableDynTexNode : public CFlowBaseNode<eNCT_Singleton>
{
public:
	CFlowFlashEnableDynTexNode(SActivationInfo* pActInfo)
	{
	}

	enum EInputPorts
	{
		EIP_Slot = 0,
		EIP_SubMtlId,
		EIP_TexSlot,
		EIP_Set,
		EIP_Enable,
		EIP_Disable,
	};

	enum EOutputPorts
	{
		EOP_Result = 0,
	};

	static const int MAX_PARAMS = 4;
	virtual void GetConfiguration(SFlowNodeConfig& config)
	{
		static const SInputPortConfig in_config[] = {
			InputPortConfig<int>("Slot",      0,                                     _HELP("Material Slot")),
			InputPortConfig<int>("SubMtlId",  0,                                     _HELP("Sub Material Id, starting at 0"),_HELP("SubMaterialId")),
			InputPortConfig<int>("TexSlot",   0,                                     _HELP("Texture Slot")),
			InputPortConfig_Void("Set",    _HELP("Set Given Option(Enable or Disable).")),
			InputPortConfig<bool>("Enable", _HELP("Enable Dynamic Texture Updating On Given Slot")),
			InputPortConfig<bool>("Disable", _HELP("Disable Dynamic Texture Updating On Given Slot")),
	
			{ 0 }
		};

		static const SOutputPortConfig out_config[] = {
			OutputPortConfig_AnyType("Result", _HELP("Result")),
			{ 0 }
		};

		config.sDescription = _HELP("Enable/Disable Flash Dynamic Texture Updating On Objects Materials.");
		config.nFlags |= EFLN_TARGET_ENTITY;
		config.pInputPorts = in_config;
		config.pOutputPorts = out_config;
		config.SetCategory(EFLN_ADVANCED);
	}

	virtual void ProcessEvent(EFlowEvent event, SActivationInfo* pActInfo)
	{
		if (event != eFE_Activate || !IsPortActive(pActInfo, EIP_Set))
			return;

		IEntity* pEntity = pActInfo->pEntity;
		if (pEntity == 0)
			return;

		IEntityRender* pIEntityRender = pEntity->GetRenderInterface();
		if (pIEntityRender == 0)
			return;

		const int slot = GetPortInt(pActInfo, EIP_Slot);
		IMaterial* pMtl = pIEntityRender->GetRenderMaterial(slot);
		if (pMtl == 0)
		{
			GameWarning("[flow] CFlowFlashEnableDynTexNode: Entity '%s' [%d] has no material at slot %d", pEntity->GetName(), pEntity->GetId(), slot);
			return;
		}

		const int& subMtlId = GetPortInt(pActInfo, EIP_SubMtlId);
		pMtl = pMtl->GetSafeSubMtl(subMtlId);
		if (pMtl == 0)
		{
			GameWarning("[flow] CFlowFlashEnableDynTexNode: Entity '%s' [%d] has no sub-material %d at slot %d", pEntity->GetName(), pEntity->GetId(), subMtlId, slot);
			return;
		}

		const bool bEnable = GetPortBool(pActInfo, EIP_Enable);
		const bool bDisable = GetPortBool(pActInfo, EIP_Disable);
		const int texSlot = GetPortInt(pActInfo, EIP_TexSlot);
		const SShaderItem& shaderItem(pMtl->GetShaderItem());
		if (shaderItem.m_pShaderResources)
		{
			SEfResTexture* pTex = shaderItem.m_pShaderResources->GetTexture(texSlot);
			if (pTex)
			{
				IDynTextureSource* pDynTexSrc = pTex->m_Sampler.m_pDynTexSource;
				if (pDynTexSrc)
				{
					IFlashPlayer* pFlashPlayer = (IFlashPlayer*)pDynTexSrc->GetSourceTemp(IDynTextureSource::DTS_I_FLASHPLAYER);
					if (pFlashPlayer)
					{

						if (bEnable && !bDisable)
						{
							pDynTexSrc->EnablePerFrameRendering(true);
							ActivateOutput(pActInfo, EOP_Result, true);
						}
						else if (bDisable && !bEnable)
						{
							pDynTexSrc->EnablePerFrameRendering(false);
							ActivateOutput(pActInfo, EOP_Result, false);
						}
						else
						{
							GameWarning("[flow] CFlowFlashEnableDynTexNode: Entity '%s' [%d] Enable/Disable is not set properly, both are set or none is set", pEntity->GetName(), pEntity->GetId());
							return;
						}


					}
					else
					{
						GameWarning("[flow] CFlowFlashEnableDynTexNode: Error while Getting pFlashPlayer  on Entity '%s' [%d]", pEntity->GetName(), pEntity->GetId());
					}
				}
				else
				{
					GameWarning("[flow] CFlowFlashEnableDynTexNode: Entity '%s' [%d] has no FlashDynTexture at sub-material %d at slot %d at texslot %d", pEntity->GetName(), pEntity->GetId(), subMtlId, slot, texSlot);
				}

			}
			else
			{
				GameWarning("[flow] CFlowFlashEnableDynTexNode: Entity '%s' [%d] has no dyn-texture at sub-material %d at slot %d at texslot %d", pEntity->GetName(), pEntity->GetId(), subMtlId, slot, texSlot);
			}
		}
		else
		{
			GameWarning("[flow] CFlowFlashEnableDynTexNode: Entity '%s' [%d] has no texture at sub-material %d at slot %d at texslot %d", pEntity->GetName(), pEntity->GetId(), subMtlId, slot, texSlot);
		}
		
	}

	virtual void GetMemoryUsage(ICrySizer* s) const
	{
		s->Add(*this);
	}
};

REGISTER_FLOW_NODE("Flash:EnableDynTexOnObject", CFlowFlashEnableDynTexNode)
