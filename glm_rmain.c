
#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"

void GLM_PrepareWorldModelBatch(void);
void GLM_DrawBillboards(void);

void GLM_RenderView(void)
{
	GLM_PrepareWorldModelBatch();
	GLM_PrepareAliasModelBatches();
	GLM_PrepareBillboards();

	GL_DrawWorldModelBatch(opaque_world);

	GL_EnterRegion("GLM_DrawEntities");
	GLM_DrawAliasModelBatches();
	GL_LeaveRegion();

	GLM_DrawBillboards();

	GL_DrawWorldModelBatch(alpha_surfaces);
}
