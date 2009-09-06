/**
 * $Id$
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Contributor(s): Blender Foundation (2008).
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <stdlib.h>

#include "RNA_define.h"
#include "RNA_types.h"

#include "rna_internal.h"

#include "DNA_scene_types.h"
#include "DNA_userdef_types.h"

#ifdef WITH_FFMPEG
#include "BKE_writeffmpeg.h"
#include <libavcodec/avcodec.h> 
#include <libavformat/avformat.h>
#endif

#include "WM_types.h"

/* prop_mode needs to be accessible from transform operator */
EnumPropertyItem prop_mode_items[] ={
	{PROP_SMOOTH, "SMOOTH", 0, "Smooth", ""},
	{PROP_SPHERE, "SPHERE", 0, "Sphere", ""},
	{PROP_ROOT, "ROOT", 0, "Root", ""},
	{PROP_SHARP, "SHARP", 0, "Sharp", ""},
	{PROP_LIN, "LINEAR", 0, "Linear", ""},
	{PROP_CONST, "CONSTANT", 0, "Constant", ""},
	{PROP_RANDOM, "RANDOM", 0, "Random", ""},
	{0, NULL, 0, NULL, NULL}};

#ifdef RNA_RUNTIME

#include "DNA_anim_types.h"
#include "DNA_node_types.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_node.h"

#include "BLI_threads.h"

#include "RE_pipeline.h"

PointerRNA rna_Scene_objects_get(CollectionPropertyIterator *iter)
{
	ListBaseIterator *internal= iter->internal;

	/* we are actually iterating a Base list, so override get */
	return rna_pointer_inherit_refine(&iter->parent, &RNA_Object, ((Base*)internal->link)->object);
}

static int layer_set(int lay, const int *values)
{
	int i, tot= 0;

	/* ensure we always have some layer selected */
	for(i=0; i<20; i++)
		if(values[i])
			tot++;
	
	if(tot==0)
		return lay;

	for(i=0; i<20; i++) {
		if(values[i]) lay |= (1<<i);
		else lay &= ~(1<<i);
	}

	return lay;
}

static void rna_Scene_layer_set(PointerRNA *ptr, const int *values)
{
	Scene *scene= (Scene*)ptr->data;

	scene->lay= layer_set(scene->lay, values);
}

static void rna_Scene_start_frame_set(PointerRNA *ptr, int value)
{
	Scene *data= (Scene*)ptr->data;
	CLAMP(value, 1, data->r.efra);
	data->r.sfra= value;
}

static void rna_Scene_end_frame_set(PointerRNA *ptr, int value)
{
	Scene *data= (Scene*)ptr->data;
	CLAMP(value, data->r.sfra, MAXFRAME);
	data->r.efra= value;
}

static int rna_Scene_use_preview_range_get(PointerRNA *ptr)
{
	Scene *data= (Scene*)ptr->data;
	
	/* this is simply overloaded to assume that preview-range 
	 * start frame cannot be less than 1 when on,
	 * so psfra=0 means 'off'
	 */
	return (data->r.psfra != 0);
}

static void rna_Scene_use_preview_range_set(PointerRNA *ptr, int value)
{
	Scene *data= (Scene*)ptr->data;
	
	/* if enable, copy range from render-range, otherwise just clear */
	if (value) {
		data->r.psfra= data->r.sfra;
		data->r.pefra= data->r.efra;
	}
	else
		data->r.psfra= 0;
}


static void rna_Scene_preview_range_start_frame_set(PointerRNA *ptr, int value)
{
	Scene *data= (Scene*)ptr->data;
	
	/* check if enabled already */
	if (data->r.psfra == 0) {
		/* set end of preview range to end frame, then clamp as per normal */
		// TODO: or just refuse to set instead?
		data->r.pefra= data->r.efra;
	}
	
	/* now set normally */
	CLAMP(value, 1, data->r.pefra);
	data->r.psfra= value;
}

static void rna_Scene_preview_range_end_frame_set(PointerRNA *ptr, int value)
{
	Scene *data= (Scene*)ptr->data;
	
	/* check if enabled already */
	if (data->r.psfra == 0) {
		/* set start of preview range to start frame, then clamp as per normal */
		// TODO: or just refuse to set instead?
		data->r.psfra= data->r.sfra; 
	}
	
	/* now set normally */
	CLAMP(value, data->r.psfra, MAXFRAME);
	data->r.pefra= value;
}

static void rna_Scene_frame_update(bContext *C, PointerRNA *ptr)
{
	//Scene *scene= ptr->id.data;
	//ED_update_for_newframe(C);
}

static int rna_Scene_active_keying_set_editable(PointerRNA *ptr)
{
	Scene *scene= (Scene *)ptr->data;
	
	/* only editable if there are some Keying Sets to change to */
	return (scene->keyingsets.first != NULL);
}

static PointerRNA rna_Scene_active_keying_set_get(PointerRNA *ptr)
{
	Scene *scene= (Scene *)ptr->data;
	return rna_pointer_inherit_refine(ptr, &RNA_KeyingSet, BLI_findlink(&scene->keyingsets, scene->active_keyingset-1));
}

static void rna_Scene_active_keying_set_set(PointerRNA *ptr, PointerRNA value)
{
	Scene *scene= (Scene *)ptr->data;
	KeyingSet *ks= (KeyingSet*)value.data;
	scene->active_keyingset= BLI_findindex(&scene->keyingsets, ks) + 1;
}

static int rna_Scene_active_keying_set_index_get(PointerRNA *ptr)
{
	Scene *scene= (Scene *)ptr->data;
	return MAX2(scene->active_keyingset-1, 0);
}

static void rna_Scene_active_keying_set_index_set(PointerRNA *ptr, int value)
{
	Scene *scene= (Scene *)ptr->data;
	scene->active_keyingset= value+1;
}

static void rna_Scene_active_keying_set_index_range(PointerRNA *ptr, int *min, int *max)
{
	Scene *scene= (Scene *)ptr->data;

	*min= 0;
	*max= BLI_countlist(&scene->keyingsets)-1;
	*max= MAX2(0, *max);
}


static char *rna_SceneRenderData_path(PointerRNA *ptr)
{
	return BLI_sprintfN("render_data");
}

static int rna_SceneRenderData_threads_get(PointerRNA *ptr)
{
	RenderData *rd= (RenderData*)ptr->data;

	if(rd->mode & R_FIXED_THREADS)
		return rd->threads;
	else
		return BLI_system_thread_count();
}

static int rna_SceneRenderData_save_buffers_get(PointerRNA *ptr)
{
	RenderData *rd= (RenderData*)ptr->data;

	return (rd->scemode & (R_EXR_TILE_FILE|R_FULL_SAMPLE)) != 0;
}

static void rna_SceneRenderData_file_format_set(PointerRNA *ptr, int value)
{
	RenderData *rd= (RenderData*)ptr->data;

	rd->imtype= value;
#ifdef WITH_FFMPEG
	ffmpeg_verify_image_type(rd);
#endif
}

static int rna_SceneRenderData_active_layer_index_get(PointerRNA *ptr)
{
	RenderData *rd= (RenderData*)ptr->data;
	return rd->actlay;
}

static void rna_SceneRenderData_active_layer_index_set(PointerRNA *ptr, int value)
{
	RenderData *rd= (RenderData*)ptr->data;
	rd->actlay= value;
}

static void rna_SceneRenderData_active_layer_index_range(PointerRNA *ptr, int *min, int *max)
{
	RenderData *rd= (RenderData*)ptr->data;

	*min= 0;
	*max= BLI_countlist(&rd->layers)-1;
	*max= MAX2(0, *max);
}

static void rna_SceneRenderData_engine_set(PointerRNA *ptr, int value)
{
	RenderData *rd= (RenderData*)ptr->data;
	RenderEngineType *type= BLI_findlink(&R_engines, value);

	if(type)
		BLI_strncpy(rd->engine, type->idname, sizeof(rd->engine));
}

static EnumPropertyItem *rna_SceneRenderData_engine_itemf(bContext *C, PointerRNA *ptr, int *free)
{
	RenderEngineType *type;
	EnumPropertyItem *item= NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int a=0, totitem= 0;

	for(type=R_engines.first; type; type=type->next, a++) {
		tmp.value= a;
		tmp.identifier= type->idname;
		tmp.name= type->name;
		RNA_enum_item_add(&item, &totitem, &tmp);
	}
	
	RNA_enum_item_end(&item, &totitem);
	*free= 1;

	return item;
}

static int rna_SceneRenderData_engine_get(PointerRNA *ptr)
{
	RenderData *rd= (RenderData*)ptr->data;
	RenderEngineType *type;
	int a= 0;

	for(type=R_engines.first; type; type=type->next, a++)
		if(strcmp(type->idname, rd->engine) == 0)
			return a;
	
	return 0;
}

static void rna_SceneRenderLayer_name_set(PointerRNA *ptr, const char *value)
{
	Scene *scene= (Scene*)ptr->id.data;
	SceneRenderLayer *rl= (SceneRenderLayer*)ptr->data;

    BLI_strncpy(rl->name, value, sizeof(rl->name));

	if(scene->nodetree) {
		bNode *node;
		int index= BLI_findindex(&scene->r.layers, rl);

		for(node= scene->nodetree->nodes.first; node; node= node->next) {
			if(node->type==CMP_NODE_R_LAYERS && node->id==NULL) {
				if(node->custom1==index)
					BLI_strncpy(node->name, rl->name, NODE_MAXSTR);
			}
		}
	}
}

static int rna_SceneRenderData_multiple_engines_get(PointerRNA *ptr)
{
	return (BLI_countlist(&R_engines) > 1);
}

static int rna_SceneRenderData_use_game_engine_get(PointerRNA *ptr)
{
	RenderData *rd= (RenderData*)ptr->data;
	RenderEngineType *type;

	for(type=R_engines.first; type; type=type->next)
		if(strcmp(type->idname, rd->engine) == 0)
			return (type->flag & RE_GAME);
	
	return 0;
}

static void rna_SceneRenderLayer_layer_set(PointerRNA *ptr, const int *values)
{
	SceneRenderLayer *rl= (SceneRenderLayer*)ptr->data;
	rl->lay= layer_set(rl->lay, values);
}

static void rna_SceneRenderLayer_zmask_layer_set(PointerRNA *ptr, const int *values)
{
	SceneRenderLayer *rl= (SceneRenderLayer*)ptr->data;
	rl->lay_zmask= layer_set(rl->lay_zmask, values);
}

static void rna_SceneRenderLayer_pass_update(bContext *C, PointerRNA *ptr)
{
	Scene *scene= (Scene*)ptr->id.data;

	if(scene->nodetree)
		ntreeCompositForceHidden(scene->nodetree, scene);
}

#else

static void rna_def_tool_settings(BlenderRNA  *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem uv_select_mode_items[] = {
		{UV_SELECT_VERTEX, "VERTEX", ICON_VERTEXSEL, "Vertex", "Vertex selection mode."},
		{UV_SELECT_EDGE, "EDGE", ICON_EDGESEL, "Edge", "Edge selection mode."},
		{UV_SELECT_FACE, "FACE", ICON_FACESEL, "Face", "Face selection mode."},
		{UV_SELECT_ISLAND, "ISLAND", ICON_LINKEDSEL, "Island", "Island selection mode."},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem mesh_select_mode_items[] = {
		{SCE_SELECT_VERTEX, "VERTEX", ICON_VERTEXSEL, "Vertex", "Vertex selection mode."},
		{SCE_SELECT_EDGE, "EDGE", ICON_EDGESEL, "Edge", "Edge selection mode."},
		{SCE_SELECT_FACE, "FACE", ICON_FACESEL, "Face", "Face selection mode."},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem snap_element_items[] = {
		{SCE_SNAP_MODE_VERTEX, "VERTEX", ICON_SNAP_VERTEX, "Vertex", "Snap to vertices."},
		{SCE_SNAP_MODE_EDGE, "EDGE", ICON_SNAP_EDGE, "Edge", "Snap to edges."},
		{SCE_SNAP_MODE_FACE, "FACE", ICON_SNAP_FACE, "Face", "Snap to faces."},
		{SCE_SNAP_MODE_VOLUME, "VOLUME", ICON_SNAP_VOLUME, "Volume", "Snap to volume."},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem snap_mode_items[] = {
		{SCE_SNAP_TARGET_CLOSEST, "CLOSEST", 0, "Closest", "Snap closest point onto target."},
		{SCE_SNAP_TARGET_CENTER, "CENTER", 0, "Center", "Snap center onto target."},
		{SCE_SNAP_TARGET_MEDIAN, "MEDIAN", 0, "Median", "Snap median onto target."},
		{SCE_SNAP_TARGET_ACTIVE, "ACTIVE", 0, "Active", "Snap active onto target."},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem auto_key_items[] = {
		{AUTOKEY_MODE_NORMAL, "ADD_REPLACE_KEYS", 0, "Add/Replace", ""},
		{AUTOKEY_MODE_EDITKEYS, "REPLACE_KEYS", 0, "Replace", ""},
		{0, NULL, 0, NULL, NULL}};

	srna= RNA_def_struct(brna, "ToolSettings", NULL);
	RNA_def_struct_ui_text(srna, "Tool Settings", "");
	
	prop= RNA_def_property(srna, "sculpt", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "Sculpt");
	RNA_def_property_ui_text(prop, "Sculpt", "");
	
	prop= RNA_def_property(srna, "vertex_paint", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "vpaint");
	RNA_def_property_ui_text(prop, "Vertex Paint", "");

	prop= RNA_def_property(srna, "weight_paint", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "wpaint");
	RNA_def_property_ui_text(prop, "Weight Paint", "");

	prop= RNA_def_property(srna, "image_paint", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "imapaint");
	RNA_def_property_ui_text(prop, "Image Paint", "");

	prop= RNA_def_property(srna, "particle_edit", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "particle");
	RNA_def_property_ui_text(prop, "Particle Edit", "");

	/* Transform */
	prop= RNA_def_property(srna, "proportional_editing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "proportional", 0);
	RNA_def_property_ui_text(prop, "Proportional Editing", "Proportional editing mode.");

	prop= RNA_def_property(srna, "proportional_editing_falloff", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "prop_mode");
	RNA_def_property_enum_items(prop, prop_mode_items);
	RNA_def_property_ui_text(prop, "Proportional Editing Falloff", "Falloff type for proportional editing mode.");

	prop= RNA_def_property(srna, "automerge_editing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "automerge", 0);
	RNA_def_property_ui_text(prop, "AutoMerge Editing", "Automatically merge vertices moved to the same location.");

	prop= RNA_def_property(srna, "snap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP);
	RNA_def_property_ui_text(prop, "Snap", "Snap while Ctrl is held during transform.");
	RNA_def_property_ui_icon(prop, ICON_SNAP_GEAR, 1);

	prop= RNA_def_property(srna, "snap_align_rotation", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP_ROTATE);
	RNA_def_property_ui_text(prop, "Snap Align Rotation", "Align rotation with the snapping target.");
	RNA_def_property_ui_icon(prop, ICON_SNAP_NORMAL, 0);

	prop= RNA_def_property(srna, "snap_element", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "snap_mode");
	RNA_def_property_enum_items(prop, snap_element_items);
	RNA_def_property_ui_text(prop, "Snap Element", "Type of element to snap to.");

	prop= RNA_def_property(srna, "snap_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "snap_target");
	RNA_def_property_enum_items(prop, snap_mode_items);
	RNA_def_property_ui_text(prop, "Snap Mode", "Which part to snap onto the target.");

	prop= RNA_def_property(srna, "snap_peel_object", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP_PEEL_OBJECT);
	RNA_def_property_ui_text(prop, "Snap Peel Object", "Consider objects as whole when finding volume center.");
	RNA_def_property_ui_icon(prop, ICON_SNAP_PEEL_OBJECT, 0);
	
	/* Auto Keying */
	prop= RNA_def_property(srna, "enable_auto_key", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "autokey_mode", AUTOKEY_ON);
	RNA_def_property_ui_text(prop, "Auto Keying", "Automatic keyframe insertion for Objects and Bones");
	
	prop= RNA_def_property(srna, "autokey_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "autokey_mode");
	RNA_def_property_enum_items(prop, auto_key_items);
	RNA_def_property_ui_text(prop, "Auto-Keying Mode", "Mode of automatic keyframe insertion for Objects and Bones");
	
	prop= RNA_def_property(srna, "record_with_nla", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "autokey_flag", ANIMRECORD_FLAG_WITHNLA);
	RNA_def_property_ui_text(prop, "Layered", "Add a new NLA Track + Strip for every loop/pass made over the animation to allow non-destructive tweaking.");

	/* UV */
	prop= RNA_def_property(srna, "uv_selection_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "uv_selectmode");
	RNA_def_property_enum_items(prop, uv_select_mode_items);
	RNA_def_property_ui_text(prop, "UV Selection Mode", "UV selection and display mode.");

	prop= RNA_def_property(srna, "uv_sync_selection", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uv_flag", UV_SYNC_SELECTION);
	RNA_def_property_ui_text(prop, "UV Sync Selection", "Keep UV and edit mode mesh selection in sync.");
	RNA_def_property_ui_icon(prop, ICON_EDIT, 0);

	prop= RNA_def_property(srna, "uv_local_view", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uv_flag", UV_SHOW_SAME_IMAGE);
	RNA_def_property_ui_text(prop, "UV Local View", "Draw only faces with the currently displayed image assigned.");

	/* Mesh */
	prop= RNA_def_property(srna, "mesh_selection_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "selectmode");
	RNA_def_property_enum_items(prop, mesh_select_mode_items);
	RNA_def_property_ui_text(prop, "Mesh Selection Mode", "Mesh selection and display mode.");

	prop= RNA_def_property(srna, "vertex_group_weight", PROP_FLOAT, PROP_PERCENTAGE);
	RNA_def_property_float_sdna(prop, NULL, "vgroup_weight");
	RNA_def_property_ui_text(prop, "Vertex Group Weight", "Weight to assign in vertex groups.");
}


static void rna_def_unit_settings(BlenderRNA  *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem unit_systems[] = {
		{USER_UNIT_NONE, "NONE", 0, "None", ""},
		{USER_UNIT_METRIC, "METRIC", 0, "Metric", ""},
		{USER_UNIT_IMPERIAL, "IMPERIAL", 0, "Imperial", ""},
		{0, NULL, 0, NULL, NULL}};

	srna= RNA_def_struct(brna, "UnitSettings", NULL);
	RNA_def_struct_ui_text(srna, "Unit Settings", "");

	/* Units */
	prop= RNA_def_property(srna, "system", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, unit_systems);
	RNA_def_property_ui_text(prop, "Unit System", "The unit system to use for button display.");
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop= RNA_def_property(srna, "scale_length", PROP_FLOAT, PROP_UNSIGNED);
	RNA_def_property_ui_text(prop, "Unit Scale", "Scale to use when converting between blender units and dimensions.");
	RNA_def_property_range(prop, 0.00001, 100000.0);
	RNA_def_property_ui_range(prop, 0.001, 100.0, 0.1, 3);
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop= RNA_def_property(srna, "use_separate", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_UNIT_OPT_SPLIT);
	RNA_def_property_ui_text(prop, "Separate Units", "Display units in pairs.");
	RNA_def_property_update(prop, NC_WINDOW, NULL);
}


void rna_def_render_layer_common(StructRNA *srna, int scene)
{
	PropertyRNA *prop;

	prop= RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	if(scene) RNA_def_property_string_funcs(prop, NULL, NULL, "rna_SceneRenderLayer_name_set");
	else RNA_def_property_string_sdna(prop, NULL, "name");
	RNA_def_property_ui_text(prop, "Name", "Render layer name.");
	RNA_def_struct_name_property(srna, prop);
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "material_override", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "mat_override");
	RNA_def_property_struct_type(prop, "Material");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Material Override", "Material to override all other materials in this render layer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "light_override", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "light_override");
	RNA_def_property_struct_type(prop, "Group");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Light Override", "Group to override all other lights in this render layer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	/* layers */
	prop= RNA_def_property(srna, "visible_layers", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_ui_text(prop, "Visible Layers", "Scene layers included in this render layer.");
	if(scene) RNA_def_property_boolean_funcs(prop, NULL, "rna_SceneRenderLayer_layer_set");
	else RNA_def_property_boolean_funcs(prop, NULL, "rna_RenderLayer_layer_set");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "zmask_layers", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "lay_zmask", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_ui_text(prop, "Zmask Layers", "Zmask scene layers.");
	if(scene) RNA_def_property_boolean_funcs(prop, NULL, "rna_SceneRenderLayer_zmask_layer_set");
	else RNA_def_property_boolean_funcs(prop, NULL, "rna_RenderLayer_zmask_layer_set");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	/* layer options */
	prop= RNA_def_property(srna, "enabled", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "layflag", SCE_LAY_DISABLE);
	RNA_def_property_ui_text(prop, "Enabled", "Disable or enable the render layer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "zmask", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_ZMASK);
	RNA_def_property_ui_text(prop, "Zmask", "Only render what's in front of the solid z values.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "zmask_negate", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_NEG_ZMASK);
	RNA_def_property_ui_text(prop, "Zmask Negate", "For Zmask, only render what is behind solid z values instead of in front.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "all_z", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_ALL_Z);
	RNA_def_property_ui_text(prop, "All Z", "Fill in Z values for solid faces in invisible layers, for masking.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "solid", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_SOLID);
	RNA_def_property_ui_text(prop, "Solid", "Render Solid faces in this Layer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "halo", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_HALO);
	RNA_def_property_ui_text(prop, "Halo", "Render Halos in this Layer (on top of Solid).");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "ztransp", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_ZTRA);
	RNA_def_property_ui_text(prop, "ZTransp", "Render Z-Transparent faces in this Layer (On top of Solid and Halos).");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "sky", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_SKY);
	RNA_def_property_ui_text(prop, "Sky", "Render Sky in this Layer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "edge", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_EDGE);
	RNA_def_property_ui_text(prop, "Edge", "Render Edge-enhance in this Layer (only works for Solid faces).");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "strand", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "layflag", SCE_LAY_STRAND);
	RNA_def_property_ui_text(prop, "Strand", "Render Strands in this Layer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	/* passes */
	prop= RNA_def_property(srna, "pass_combined", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_COMBINED);
	RNA_def_property_ui_text(prop, "Combined", "Deliver full combined RGBA buffer.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_z", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_Z);
	RNA_def_property_ui_text(prop, "Z", "Deliver Z values pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	
	prop= RNA_def_property(srna, "pass_vector", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_VECTOR);
	RNA_def_property_ui_text(prop, "Vector", "Deliver speed vector pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_normal", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_NORMAL);
	RNA_def_property_ui_text(prop, "Normal", "Deliver normal pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_uv", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_UV);
	RNA_def_property_ui_text(prop, "UV", "Deliver texture UV pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_mist", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_MIST);
	RNA_def_property_ui_text(prop, "Mist", "Deliver mist factor pass (0.0-1.0).");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_object_index", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_INDEXOB);
	RNA_def_property_ui_text(prop, "Object Index", "Deliver object index pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_color", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_RGBA);
	RNA_def_property_ui_text(prop, "Color", "Deliver shade-less color pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_diffuse", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_DIFFUSE);
	RNA_def_property_ui_text(prop, "Diffuse", "Deliver diffuse pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_specular", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_SPEC);
	RNA_def_property_ui_text(prop, "Specular", "Deliver specular pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_shadow", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_SHADOW);
	RNA_def_property_ui_text(prop, "Shadow", "Deliver shadow pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_ao", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_AO);
	RNA_def_property_ui_text(prop, "AO", "Deliver AO pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	
	prop= RNA_def_property(srna, "pass_reflection", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_REFLECT);
	RNA_def_property_ui_text(prop, "Reflection", "Deliver ratraced reflection pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_refraction", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "passflag", SCE_PASS_REFRACT);
	RNA_def_property_ui_text(prop, "Refraction", "Deliver ratraced refraction pass.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_specular_exclude", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "pass_xor", SCE_PASS_SPEC);
	RNA_def_property_ui_text(prop, "Specular Exclude", "Exclude specular pass from combined.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_shadow_exclude", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "pass_xor", SCE_PASS_SHADOW);
	RNA_def_property_ui_text(prop, "Shadow Exclude", "Exclude shadow pass from combined.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_ao_exclude", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "pass_xor", SCE_PASS_AO);
	RNA_def_property_ui_text(prop, "AO Exclude", "Exclude AO pass from combined.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	
	prop= RNA_def_property(srna, "pass_reflection_exclude", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "pass_xor", SCE_PASS_REFLECT);
	RNA_def_property_ui_text(prop, "Reflection Exclude", "Exclude ratraced reflection pass from combined.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop= RNA_def_property(srna, "pass_refraction_exclude", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "pass_xor", SCE_PASS_REFRACT);
	RNA_def_property_ui_text(prop, "Refraction Exclude", "Exclude ratraced refraction pass from combined.");
	if(scene) RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, "rna_SceneRenderLayer_pass_update");
	else RNA_def_property_clear_flag(prop, PROP_EDITABLE);
}

void rna_def_scene_game_data(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem framing_types_items[] ={
		{SCE_GAMEFRAMING_BARS, "BARS", 0, "Stretch", ""},
		{SCE_GAMEFRAMING_EXTEND, "EXTEND", 0, "Extend", ""},
		{SCE_GAMEFRAMING_SCALE, "SCALE", 0, "Scale", ""},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem dome_modes_items[] ={
		{DOME_FISHEYE, "FISHEYE", 0, "Fisheye", ""},
		{DOME_TRUNCATED_FRONT, "TRUNCATED_FRONT", 0, "Front-Truncated", ""},
		{DOME_TRUNCATED_REAR, "TRUNCATED_REAR", 0, "Rear-Truncated", ""},
		{DOME_ENVMAP, "ENVMAP", 0, "Cube Map", ""},
		{DOME_PANORAM_SPH, "PANORAM_SPH", 0, "Spherical Panoramic", ""},
		{0, NULL, 0, NULL, NULL}};
		
 	static EnumPropertyItem stereo_modes_items[] ={
//		{STEREO_NOSTEREO, "NO_STEREO", 0, "No Stereo", ""},
		{STEREO_QUADBUFFERED, "QUADBUFFERED", 0, "Quad-Buffer", ""},
		{STEREO_ABOVEBELOW, "ABOVEBELOW", 0, "Above-Below", ""},
		{STEREO_INTERLACED, "INTERLACED", 0, "Interlaced", ""},
		{STEREO_ANAGLYPH, "ANAGLYPH", 0, "Anaglyph", ""},
		{STEREO_SIDEBYSIDE, "SIDEBYSIDE", 0, "Side-by-side", ""},
		{STEREO_VINTERLACE, "VINTERLACE", 0, "Vinterlace", ""},
//		{STEREO_DOME, "DOME", 0, "Dome", ""},
		{0, NULL, 0, NULL, NULL}};
		
 	static EnumPropertyItem stereo_items[] ={
		{STEREO_NOSTEREO, "NO_STEREO", 0, "No Stereo", ""},
		{STEREO_ENABLED, "STEREO", 0, "Stereo", ""},
		{STEREO_DOME, "DOME", 0, "Dome", ""},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem physics_engine_items[] = {
		{WOPHY_NONE, "NONE", 0, "None", ""},
		//{WOPHY_ENJI, "ENJI", 0, "Enji", ""},
		//{WOPHY_SUMO, "SUMO", 0, "Sumo (Deprecated)", ""},
		//{WOPHY_DYNAMO, "DYNAMO", 0, "Dynamo", ""},
		//{WOPHY_ODE, "ODE", 0, "ODE", ""},
		{WOPHY_BULLET, "BULLET", 0, "Bullet", ""},
		{0, NULL, 0, NULL, NULL}};

	srna= RNA_def_struct(brna, "SceneGameData", NULL);
	RNA_def_struct_sdna(srna, "GameData");
	RNA_def_struct_nested(brna, srna, "Scene");
	RNA_def_struct_ui_text(srna, "Game Data", "Game data for a Scene datablock.");
	
	prop= RNA_def_property(srna, "resolution_x", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "xplay");
	RNA_def_property_range(prop, 4, 10000);
	RNA_def_property_ui_text(prop, "Resolution X", "Number of horizontal pixels in the screen.");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "resolution_y", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "yplay");
	RNA_def_property_range(prop, 4, 10000);
	RNA_def_property_ui_text(prop, "Resolution Y", "Number of vertical pixels in the screen.");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "depth", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "depth");
	RNA_def_property_range(prop, 8, 32);
	RNA_def_property_ui_text(prop, "Bits", "Displays bit depth of full screen display.");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	// Do we need it here ? (since we already have it in World
	prop= RNA_def_property(srna, "frequency", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "freqplay");
	RNA_def_property_range(prop, 4, 2000);
	RNA_def_property_ui_text(prop, "Freq", "Displays clock frequency of fullscreen display.");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "fullscreen", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "fullscreen", 1.0);
	RNA_def_property_ui_text(prop, "Fullscreen", "Starts player in a new fullscreen display");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	/* Framing */
	prop= RNA_def_property(srna, "framing_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "framing.type");
	RNA_def_property_enum_items(prop, framing_types_items);
	RNA_def_property_ui_text(prop, "Framing Types", "Select the type of Framing you want.");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "framing_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "framing.col");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "", "");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	/* Stereo */
	prop= RNA_def_property(srna, "stereo", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "stereoflag");
	RNA_def_property_enum_items(prop, stereo_items);
	RNA_def_property_ui_text(prop, "Stereo Options", "");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "stereo_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "stereomode");
	RNA_def_property_enum_items(prop, stereo_modes_items);
	RNA_def_property_ui_text(prop, "Stereo Mode", "");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	/* Dome */
	prop= RNA_def_property(srna, "dome_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "dome.mode");
	RNA_def_property_enum_items(prop, dome_modes_items);
	RNA_def_property_ui_text(prop, "Dome Mode", "");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "dome_tesselation", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "dome.res");
	RNA_def_property_ui_range(prop, 1, 8, 1, 1);
	RNA_def_property_ui_text(prop, "Tesselation", "Tesselation level - check the generated mesh in wireframe mode");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "dome_buffer_resolution", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "dome.resbuf");
	RNA_def_property_ui_range(prop, 0.1, 1.0, 0.1, 0.1);
	RNA_def_property_ui_text(prop, "Buffer Resolution", "Buffer Resolution - decrease it to increase speed");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "dome_angle", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "dome.angle");
	RNA_def_property_ui_range(prop, 90, 250, 1, 1);
	RNA_def_property_ui_text(prop, "Angle", "Field of View of the Dome - it only works in mode Fisheye and Truncated");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "dome_tilt", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "dome.tilt");
	RNA_def_property_ui_range(prop, -180, 180, 1, 1);
	RNA_def_property_ui_text(prop, "Tilt", "Camera rotation in horizontal axis");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "dome_text", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "dome.warptext");
	RNA_def_property_struct_type(prop, "Text");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Warp Data", "Custom Warp Mesh data file");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	/* physics */
	prop= RNA_def_property(srna, "physics_engine", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "physicsEngine");
	RNA_def_property_enum_items(prop, physics_engine_items);
	RNA_def_property_ui_text(prop, "Physics Engine", "Physics engine used for physics simulation in the game engine.");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "physics_gravity", PROP_FLOAT, PROP_ACCELERATION);
	RNA_def_property_float_sdna(prop, NULL, "gravity");
	RNA_def_property_range(prop, 0.0, 25.0);
	RNA_def_property_ui_text(prop, "Physics Gravity", "Gravitational constant used for physics simulation in the game engine.");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "occlusion_culling_resolution", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "occlusionRes");
	RNA_def_property_range(prop, 128.0, 1024.0);
	RNA_def_property_ui_text(prop, "Occlusion Resolution", "The size of the occlusion buffer in pixel, use higher value for better precsion (slower)");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "fps", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ticrate");
	RNA_def_property_ui_range(prop, 1, 60, 1, 1);
	RNA_def_property_range(prop, 1, 250);
	RNA_def_property_ui_text(prop, "Frames Per Second", "The nominal number of game frames per second. Physics fixed timestep = 1/fps, independently of actual frame rate.");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "logic_step_max", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "maxlogicstep");
	RNA_def_property_ui_range(prop, 1, 5, 1, 1);
	RNA_def_property_range(prop, 1, 5);
	RNA_def_property_ui_text(prop, "Max Logic Steps", "Sets the maximum number of logic frame per game frame if graphics slows down the game, higher value allows better synchronization with physics");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "physics_step_max", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "maxphystep");
	RNA_def_property_ui_range(prop, 1, 5, 1, 1);
	RNA_def_property_range(prop, 1, 5);
	RNA_def_property_ui_text(prop, "Max Physics Steps", "Sets the maximum number of physics step per game frame if graphics slows down the game, higher value allows physics to keep up with realtime");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	prop= RNA_def_property(srna, "physics_step_sub", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "physubstep");
	RNA_def_property_ui_range(prop, 1, 5, 1, 1);
	RNA_def_property_range(prop, 1, 5);
	RNA_def_property_ui_text(prop, "Physics Sub Steps", "Sets the number of simulation substep per physic timestep, higher value give better physics precision");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	/* mode */
	prop= RNA_def_property(srna, "use_occlusion_culling", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", (1 << 5)); //XXX mode hardcoded // WO_DBVT_CULLING
	RNA_def_property_ui_text(prop, "DBVT culling", "Use optimized Bullet DBVT tree for view frustrum and occlusion culling");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	// not used // deprecated !!!!!!!!!!!!!
	prop= RNA_def_property(srna, "activity_culling", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", (1 << 3)); //XXX mode hardcoded
	RNA_def_property_ui_text(prop, "Activity Culling", "Activity culling is enabled");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	// not used // deprecated !!!!!!!!!!!!!
	prop= RNA_def_property(srna, "activity_culling_box_radius", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "activityBoxRadius");
	RNA_def_property_range(prop, 0.0, 1000.0);
	RNA_def_property_ui_text(prop, "box radius", "Radius of the activity bubble, in Manhattan length. Objects outside the box are activity-culled");
	RNA_def_property_update(prop, NC_SCENE, NULL);
}

static void rna_def_scene_render_layer(BlenderRNA *brna)
{
	StructRNA *srna;

	srna= RNA_def_struct(brna, "SceneRenderLayer", NULL);
	RNA_def_struct_ui_text(srna, "Scene Render Layer", "Render layer.");

	rna_def_render_layer_common(srna, 1);
}

static void rna_def_scene_render_data(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	static EnumPropertyItem pixel_filter_items[] ={
		{R_FILTER_BOX, "BOX", 0, "Box", ""},
		{R_FILTER_TENT, "TENT", 0, "Tent", ""},
		{R_FILTER_QUAD, "QUADRATIC", 0, "Quadratic", ""},
		{R_FILTER_CUBIC, "CUBIC", 0, "Cubic", ""},
		{R_FILTER_CATROM, "CATMULLROM", 0, "Catmull-Rom", ""},
		{R_FILTER_GAUSS, "GAUSSIAN", 0, "Gaussian", ""},
		{R_FILTER_MITCH, "MITCHELL", 0, "Mitchell-Netravali", ""},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem alpha_mode_items[] ={
		{R_ADDSKY, "SKY", 0, "Sky", "Transparent pixels are filled with sky color"},
		{R_ALPHAPREMUL, "PREMUL", 0, "Premultiplied", "Transparent RGB pixels are multiplied by the alpha channel"},
		{R_ALPHAKEY, "STRAIGHT", 0, "Straight Alpha", "Transparent RGB and alpha pixels are unmodified"},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem color_mode_items[] ={
		{R_PLANESBW, "BW", 0, "BW", "Images are saved with BW (grayscale) data"},
		{R_PLANES24, "RGB", 0, "RGB", "Images are saved with RGB (color) data"},
		{R_PLANES32, "RGBA", 0, "RGBA", "Images are saved with RGB and Alpha data (if supported)"},
		{0, NULL, 0, NULL, NULL}};
	
	static EnumPropertyItem display_mode_items[] ={
		{R_OUTPUT_SCREEN, "SCREEN", 0, "Full Screen", "Images are rendered in full Screen"},
		{R_OUTPUT_AREA, "AREA", 0, "Image Editor", "Images are rendered in Image Editor"},
		{R_OUTPUT_WINDOW, "WINDOW", 0, "New Window", "Images are rendered in new Window"},
		{0, NULL, 0, NULL, NULL}};
	
	static EnumPropertyItem octree_resolution_items[] = {
		{64, "OCTREE_RES_64", 0, "64", ""},
		{128, "OCTREE_RES_128", 0, "128", ""},
		{256, "OCTREE_RES_256", 0, "256", ""},
		{512, "OCTREE_RES_512", 0, "512", ""},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem raytrace_structure_items[] = {
		{R_RAYSTRUCTURE_AUTO, "{R_RAYSTRUCTURE_AUTO", 0, "auto", ""},
		{R_RAYSTRUCTURE_OCTREE, "R_RAYSTRUCTURE_OCTREE", 0, "octree", "Use old octree structure (no support for instances)"},
		{R_RAYSTRUCTURE_BLIBVH, "R_RAYSTRUCTURE_BLIBVH", 0, "blibvh", "Use BLI_kdopbvh.c"},
		{R_RAYSTRUCTURE_VBVH, "R_RAYSTRUCTURE_VBVH", 0, "vBVH", ""},
		{R_RAYSTRUCTURE_SIMD_SVBVH, "R_RAYSTRUCTURE_SIMD_SVBVH", 0, "SIMD SVBVH", "Requires SIMD"},
		{R_RAYSTRUCTURE_SIMD_QBVH, "R_RAYSTRUCTURE_SIMD_QBVH", 0, "SIMD QBVH", "Requires SIMD"},
		{R_RAYSTRUCTURE_BIH, "R_RAYSTRUCTURE_BIH", 0, "BIH", ""},
		{0, NULL, 0, NULL, NULL}
		};

	static EnumPropertyItem fixed_oversample_items[] = {
		{5, "OVERSAMPLE_5", 0, "5", ""},
		{8, "OVERSAMPLE_8", 0, "8", ""},
		{11, "OVERSAMPLE_11", 0, "11", ""},
		{16, "OVERSAMPLE_16", 0, "16", ""},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem field_order_items[] = {
		{0, "FIELDS_EVENFIRST", 0, "Even", "Even Fields First"},
		{R_ODDFIELD, "FIELDS_ODDFIRST", 0, "Odd", "Odd Fields First"},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem threads_mode_items[] = {
		{0, "THREADS_AUTO", 0, "Auto-detect", "Automatically determine the number of threads, based on CPUs"},
		{R_FIXED_THREADS, "THREADS_FIXED", 0, "Fixed", "Manually determine the number of threads"},
		{0, NULL, 0, NULL, NULL}};
	
	static EnumPropertyItem stamp_font_size_items[] = {
		{1, "STAMP_FONT_TINY", 0, "Tiny", ""},
		{2, "STAMP_FONT_SMALL", 0, "Small", ""},
		{3, "STAMP_FONT_MEDIUM", 0, "Medium", ""},
		{0, "STAMP_FONT_LARGE", 0, "Large", ""},
		{4, "STAMP_FONT_EXTRALARGE", 0, "Extra Large", ""},
		{0, NULL, 0, NULL, NULL}};
		
	
	static EnumPropertyItem image_type_items[] = {
		{R_PNG, "PNG", 0, "PNG", ""},
		{R_JPEG90, "JPEG", 0, "JPEG", ""},
#ifdef WITH_OPENJPEG
		{R_JP2, "JPEG2000", 0, "JPEG 2000", ""},
#endif		
		{R_TIFF, "TIFF", 0, "TIFF", ""},	// XXX only with G.have_libtiff
		{R_BMP, "BMP", 0, "BMP", ""},
		{R_TARGA, "TARGA", 0, "Targa", ""},
		{R_RAWTGA, "RAWTARGA", 0, "Targa Raw", ""},
		//{R_DDS, "DDS", 0, "DDS", ""}, // XXX not yet implemented
		{R_HAMX, "HAMX", 0, "HamX", ""},
		{R_IRIS, "IRIS", 0, "Iris", ""},
		{0, "", 0, NULL, NULL},
#ifdef WITH_OPENEXR
		{R_OPENEXR, "OPENEXR", 0, "OpenEXR", ""},
		{R_MULTILAYER, "MULTILAYER", 0, "MultiLayer", ""},
#endif
		{R_RADHDR, "RADHDR", 0, "Radiance HDR", ""},
		{R_CINEON, "CINEON", 0, "Cineon", ""},
		{R_DPX, "DPX", 0, "DPX", ""},
		{0, "", 0, NULL, NULL},
		{R_AVIRAW, "AVIRAW", 0, "AVI Raw", ""},
		{R_AVIJPEG, "AVIJPEG", 0, "AVI JPEG", ""},
#ifdef _WIN32
		{R_AVICODEC, "AVICODEC", 0, "AVI Codec", ""},
#endif
#ifdef WITH_QUICKTIME
		{R_QUICKTIME, "QUICKTIME", 0, "QuickTime", ""},
#endif
#ifdef __sgi
		{R_MOVIE, "MOVIE", 0, "Movie", ""},
#endif
#ifdef WITH_FFMPEG
		{R_H264, "H264", 0, "H.264", ""},
		{R_XVID, "XVID", 0, "Xvid", ""},
		// XXX broken
#if 0
#ifdef WITH_OGG
		{R_THEORA, "THEORA", 0, "Ogg Theora", ""},
#endif
#endif
		{R_FFMPEG, "FFMPEG", 0, "FFMpeg", ""},
#endif
		{0, "", 0, NULL, NULL},
		{R_FRAMESERVER, "FRAMESERVER", 0, "Frame Server", ""},
		{0, NULL, 0, NULL, NULL}};
		
#ifdef WITH_OPENEXR	
	static EnumPropertyItem exr_codec_items[] = {
		{0, "NONE", 0, "None", ""},
		{1, "PXR24", 0, "Pxr24 (lossy)", ""},
		{2, "ZIP", 0, "ZIP (lossless)", ""},
		{3, "PIZ", 0, "PIZ (lossless)", ""},
		{4, "RLE", 0, "RLE (lossless)", ""},
		{0, NULL, 0, NULL, NULL}};
#endif

#ifdef WITH_OPENJPEG
	static EnumPropertyItem jp2_preset_items[] = {
		{0, "NO_PRESET", 0, "No Preset", ""},
		{1, "R_JPEG2K_CINE_PRESET", 0, "Cinema 24fps 2048x1080", ""},
		{2, "R_JPEG2K_CINE_PRESET|R_JPEG2K_CINE_48FPS", 0, "Cinema 48fps 2048x1080", ""},
		{3, "R_JPEG2K_CINE_PRESET", 0, "Cinema 24fps 4096x2160", ""},
		{4, "R_JPEG2K_CINE_PRESET", 0, "Cine-Scope 24fps 2048x858", ""},
		{5, "R_JPEG2K_CINE_PRESET|R_JPEG2K_CINE_48FPS", 0, "Cine-Scope 48fps 2048x858", ""},
		{6, "R_JPEG2K_CINE_PRESET", 0, "Cine-Flat 24fps 1998x1080", ""},
		{7, "R_JPEG2K_CINE_PRESET|R_JPEG2K_CINE_48FPS", 0, "Cine-Flat 48fps 1998x1080", ""},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem jp2_depth_items[] = {
		{0, "8", 0, "8", ""},
		{R_JPEG2K_12BIT, "16", 0, "16", ""},
		{R_JPEG2K_16BIT, "32", 0, "32", ""},
		{0, NULL, 0, NULL, NULL}};
#endif

#ifdef WITH_FFMPEG
	static EnumPropertyItem ffmpeg_format_items[] = {
		{FFMPEG_MPEG1, "MPEG1", 0, "MPEG-1", ""},
		{FFMPEG_MPEG2, "MPEG2", 0, "MPEG-2", ""},
		{FFMPEG_MPEG4, "MPEG4", 0, "MPEG-4", ""},
		{FFMPEG_AVI, "AVI", 0, "AVI", ""},
		{FFMPEG_MOV, "QUICKTIME", 0, "Quicktime", ""},
		{FFMPEG_DV, "DV", 0, "DV", ""},
		{FFMPEG_H264, "H264", 0, "H.264", ""},
		{FFMPEG_XVID, "XVID", 0, "Xvid", ""},
		// XXX broken
#if 0
#ifdef WITH_OGG
		{FFMPEG_OGG, "OGG", 0, "Ogg", ""},
#endif
#endif
		{FFMPEG_FLV, "FLASH", 0, "Flash", ""},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem ffmpeg_codec_items[] = {
		{CODEC_ID_MPEG1VIDEO, "MPEG1", 0, "MPEG-1", ""},
		{CODEC_ID_MPEG2VIDEO, "MPEG2", 0, "MPEG-2", ""},
		{CODEC_ID_MPEG4, "MPEG4", 0, "MPEG-4(divx)", ""},
		{CODEC_ID_HUFFYUV, "HUFFYUV", 0, "HuffYUV", ""},
		{CODEC_ID_DVVIDEO, "DV", 0, "DV", ""},
		{CODEC_ID_H264, "H264", 0, "H.264", ""},
		{CODEC_ID_XVID, "XVID", 0, "Xvid", ""},
#ifdef WITH_OGG
		{CODEC_ID_THEORA, "THEORA", 0, "Theora", ""},
#endif
		{CODEC_ID_FLV1, "FLASH", 0, "Flash Video", ""},
		{0, NULL, 0, NULL, NULL}};
		
	static EnumPropertyItem ffmpeg_audio_codec_items[] = {
		{CODEC_ID_MP2, "MP2", 0, "MP2", ""},
		{CODEC_ID_MP3, "MP3", 0, "MP3", ""},
		{CODEC_ID_AC3, "AC3", 0, "AC3", ""},
		{CODEC_ID_AAC, "AAC", 0, "AAC", ""},
		{CODEC_ID_VORBIS, "VORBIS", 0, "Vorbis", ""},
		{CODEC_ID_PCM_S16LE, "PCM", 0, "PCM", ""},
		{0, NULL, 0, NULL, NULL}};
#endif

	static EnumPropertyItem engine_items[] = {
		{0, "BLENDER_RENDER", 0, "Blender Render", ""},
		{0, NULL, 0, NULL, NULL}};

	srna= RNA_def_struct(brna, "SceneRenderData", NULL);
	RNA_def_struct_sdna(srna, "RenderData");
	RNA_def_struct_nested(brna, srna, "Scene");
	RNA_def_struct_path_func(srna, "rna_SceneRenderData_path");
	RNA_def_struct_ui_text(srna, "Render Data", "Rendering settings for a Scene datablock.");
	
	prop= RNA_def_property(srna, "color_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "planes");
	RNA_def_property_enum_items(prop, color_mode_items);
	RNA_def_property_ui_text(prop, "Color Mode", "Choose BW for saving greyscale images, RGB for saving red, green and blue channels, AND RGBA for saving red, green, blue + alpha channels");
	
	prop= RNA_def_property(srna, "resolution_x", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "xsch");
	RNA_def_property_range(prop, 4, 10000);
	RNA_def_property_ui_text(prop, "Resolution X", "Number of horizontal pixels in the rendered image.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS|NC_OBJECT, NULL);
	
	prop= RNA_def_property(srna, "resolution_y", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ysch");
	RNA_def_property_range(prop, 4, 10000);
	RNA_def_property_ui_text(prop, "Resolution Y", "Number of vertical pixels in the rendered image.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS|NC_OBJECT, NULL);
	
	prop= RNA_def_property(srna, "resolution_percentage", PROP_INT, PROP_PERCENTAGE);
	RNA_def_property_int_sdna(prop, NULL, "size");
	RNA_def_property_ui_range(prop, 1, 100, 10, 1);
	RNA_def_property_ui_text(prop, "Resolution %", "Percentage scale for render resolution");
	
	prop= RNA_def_property(srna, "parts_x", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "xparts");
	RNA_def_property_range(prop, 1, 512);
	RNA_def_property_ui_text(prop, "Parts X", "Number of horizontal tiles to use while rendering.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "parts_y", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "yparts");
	RNA_def_property_range(prop, 1, 512);
	RNA_def_property_ui_text(prop, "Parts Y", "Number of vertical tiles to use while rendering.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "pixel_aspect_x", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "xasp");
	RNA_def_property_range(prop, 1.0f, 200.0f);
	RNA_def_property_ui_text(prop, "Pixel Aspect X", "Horizontal aspect ratio - for anamorphic or non-square pixel output");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS|NC_OBJECT, NULL);
	
	prop= RNA_def_property(srna, "pixel_aspect_y", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "yasp");
	RNA_def_property_range(prop, 1.0f, 200.0f);
	RNA_def_property_ui_text(prop, "Pixel Aspect Y", "Vertical aspect ratio - for anamorphic or non-square pixel output");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS|NC_OBJECT, NULL);
	
	/* JPEG and AVI JPEG */
	
	prop= RNA_def_property(srna, "quality", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "quality");
	RNA_def_property_range(prop, 1, 100);
	RNA_def_property_ui_text(prop, "Quality", "Quality of JPEG images, AVI Jpeg and SGI movies.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	/* Tiff */
	
	prop= RNA_def_property(srna, "tiff_bit", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_TIFF_16BIT);
	RNA_def_property_ui_text(prop, "16 Bit", "Save TIFF with 16 bits per channel");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	/* Cineon and DPX */
	
	prop= RNA_def_property(srna, "cineon_log", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_CINEON_LOG);
	RNA_def_property_ui_text(prop, "Log", "Convert to logarithmic color space");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "cineon_black", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "cineonblack");
	RNA_def_property_range(prop, 0, 1024);
	RNA_def_property_ui_text(prop, "B", "Log conversion reference blackpoint");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "cineon_white", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "cineonwhite");
	RNA_def_property_range(prop, 0, 1024);
	RNA_def_property_ui_text(prop, "W", "Log conversion reference whitepoint");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "cineon_gamma", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "cineongamma");
	RNA_def_property_range(prop, 0.0f, 10.0f);
	RNA_def_property_ui_text(prop, "G", "Log conversion gamma");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

#ifdef WITH_OPENEXR	
	/* OpenEXR */

	prop= RNA_def_property(srna, "exr_codec", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "quality");
	RNA_def_property_enum_items(prop, exr_codec_items);
	RNA_def_property_ui_text(prop, "Codec", "Codec settings for OpenEXR");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "exr_half", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_OPENEXR_HALF);
	RNA_def_property_ui_text(prop, "Half", "Use 16 bit floats instead of 32 bit floats per channel");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "exr_zbuf", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_OPENEXR_ZBUF);
	RNA_def_property_ui_text(prop, "Zbuf", "Save the z-depth per pixel (32 bit unsigned int zbuffer)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "exr_preview", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_PREVIEW_JPG);
	RNA_def_property_ui_text(prop, "Preview", "When rendering animations, save JPG preview images in same directory");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
#endif

#ifdef WITH_OPENJPEG
	/* Jpeg 2000 */

	prop= RNA_def_property(srna, "jpeg_preset", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "jp2_preset");
	RNA_def_property_enum_items(prop, jp2_preset_items);
	RNA_def_property_ui_text(prop, "Preset", "Use a DCI Standard preset for saving jpeg2000");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "jpeg_depth", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "jp2_depth");
	RNA_def_property_enum_items(prop, jp2_depth_items);
	RNA_def_property_ui_text(prop, "Depth", "Bit depth per channel");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "jpeg_ycc", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "subimtype", R_JPEG2K_YCC);
	RNA_def_property_ui_text(prop, "YCC", "Save luminance-chrominance-chrominance channels instead of RGB colors");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
#endif

#ifdef WITH_FFMPEG
	/* FFMPEG Video*/
	
	prop= RNA_def_property(srna, "ffmpeg_format", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "ffcodecdata.type");
	RNA_def_property_enum_items(prop, ffmpeg_format_items);
	RNA_def_property_ui_text(prop, "Format", "Output file format");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_codec", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "ffcodecdata.codec");
	RNA_def_property_enum_items(prop, ffmpeg_codec_items);
	RNA_def_property_ui_text(prop, "Codec", "FFMpeg codec to use");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_video_bitrate", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.video_bitrate");
	RNA_def_property_range(prop, 1, 14000);
	RNA_def_property_ui_text(prop, "Bitrate", "Video bitrate(kb/s)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_minrate", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.rc_min_rate");
	RNA_def_property_range(prop, 0, 9000);
	RNA_def_property_ui_text(prop, "Min Rate", "Rate control: min rate(kb/s)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_maxrate", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.rc_max_rate");
	RNA_def_property_range(prop, 1, 14000);
	RNA_def_property_ui_text(prop, "Max Rate", "Rate control: max rate(kb/s)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_muxrate", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.mux_rate");
	RNA_def_property_range(prop, 0, 100000000);
	RNA_def_property_ui_text(prop, "Mux Rate", "Mux rate (bits/s(!))");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_gopsize", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.gop_size");
	RNA_def_property_range(prop, 0, 100);
	RNA_def_property_ui_text(prop, "GOP Size", "Distance between key frames");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_buffersize", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.rc_buffer_size");
	RNA_def_property_range(prop, 0, 2000);
	RNA_def_property_ui_text(prop, "Buffersize", "Rate control: buffer size (kb)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_packetsize", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.mux_packet_size");
	RNA_def_property_range(prop, 0, 16384);
	RNA_def_property_ui_text(prop, "Mux Packet Size", "Mux packet size (byte)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_autosplit", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ffcodecdata.flags", FFMPEG_AUTOSPLIT_OUTPUT);
	RNA_def_property_ui_text(prop, "Autosplit Output", "Autosplit output at 2GB boundary.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	/* FFMPEG Audio*/
	
	prop= RNA_def_property(srna, "ffmpeg_audio_codec", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "ffcodecdata.audio_codec");
	RNA_def_property_enum_items(prop, ffmpeg_audio_codec_items);
	RNA_def_property_ui_text(prop, "Audio Codec", "FFMpeg audio codec to use");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_audio_bitrate", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "ffcodecdata.audio_bitrate");
	RNA_def_property_range(prop, 32, 384);
	RNA_def_property_ui_text(prop, "Bitrate", "Audio bitrate(kb/s)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "ffmpeg_multiplex_audio", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ffcodecdata.flags", FFMPEG_MULTIPLEX_AUDIO);
	RNA_def_property_ui_text(prop, "Multiplex Audio", "Interleave audio with the output video");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
#endif

	prop= RNA_def_property(srna, "fps", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "frs_sec");
	RNA_def_property_range(prop, 1, 120);
	RNA_def_property_ui_text(prop, "FPS", "Framerate, expressed in frames per second.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "fps_base", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "frs_sec_base");
	RNA_def_property_range(prop, 0.1f, 120.0f);
	RNA_def_property_ui_text(prop, "FPS Base", "Framerate base");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "dither_intensity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "dither_intensity");
	RNA_def_property_range(prop, 0.0f, 2.0f);
	RNA_def_property_ui_text(prop, "Dither Intensity", "Amount of dithering noise added to the rendered image to break up banding.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "pixel_filter", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "filtertype");
	RNA_def_property_enum_items(prop, pixel_filter_items);
	RNA_def_property_ui_text(prop, "Pixel Filter", "Reconstruction filter used for combining anti-aliasing samples.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "filter_size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "gauss");
	RNA_def_property_range(prop, 0.5f, 1.5f);
	RNA_def_property_ui_text(prop, "Filter Size", "Pixel width over which the reconstruction filter combines samples.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "alpha_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "alphamode");
	RNA_def_property_enum_items(prop, alpha_mode_items);
	RNA_def_property_ui_text(prop, "Alpha Mode", "Representation of alpha information in the RGBA pixels.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "octree_resolution", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "ocres");
	RNA_def_property_enum_items(prop, octree_resolution_items);
	RNA_def_property_ui_text(prop, "Octree Resolution", "Resolution of raytrace accelerator. Use higher resolutions for larger scenes.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	prop= RNA_def_property(srna, "raytrace_structure", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "raystructure");
	RNA_def_property_enum_items(prop, raytrace_structure_items);
	RNA_def_property_ui_text(prop, "Raytrace Acceleration Structure", "Type of raytrace accelerator structure.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	prop= RNA_def_property(srna, "antialiasing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_OSA);
	RNA_def_property_ui_text(prop, "Anti-Aliasing", "Render and combine multiple samples per pixel to prevent jagged edges.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "antialiasing_samples", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "osa");
	RNA_def_property_enum_items(prop, fixed_oversample_items);
	RNA_def_property_ui_text(prop, "Anti-Aliasing Samples", "Amount of anti-aliasing samples per pixel.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "fields", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_FIELDS);
	RNA_def_property_ui_text(prop, "Fields", "Render image to two fields per frame, for interlaced TV output.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "field_order", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "mode");
	RNA_def_property_enum_items(prop, field_order_items);
	RNA_def_property_ui_text(prop, "Field Order", "Order of video fields. Select which lines get rendered first, to create smooth motion for TV output");
	
	prop= RNA_def_property(srna, "fields_still", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_FIELDSTILL);
	RNA_def_property_ui_text(prop, "Fields Still", "Disable the time difference between fields.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "sync_audio", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "audio.flag", AUDIO_SYNC);
	RNA_def_property_ui_text(prop, "Sync Audio", "Play back and sync with audio from Sequence Editor");
	RNA_def_property_update(prop, NC_SCENE, NULL);
	
	prop= RNA_def_property(srna, "render_shadows", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_SHADOW);
	RNA_def_property_ui_text(prop, "Render Shadows", "Calculate shadows while rendering.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "render_envmaps", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_ENVMAP);
	RNA_def_property_ui_text(prop, "Render Environment Maps", "Calculate environment maps while rendering.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "render_radiosity", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_RADIO);
	RNA_def_property_ui_text(prop, "Render Radiosity", "Calculate radiosity in a pre-process before rendering.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "render_sss", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_SSS);
	RNA_def_property_ui_text(prop, "Render SSS", "Calculate sub-surface scattering in materials rendering.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "render_raytracing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_RAYTRACE);
	RNA_def_property_ui_text(prop, "Render Raytracing", "Pre-calculate the raytrace accelerator and render raytracing effects.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "render_textures", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "scemode", R_NO_TEX);
	RNA_def_property_ui_text(prop, "Render Textures", "Use textures to affect material properties.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "edge", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_EDGE);
	RNA_def_property_ui_text(prop, "Edge", "Create a toon outline around the edges of geometry");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "edge_intensity", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "edgeint");
	RNA_def_property_range(prop, 0, 255);
	RNA_def_property_ui_text(prop, "Edge Intensity", "Threshold for drawing outlines on geometry edges");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "edge_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "edgeR");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Color", "");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "threads", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "threads");
	RNA_def_property_range(prop, 1, 8);
	RNA_def_property_int_funcs(prop, "rna_SceneRenderData_threads_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Threads", "Number of CPU threads to use simultaneously while rendering (for multi-core/CPU systems)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "threads_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "mode");
	RNA_def_property_enum_items(prop, threads_mode_items);
	RNA_def_property_ui_text(prop, "Threads Mode", "Determine the amount of render threads used");
	
	prop= RNA_def_property(srna, "motion_blur", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_MBLUR);
	RNA_def_property_ui_text(prop, "Motion Blur", "Use multi-sampled 3D scene motion blur (uses number of anti-aliasing samples).");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "border", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_BORDER);
	RNA_def_property_ui_text(prop, "Border", "Render a user-defined border region, within the frame size.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "crop_to_border", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_CROP);
	RNA_def_property_ui_text(prop, "Crop to Border", "Crop the rendered frame to the defined border size.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "placeholders", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_TOUCH);
	RNA_def_property_ui_text(prop, "Placeholders", "Create empty placeholder files while rendering frames (similar to Unix 'touch').");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "no_overwrite", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mode", R_NO_OVERWRITE);
	RNA_def_property_ui_text(prop, "No Overwrite", "Skip and don't overwrite existing files while rendering");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "use_compositing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_DOCOMP);
	RNA_def_property_ui_text(prop, "Compositing", "Process the render result through the compositing pipeline, if compositing nodes are enabled.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "use_sequencer", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_DOSEQ);
	RNA_def_property_ui_text(prop, "Sequencer", "Process the render (and composited) result through the video sequence editor pipeline, if sequencer strips exist.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "color_management", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "color_mgt_flag", R_COLOR_MANAGEMENT);
	RNA_def_property_ui_text(prop, "Color Management", "Use color profiles and gamma corrected imaging pipeline");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS|NC_MATERIAL|ND_SHADING, NULL);
	
	prop= RNA_def_property(srna, "file_extensions", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_EXTENSION);
	RNA_def_property_ui_text(prop, "File Extensions", "Add the file format extensions to the rendered file name (eg: filename + .jpg)");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "file_format", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "imtype");
	RNA_def_property_enum_items(prop, image_type_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_SceneRenderData_file_format_set", NULL);
	RNA_def_property_ui_text(prop, "File Format", "File format to save the rendered images as.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "free_image_textures", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_FREE_IMAGE);
	RNA_def_property_ui_text(prop, "Free Image Textures", "Free all image texture from memory after render, to save memory before compositing.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "save_buffers", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_EXR_TILE_FILE);
	RNA_def_property_boolean_funcs(prop, "rna_SceneRenderData_save_buffers_get", NULL);
	RNA_def_property_ui_text(prop, "Save Buffers","Save tiles for all RenderLayers and SceneNodes to files in the temp directory (saves memory, required for Full Sample).");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "full_sample", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_FULL_SAMPLE);
	RNA_def_property_ui_text(prop, "Full Sample","Save for every anti-aliasing sample the entire RenderLayer results. This solves anti-aliasing issues with compositing.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "backbuf", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "bufflag", R_BACKBUF);
	RNA_def_property_ui_text(prop, "Back Buffer", "Render backbuffer image");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	prop= RNA_def_property(srna, "display_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "displaymode");
	RNA_def_property_enum_items(prop, display_mode_items);
	RNA_def_property_ui_text(prop, "Display", "Select where rendered images will be displayed");
	
	prop= RNA_def_property(srna, "output_path", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "pic");
	RNA_def_property_ui_text(prop, "Output Path", "Directory/name to save animations, # characters defines the position and length of frame numbers.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	/* stamp */
	
	prop= RNA_def_property(srna, "stamp_time", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_TIME);
	RNA_def_property_ui_text(prop, "Stamp Time", "Include the current time in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_date", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_DATE);
	RNA_def_property_ui_text(prop, "Stamp Date", "Include the current date in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_frame", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_FRAME);
	RNA_def_property_ui_text(prop, "Stamp Frame", "Include the frame number in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_camera", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_CAMERA);
	RNA_def_property_ui_text(prop, "Stamp Camera", "Include the name of the active camera in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_scene", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_SCENE);
	RNA_def_property_ui_text(prop, "Stamp Scene", "Include the name of the active scene in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_note", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_NOTE);
	RNA_def_property_ui_text(prop, "Stamp Note", "Include a custom note in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_marker", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_MARKER);
	RNA_def_property_ui_text(prop, "Stamp Marker", "Include the name of the last marker in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_filename", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_FILENAME);
	RNA_def_property_ui_text(prop, "Stamp Filename", "Include the filename of the .blend file in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_sequence_strip", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_SEQSTRIP);
	RNA_def_property_ui_text(prop, "Stamp Sequence Strip", "Include the name of the foreground sequence strip in image metadata");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_note_text", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "stamp_udata");
	RNA_def_property_ui_text(prop, "Stamp Note Text", "Custom text to appear in the stamp note");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "render_stamp", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "stamp", R_STAMP_DRAW);
	RNA_def_property_ui_text(prop, "Render Stamp", "Render the stamp info text in the rendered image");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_font_size", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "stamp_font_id");
	RNA_def_property_enum_items(prop, stamp_font_size_items);
	RNA_def_property_ui_text(prop, "Stamp Font Size", "Size of the font used when rendering stamp text");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_foreground", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "fg_stamp");
	RNA_def_property_array(prop, 4);
	RNA_def_property_range(prop,0.0,1.0);
	RNA_def_property_ui_text(prop, "Stamp Foreground", "Stamp text color");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "stamp_background", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "bg_stamp");
	RNA_def_property_array(prop, 4);
	RNA_def_property_range(prop,0.0,1.0);
	RNA_def_property_ui_text(prop, "Stamp Background", "Color to use behind stamp text");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	/* layers */
	
	prop= RNA_def_property(srna, "layers", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "layers", NULL);
	RNA_def_property_struct_type(prop, "SceneRenderLayer");
	RNA_def_property_ui_text(prop, "Render Layers", "");

	prop= RNA_def_property(srna, "single_layer", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scemode", R_SINGLE_LAYER);
	RNA_def_property_ui_text(prop, "Single Layer", "Only render the active layer.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	prop= RNA_def_property(srna, "active_layer_index", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "actlay");
	RNA_def_property_int_funcs(prop, "rna_SceneRenderData_active_layer_index_get", "rna_SceneRenderData_active_layer_index_set", "rna_SceneRenderData_active_layer_index_range");
	RNA_def_property_ui_text(prop, "Active Layer Index", "Active index in render layer array.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);

	/* engine */
	prop= RNA_def_property(srna, "engine", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, engine_items);
	RNA_def_property_enum_funcs(prop, "rna_SceneRenderData_engine_get", "rna_SceneRenderData_engine_set", "rna_SceneRenderData_engine_itemf");
	RNA_def_property_ui_text(prop, "Engine", "Engine to use for rendering.");
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop= RNA_def_property(srna, "multiple_engines", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_SceneRenderData_multiple_engines_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Multiple Engines", "More than one rendering engine is available.");

	prop= RNA_def_property(srna, "use_game_engine", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_SceneRenderData_use_game_engine_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Use Game Engine", "Current rendering engine is a game engine.");
}

void RNA_def_scene(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	/* Struct definition */
	srna= RNA_def_struct(brna, "Scene", "ID");
	RNA_def_struct_ui_text(srna, "Scene", "Scene consisting objects and defining time and render related settings.");
	RNA_def_struct_ui_icon(srna, ICON_SCENE_DATA);
	RNA_def_struct_clear_flag(srna, STRUCT_ID_REFCOUNT);
	
	/* Global Settings */
	prop= RNA_def_property(srna, "camera", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Camera", "Active camera used for rendering the scene.");

	prop= RNA_def_property(srna, "world", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "World", "World used for rendering the scene.");
	RNA_def_property_update(prop, NC_WORLD, NULL);

	prop= RNA_def_property(srna, "cursor_location", PROP_FLOAT, PROP_XYZ|PROP_UNIT_LENGTH);
	RNA_def_property_float_sdna(prop, NULL, "cursor");
	RNA_def_property_ui_text(prop, "Cursor Location", "3D cursor location.");
	RNA_def_property_ui_range(prop, -10000.0, 10000.0, 10, 4);
	RNA_def_property_update(prop, NC_WINDOW, NULL);
	
	/* Bases/Objects */
	prop= RNA_def_property(srna, "objects", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "base", NULL);
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_ui_text(prop, "Objects", "");
	RNA_def_property_collection_funcs(prop, 0, 0, 0, "rna_Scene_objects_get", 0, 0, 0, 0, 0);

	/* Layers */
	prop= RNA_def_property(srna, "visible_layers", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_ui_text(prop, "Visible Layers", "Layers visible when rendering the scene.");
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Scene_layer_set");
	
	
	/* Frame Range Stuff */
	prop= RNA_def_property(srna, "current_frame", PROP_INT, PROP_TIME);
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_int_sdna(prop, NULL, "r.cfra");
	RNA_def_property_range(prop, MINAFRAME, MAXFRAME);
	RNA_def_property_ui_text(prop, "Current Frame", "");
	RNA_def_property_update(prop, NC_SCENE|ND_FRAME, "rna_Scene_frame_update");
	
	prop= RNA_def_property(srna, "start_frame", PROP_INT, PROP_TIME);
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_int_sdna(prop, NULL, "r.sfra");
	RNA_def_property_int_funcs(prop, NULL, "rna_Scene_start_frame_set", NULL);
	RNA_def_property_ui_text(prop, "Start Frame", "");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "end_frame", PROP_INT, PROP_TIME);
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_int_sdna(prop, NULL, "r.efra");
	RNA_def_property_int_funcs(prop, NULL, "rna_Scene_end_frame_set", NULL);
	RNA_def_property_ui_text(prop, "End Frame", "");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "frame_step", PROP_INT, PROP_TIME);
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_int_sdna(prop, NULL, "frame_step");
	RNA_def_property_ui_text(prop, "Frame Step", "Number of frames to skip forward while rendering/playing back each frame");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	/* Preview Range (frame-range for UI playback) */
	prop=RNA_def_property(srna, "use_preview_range", PROP_BOOLEAN, PROP_NONE); /* use_preview_range is not really a separate setting in SDNA */
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_boolean_funcs(prop, "rna_Scene_use_preview_range_get", "rna_Scene_use_preview_range_set");
	RNA_def_property_ui_text(prop, "Use Preview Range", "");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "preview_range_start_frame", PROP_INT, PROP_TIME);
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_int_sdna(prop, NULL, "r.psfra");
	RNA_def_property_int_funcs(prop, NULL, "rna_Scene_preview_range_start_frame_set", NULL);
	RNA_def_property_ui_text(prop, "Preview Range Start Frame", "");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	prop= RNA_def_property(srna, "preview_range_end_frame", PROP_INT, PROP_TIME);
	RNA_def_property_clear_flag(prop, PROP_ANIMATEABLE);
	RNA_def_property_int_sdna(prop, NULL, "r.pefra");
	RNA_def_property_int_funcs(prop, NULL, "rna_Scene_preview_range_end_frame_set", NULL);
	RNA_def_property_ui_text(prop, "Preview Range End Frame", "");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	/* Stamp */
	prop= RNA_def_property(srna, "stamp_note", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "r.stamp_udata");
	RNA_def_property_ui_text(prop, "Stamp Note", "User define note for the render stamping.");
	RNA_def_property_update(prop, NC_SCENE|ND_RENDER_OPTIONS, NULL);
	
	/* Nodes (Compositing) */
	prop= RNA_def_property(srna, "nodetree", PROP_POINTER, PROP_NONE);
	RNA_def_property_ui_text(prop, "Node Tree", "Compositing node tree.");
	
	/* Sequencer */
	prop= RNA_def_property(srna, "sequence_editor", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "ed");
	RNA_def_property_struct_type(prop, "SequenceEditor");
	RNA_def_property_ui_text(prop, "Sequence Editor", "");
	
	/* Keying Sets */
	prop= RNA_def_property(srna, "keying_sets", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "keyingsets", NULL);
	RNA_def_property_struct_type(prop, "KeyingSet");
	RNA_def_property_ui_text(prop, "Keying Sets", "Keying Sets for this Scene.");
	RNA_def_property_update(prop, NC_SCENE|ND_KEYINGSET, NULL);
	
	prop= RNA_def_property(srna, "active_keying_set", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "KeyingSet");
	RNA_def_property_editable_func(prop, "rna_Scene_active_keying_set_editable");
	RNA_def_property_pointer_funcs(prop, "rna_Scene_active_keying_set_get", "rna_Scene_active_keying_set_set", NULL);
	RNA_def_property_ui_text(prop, "Active Keying Set", "Active Keying Set used to insert/delete keyframes.");
	RNA_def_property_update(prop, NC_SCENE|ND_KEYINGSET, NULL);
	
	prop= RNA_def_property(srna, "active_keying_set_index", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "active_keyingset");
	RNA_def_property_int_funcs(prop, "rna_Scene_active_keying_set_index_get", "rna_Scene_active_keying_set_index_set", "rna_Scene_active_keying_set_index_range");
	RNA_def_property_ui_text(prop, "Active Keying Set Index", "Current Keying Set index.");
	RNA_def_property_update(prop, NC_SCENE|ND_KEYINGSET, NULL);
	
	/* Tool Settings */
	prop= RNA_def_property(srna, "tool_settings", PROP_POINTER, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "toolsettings");
	RNA_def_property_struct_type(prop, "ToolSettings");
	RNA_def_property_ui_text(prop, "Tool Settings", "");

	/* Unit Settings */
	prop= RNA_def_property(srna, "unit_settings", PROP_POINTER, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "unit");
	RNA_def_property_struct_type(prop, "UnitSettings");
	RNA_def_property_ui_text(prop, "Unit Settings", "Unit editing settings");
	
	/* Render Data */
	prop= RNA_def_property(srna, "render_data", PROP_POINTER, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "r");
	RNA_def_property_struct_type(prop, "SceneRenderData");
	RNA_def_property_ui_text(prop, "Render Data", "");
	
	/* Markers */
	prop= RNA_def_property(srna, "timeline_markers", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "markers", NULL);
	RNA_def_property_struct_type(prop, "TimelineMarker");
	RNA_def_property_ui_text(prop, "Timeline Markers", "Markers used in all timelines for the current scene.");

	/* Game Settings */
	prop= RNA_def_property(srna, "game_data", PROP_POINTER, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "gm");
	RNA_def_property_struct_type(prop, "SceneGameData");
	RNA_def_property_ui_text(prop, "Game Data", "");
	
	rna_def_tool_settings(brna);
	rna_def_unit_settings(brna);
	rna_def_scene_render_data(brna);
	rna_def_scene_game_data(brna);
	rna_def_scene_render_layer(brna);
}

#endif

